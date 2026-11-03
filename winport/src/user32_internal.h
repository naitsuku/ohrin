/*
	user32_internal.h — user32 実装の内部構造（ウィンドウ・メッセージキュー・描画面）

	スレッドモデル（docs/porting/mac_native_plan.md「仮想プロセス」）:
	- Win32 側のコード（sakura の各プロセス）は Win32 スレッドで動く。各スレッドがメッセージキューを持つ
	- macOS のメインスレッドは Cocoa のイベントループ専用（表示サーバ役）。Win32 のウィンドウ
	  プロシージャは決して呼ばない。入力はメッセージとして所有スレッドのキューに積む
	- 描画は各ウィンドウの描画面（Surface: CGBitmapContext）に行い、メインスレッドが画面へ転送する

	排他: ウィンドウ木と各ウィンドウの属性は g_userMutex（再帰ロック）で守る。
	ウィンドウプロシージャはロックを持たずに呼ぶこと。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <windows.h>
#include <commctrl.h>

// Windows ヘッダの `#define interface struct` が Apple のヘッダ（IOKit 等）と衝突するので一時的に外す
#pragma push_macro("interface")
#undef interface
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#pragma pop_macro("interface")

#include "backend.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace winport {

inline backend::Rect ToB(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
inline RECT ToRECT(const backend::Rect& r) { return {r.left, r.top, r.right, r.bottom}; }

// ---- 描画面 ----
//! ウィンドウのクライアント領域の画素（座標は GDI 単位 = Cocoa のポイント、y 下向き）
struct Surface {
	CGContextRef ctx = nullptr;   //!< 画素は width*scale x height*scale
	int width = 0;
	int height = 0;
	double scale = 1.0;
	std::mutex mutex;             //!< 描画（Win32 スレッド）と転送（メインスレッド）の排他
	~Surface();
};
std::shared_ptr<Surface> CreateSurface(int width, int height, double scale, const Surface* copyFrom);

// ---- ウィンドウクラス ----
struct WindowClass {
	std::wstring name;
	UINT style = 0;
	WNDPROC proc = nullptr;
	int cbClsExtra = 0;
	int cbWndExtra = 0;
	HINSTANCE instance = nullptr;
	HICON icon = nullptr;
	HCURSOR cursor = nullptr;
	HBRUSH background = nullptr;
	ATOM atom = 0;
	bool system = false;          //!< 組み込みクラス（BUTTON 等の未実装コントロールを含む）
};

struct Subclass {
	SUBCLASSPROC proc;
	UINT_PTR id;
	DWORD_PTR refData;
};

// ---- ウィンドウ ----
struct Window {
	DWORD magic = 0x57494E44;     //!< 'WIND'。HWND の妥当性確認用
	const WindowClass* cls = nullptr;
	WNDPROC proc = nullptr;
	DWORD style = 0;
	DWORD exStyle = 0;
	std::wstring text;
	HWND parent = nullptr;        //!< WS_CHILD の親
	HWND owner = nullptr;         //!< トップレベルの所有者
	std::vector<HWND> children;   //!< Z 順（先頭が最前面）
	RECT rect{};                  //!< ウィンドウ矩形（子は親のクライアント座標、トップレベルはスクリーン座標）
	UINT_PTR id = 0;              //!< GWLP_ID（子ウィンドウ）またはメニュー
	HMENU menu = nullptr;
	HINSTANCE instance = nullptr;
	LONG_PTR userData = 0;
	std::vector<unsigned char> extra;  //!< cbWndExtra
	DWORD threadId = 0;
	DWORD processId = 0;          //!< 仮想プロセス ID（process.cpp）
	bool enabled = true;
	bool destroying = false;
	std::vector<Subclass> subclasses;  //!< comctl32 SetWindowSubclass（末尾が最後に追加したもの）

	// スクロールバー（WS_VSCROLL / WS_HSCROLL）
	SCROLLINFO scroll[2]{};       //!< [SB_HORZ, SB_VERT]
	bool scrollVisible[2]{};

	// 再描画
	bool updatePending = false;
	RECT updateRect{};            //!< 無効領域の外接矩形（クライアント座標）
	bool eraseBackground = false;

	// 表示（メインスレッドのオブジェクト）
	void* platform = nullptr;     //!< Cocoa 側のオブジェクト（cocoa_backend.mm が管理）
	std::shared_ptr<Surface> surface;
};

std::recursive_mutex& UserMutex();

//! HWND → Window*（無効なら nullptr）
Window* GetWindowObject(HWND hwnd);
inline HWND ToHwnd(Window* w) { return reinterpret_cast<HWND>(w); }

//! 非クライアント領域の幅（枠・スクロールバー）。クライアント矩形はウィンドウ矩形からこれを引いたもの
RECT NonClientInsets(const Window* w);
//! クライアント領域の大きさ
SIZE ClientSize(const Window* w);
//! ウィンドウ矩形の原点のスクリーン座標
POINT WindowOriginOnScreen(const Window* w);
//! クライアント領域の原点のスクリーン座標
POINT ClientOriginOnScreen(const Window* w);
//! 最上位の祖先（トップレベル）
Window* TopLevelOf(Window* w);
//! 自分と祖先がすべて WS_VISIBLE か
bool IsReallyVisible(const Window* w);

//! 描画面を（必要なら作り直して）取得する
std::shared_ptr<Surface> EnsureSurface(Window* w);

//! 無効領域を足し、所有スレッドを起こす
void AddUpdateRect(Window* w, const RECT* rc, bool erase);

//! ウィンドウ一覧（ロック中に使う）
const std::vector<HWND>& AllWindows();

// ---- メッセージキュー ----
struct SentMessage {
	HWND hwnd;
	UINT msg;
	WPARAM wParam;
	LPARAM lParam;
	LRESULT result = 0;
	bool done = false;
};

struct Timer {
	HWND hwnd;
	UINT_PTR id;
	UINT elapse;
	TIMERPROC proc;
	unsigned long long due;  //!< ミリ秒（GetTickCount64 基準）
};

struct ThreadQueue {
	DWORD threadId = 0;
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<MSG> posted;
	std::deque<SentMessage*> sent;
	std::vector<Timer> timers;
	bool quit = false;
	int quitCode = 0;
	bool paintPending = false;   //!< どれかのウィンドウに無効領域がある
	// ウィンドウ状態（スレッドごと）
	HWND focus = nullptr;
	HWND active = nullptr;
	HWND capture = nullptr;
	// TranslateMessage 用: WM_KEYDOWN の time 値 → 入力文字
	std::map<DWORD, std::wstring> keyChars;
};

//! 指定スレッドのキュー（無ければ作る）
ThreadQueue& QueueOf(DWORD threadId);
ThreadQueue& CurrentQueue();
//! キューへの投函（どのスレッドからでも可）
void PostToQueue(DWORD threadId, const MSG& msg);
//! スレッドを起こす（無効領域・タイマー変化時など）
void WakeQueue(DWORD threadId);

//! メインスレッド（Cocoa）からの入力投函。time に入力文字の対応付けを残す
void PostKeyMessage(HWND hwnd, UINT msg, WPARAM vk, LPARAM lParam, const std::wstring& chars);

// ---- キーボード状態（メインスレッドが更新、Win32 スレッドが参照） ----
void SetKeyDown(int vk, bool down);
bool IsKeyDown(int vk);

}  // namespace winport
