/*
	backend.h — Win32 側（user32 / gdi32）と表示系（cocoa_backend.mm）の境界

	このヘッダは Windows ヘッダに依存しない（Cocoa と windows.h は同じ翻訳単位に入れられないため）。
	ウィンドウは void*（= HWND）、矩形は backend::Rect（RECT と同じ並び）で受け渡す。

	- backend::*  … Win32 側 → 表示系。どのスレッドから呼んでもよく、メインスレッドへ非同期に回す
	               （Win32 スレッドがメインスレッドを待つとデッドロックし得るため、待たない）
	- bridge::*   … 表示系（メインスレッド）→ Win32 側。状態の読み取りと入力の投函（bridge.cpp）

	SPDX-License-Identifier: Zlib
*/
#pragma once
// windows.h の後に読まれても Apple のヘッダが壊れないよう `interface` マクロを一時的に外す
#pragma push_macro("interface")
#undef interface
#include <CoreGraphics/CoreGraphics.h>
#pragma pop_macro("interface")

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace winport::backend {

struct Rect {
	int left, top, right, bottom;
	int Width() const { return right - left; }
	int Height() const { return bottom - top; }
};

//! アプリ起動時にメインスレッドで呼ぶ（NSApplication の準備と画面情報の確定）
void Initialize();
//! メインスレッドのイベントループ（戻らない）
//! Cocoa のイベントループを回す。起動が完了したら LaunchFirstProcess を呼ぶ
void Run(int argc, char** argv);

//! ウィンドウが表示状態になった／隠れた（トップレベルなら NSWindow を出す）
void WindowVisibilityChanged(void* hwnd);
//! ウィンドウが破棄された（platform は Window が持っていた表示系オブジェクト）
void WindowDestroyed(void* hwnd, void* platform);
//! 位置・大きさ・スタイルが変わった
void FrameChanged(void* hwnd);
//! タイトルが変わった
void TextChanged(void* hwnd);
//! 描画面の dirty 矩形（クライアント座標）を画面へ反映する
void Present(void* hwnd, const Rect& dirty);
//! アクティブ化（キーウィンドウにする）
void Activate(void* hwnd);
//! キャレット（hwnd のクライアント座標）。hwnd = nullptr で消す
void CaretChanged(void* hwnd, const Rect& rc, bool visible);
//! マウスカーソル（IDC_* の値）
void SetCursorShape(uintptr_t cursorId);
//! スクロールバーの状態が変わった
void ScrollBarsChanged(void* hwnd);
//! 警告音
void Beep();
//! 全仮想プロセスが終了した
void Quit();
// ---- 開く／保存／フォントのパネル ----
//! パネルの補助ビューに写すコントロール
struct PanelControl {
	int id = 0;
	bool check = false;                //!< false ならポップアップ
	std::u16string label;
	std::vector<std::u16string> items;
	int value = 0;                     //!< 選択の位置、またはチェック状態
	bool enabled = true;
};
struct PanelRequest {
	bool save = false;
	bool multiple = false;
	std::u16string title, directory, name;
	std::vector<PanelControl> controls;
};
struct PanelResult {
	std::atomic<bool> done{false};
	bool ok = false;
	std::vector<std::u16string> paths;
	std::vector<int> values;           //!< PanelRequest::controls と同じ並び
};
struct FontChoice {
	std::u16string family;
	int size = 12;
	bool bold = false, italic = false;
};
//! 待たずに戻り、閉じられたら result->done が立つ。request は閉じるまで呼び出し側が保持する
void ShowFilePanel(const PanelRequest* request, PanelResult* result);
//! 閉じられたら *result が 1（OK）か -1（キャンセル）になり、OK なら choice を更新する
void ShowFontPanel(FontChoice* choice, std::atomic<int>* result);
//! システムのクリップボード。どのスレッドから呼んでもよく、メインスレッドを待たない
long ClipboardChangeCount();
void ClipboardWriteText(const std::u16string& text);   //!< 内容を置き換える（空なら消すだけ）
bool ClipboardReadText(std::u16string* text);
//! ウィンドウのメニューが変わった（アクティブなウィンドウなら mac のメニューバーを作り直す）
void MenuBarChanged(void* hwnd);
//! ポップアップメニューを出す。待たずに戻り、閉じられたら *result に項目 ID（選択なしは -1）が入る
void ShowPopupMenu(void* hmenu, int x, int y, std::atomic<int>* result);
//! 未確定文字列の描画に使うフォント（PostScript 名、大きさ）
void SetImeFont(const char* postscriptName, double size);
//! 表示系が動いているか（テストでは Initialize を呼ばないので false）
bool Available();
//! メッセージボックスを出す。待たずに戻り、閉じられたら *result に IDOK 等が入る（type は MB_* の値）
void ShowMessageBox(const std::u16string& text, const std::u16string& caption, unsigned type, std::atomic<int>* result);

//! 画面情報（Initialize で確定。Win32 のスクリーン座標 = 主画面左上原点・y 下向き）
double ScaleFactor();
Rect ScreenRect();
Rect WorkArea();
int TitleBarHeight();
//! マウス位置（スクリーン座標）
void CursorPos(int* x, int* y);

}  // namespace winport::backend

namespace winport {
//! 最初の仮想プロセス（sakura の wWinMain）をコマンドライン引数付きで起動する（process.cpp）
//! documents は起動時に Finder から渡されたファイル（UTF-8）
void LaunchFirstProcess(int argc, char** argv, const std::vector<std::string>& documents);
//! 起動後にファイルを開く。空なら新しい無題のウィンドウ
void OpenDocument(const std::string& path);
}  // namespace winport

namespace winport::bridge {

using backend::Rect;

//! 表示系が必要とするウィンドウの状態（UserMutex の下で一度に読む）
struct WindowInfo {
	bool valid = false;
	bool topLevel = false;
	bool visible = false;         //!< 自分と祖先がすべて可視
	bool selfVisible = false;     //!< 自分の WS_VISIBLE
	bool captioned = false;       //!< タイトルバー付き
	bool resizable = false;
	bool minimizable = false;
	void* parent = nullptr;
	void* owner = nullptr;        //!< 所有者のトップレベル
	Rect windowRect{};            //!< トップレベル: スクリーン座標 / 子: 親のクライアント座標
	Rect clientInView{};          //!< 表示系のビュー（トップレベルはコンテンツ領域、子はウィンドウ矩形）内のクライアント矩形
	std::u16string title;
	bool scrollVisible[2]{};      //!< [横, 縦]
	int scrollMin[2]{}, scrollMax[2]{}, scrollPage[2]{}, scrollPos[2]{};
	bool dblclks = false;         //!< CS_DBLCLKS
	void* platform = nullptr;
};

WindowInfo GetInfo(void* hwnd);
std::vector<void*> Children(void* hwnd);
void SetPlatform(void* hwnd, void* platform);

//! 描画面の画像（呼び出し側が CGImageRelease する）。scale は 1 ポイントあたりの画素数
CGImageRef SnapshotSurface(void* hwnd, double* scale, int* width, int* height);

// ---- メニュー ----
struct MenuItemInfo {
	std::u16string text;   //!< "開く(&O)...\tCtrl+O" のような Win32 の表記
	unsigned id = 0;
	void* sub = nullptr;   //!< サブメニュー（HMENU）
	bool separator = false;
	bool checked = false;
	bool disabled = false;
};
void* MenuOfWindow(void* hwnd);
std::vector<MenuItemInfo> GetMenuItems(void* hmenu);
//! 所有スレッドで WM_INITMENUPOPUP を処理させ、完了を待つ（timeoutMs で打ち切り）。
//! 表示系が Win32 スレッドを待つのはここだけ。Win32 スレッドは表示系を待たないのでデッドロックしない
void InitMenuPopup(void* hwnd, void* hmenu, int position, int timeoutMs);
void PostMenuCommand(void* hwnd, unsigned id);
//! 全トップレベルウィンドウに閉じる要求を出す（Cmd+Q）
void CloseAllWindows();

//! 診断用: 全ウィンドウの状態を stderr に出す（環境変数 WINPORT_DUMP=1 で起動 5 秒後に呼ばれる）
void DumpWindows();

// ---- 入力（メインスレッドから投函） ----
enum class Mouse { move, down, up, doubleClick };
enum Modifier : unsigned { kShift = 1, kControl = 2, kAlt = 4, kCommand = 8, kLeft = 16, kRight = 32, kMiddle = 64 };

//! button: 0 = 左, 1 = 右, 2 = 中。x, y はスクリーン座標
void PostMouse(void* hwnd, Mouse kind, int button, int x, int y, unsigned modifiers);
//! delta は WHEEL_DELTA(120) 単位
void PostWheel(void* hwnd, int delta, bool horizontal, int x, int y, unsigned modifiers);
//! macKeyCode は NSEvent.keyCode。chars は入力文字（修飾キー付きなら空）
void PostKey(void* topLevel, int macKeyCode, bool down, bool repeat, const std::u16string& chars, unsigned modifiers);
//! 修飾キーの押下状態の変化
void ModifiersChanged(void* topLevel, unsigned modifiers);
//! IME の確定文字列。フォーカスのあるウィンドウへ WM_IME_COMPOSITION(GCS_RESULTSTR) を送る
//! ファイルのドロップ。hwnd はドロップされたビューのウィンドウ、x / y はスクリーン座標
void PostDropFiles(void* hwnd, const std::vector<std::u16string>& paths, int x, int y);
void PostImeResult(void* topLevel, const std::u16string& text);
//! ユーザー操作でトップレベルが動いた／大きさが変わった（スクリーン座標のウィンドウ矩形）
void FrameMovedByUser(void* hwnd, const Rect& windowRect);
void Activated(void* hwnd, bool active);
void CloseRequested(void* hwnd);
//! スクロールバー操作。bar: 0 = 横, 1 = 縦。code は SB_*、pos は SB_THUMBTRACK/THUMBPOSITION 用
void PostScroll(void* hwnd, int bar, int code, int pos);

}  // namespace winport::bridge
