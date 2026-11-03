/*
	bridge.cpp — 表示系（メインスレッド）から Win32 側への窓口（backend.h の bridge::*）

	- 状態の読み取り（GetInfo / SnapshotSurface）
	- 入力の変換と投函: mac のキーコード → 仮想キー、Cmd → Ctrl（要求仕様 4）、Option → Alt
	- ウィンドウ操作（移動・大きさ・アクティブ化・閉じる）は内部メッセージで所有スレッドに渡し、
	  そのスレッドで SetWindowPos 等を実行する（HandleInternalMessage）

	SPDX-License-Identifier: Zlib
*/
#include "user32_internal.h"

#include <imm.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma push_macro("interface")
#undef interface
#include <ImageIO/ImageIO.h>
#pragma pop_macro("interface")
#include <functional>

#include "winport_internal.h"

namespace winport {

// 内部メッセージ（アプリのメッセージと衝突しないよう WM_USER 範囲の末尾を使う）
constexpr UINT WM_WINPORT_FRAME = 0x7FF0;      //!< wParam = MAKEWPARAM(x, y), lParam = MAKELPARAM(cx, cy)
constexpr UINT WM_WINPORT_ACTIVATE = 0x7FF1;   //!< wParam = TRUE/FALSE
constexpr UINT WM_WINPORT_SETCURSOR = 0x7FF2;  //!< lParam = MAKELPARAM(HTCLIENT, mouse message)

constexpr UINT WM_WINPORT_IMERESULT = 0x7FF3;  //!< lParam = new std::wstring（受け側で解放）

constexpr UINT WM_WINPORT_INITMENU = 0x7FF4;   //!< wParam = HMENU, lParam = new InitMenuRequest

extern thread_local std::wstring t_imeResult;  // imm32.cpp

struct InitMenuRequest {
	int position;
	std::shared_ptr<std::atomic<bool>> done;
};

bool IsInternalMessage(UINT msg) { return msg >= WM_WINPORT_FRAME && msg <= WM_WINPORT_INITMENU; }

void HandleInternalMessage(const MSG& m)
{
	switch (m.message) {
	case WM_WINPORT_FRAME:
		SetWindowPos(m.hwnd, nullptr, static_cast<short>(LOWORD(m.wParam)), static_cast<short>(HIWORD(m.wParam)),
		             static_cast<short>(LOWORD(m.lParam)), static_cast<short>(HIWORD(m.lParam)), SWP_NOZORDER | SWP_NOACTIVATE);
		UpdateWindow(m.hwnd);
		break;
	case WM_WINPORT_ACTIVATE:
		if (m.wParam) {
			if (!IsWindowEnabled(m.hwnd)) {
				// モーダルダイアログの下のウィンドウがクリックされたら、ダイアログを前へ戻す
				std::lock_guard lock(UserMutex());
				for (auto h : AllWindows()) {
					auto o = reinterpret_cast<Window*>(h);
					if (o->owner == m.hwnd && (o->style & WS_VISIBLE) && o->enabled) backend::Activate(h);
				}
				break;
			}
			SetActiveWindow(m.hwnd);
		} else if (CurrentQueue().active == m.hwnd) {
			SetActiveWindow(nullptr);
			SetFocus(nullptr);
		}
		break;
	case WM_WINPORT_IMERESULT: {
		std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(m.lParam));
		t_imeResult = *text;
		SendMessageW(m.hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
		SendMessageW(m.hwnd, WM_IME_COMPOSITION, 0, GCS_RESULTSTR);
		SendMessageW(m.hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
		t_imeResult.clear();
		break;
	}
	case WM_WINPORT_INITMENU: {
		std::unique_ptr<InitMenuRequest> req(reinterpret_cast<InitMenuRequest*>(m.lParam));
		SendMessageW(m.hwnd, WM_INITMENUPOPUP, m.wParam, MAKELPARAM(req->position, 0));
		*req->done = true;
		break;
	}
	case WM_WINPORT_SETCURSOR:
		if (!SendMessageW(m.hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(m.hwnd), m.lParam)) SetCursor(LoadCursorW(nullptr, IDC_ARROW));
		break;
	default:
		break;
	}
}

namespace {

void Post(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto w = GetWindowObject(hwnd);
	if (!w) return;
	PostToQueue(w->threadId, MSG{hwnd, msg, wParam, lParam, static_cast<DWORD>(GetTickCount()), {}});
}

//! mac のキーコード（Carbon kVK_*）→ 仮想キー
struct KeyMap { int mac; int vk; bool extended; };
constexpr KeyMap kKeys[] = {
	{0x00, 'A'}, {0x01, 'S'}, {0x02, 'D'}, {0x03, 'F'}, {0x04, 'H'}, {0x05, 'G'}, {0x06, 'Z'}, {0x07, 'X'},
	{0x08, 'C'}, {0x09, 'V'}, {0x0B, 'B'}, {0x0C, 'Q'}, {0x0D, 'W'}, {0x0E, 'E'}, {0x0F, 'R'}, {0x10, 'Y'},
	{0x11, 'T'}, {0x12, '1'}, {0x13, '2'}, {0x14, '3'}, {0x15, '4'}, {0x16, '6'}, {0x17, '5'}, {0x18, VK_OEM_PLUS},
	{0x19, '9'}, {0x1A, '7'}, {0x1B, VK_OEM_MINUS}, {0x1C, '8'}, {0x1D, '0'}, {0x1E, VK_OEM_6}, {0x1F, 'O'},
	{0x20, 'U'}, {0x21, VK_OEM_4}, {0x22, 'I'}, {0x23, 'P'}, {0x24, VK_RETURN}, {0x25, 'L'}, {0x26, 'J'},
	{0x27, VK_OEM_7}, {0x28, 'K'}, {0x29, VK_OEM_1}, {0x2A, VK_OEM_5}, {0x2B, VK_OEM_COMMA}, {0x2C, VK_OEM_2},
	{0x2D, 'N'}, {0x2E, 'M'}, {0x2F, VK_OEM_PERIOD}, {0x30, VK_TAB}, {0x31, VK_SPACE}, {0x32, VK_OEM_3},
	{0x33, VK_BACK}, {0x35, VK_ESCAPE}, {0x36, VK_CONTROL, true}, {0x37, VK_CONTROL}, {0x38, VK_SHIFT},
	{0x39, VK_CAPITAL}, {0x3A, VK_MENU}, {0x3B, VK_CONTROL}, {0x3C, VK_SHIFT}, {0x3D, VK_MENU, true},
	{0x3E, VK_CONTROL, true}, {0x40, VK_F17}, {0x41, VK_DECIMAL}, {0x43, VK_MULTIPLY}, {0x45, VK_ADD},
	{0x47, VK_CLEAR}, {0x4B, VK_DIVIDE, true}, {0x4C, VK_RETURN, true}, {0x4E, VK_SUBTRACT}, {0x51, VK_OEM_PLUS},
	{0x52, VK_NUMPAD0}, {0x53, VK_NUMPAD1}, {0x54, VK_NUMPAD2}, {0x55, VK_NUMPAD3}, {0x56, VK_NUMPAD4},
	{0x57, VK_NUMPAD5}, {0x58, VK_NUMPAD6}, {0x59, VK_NUMPAD7}, {0x5B, VK_NUMPAD8}, {0x5C, VK_NUMPAD9},
	{0x5D, VK_OEM_5}, {0x5E, VK_OEM_102}, {0x5F, VK_SEPARATOR},
	{0x60, VK_F5}, {0x61, VK_F6}, {0x62, VK_F7}, {0x63, VK_F3}, {0x64, VK_F8}, {0x65, VK_F9}, {0x67, VK_F11},
	{0x69, VK_F13}, {0x6A, VK_F16}, {0x6B, VK_F14}, {0x6D, VK_F10}, {0x6F, VK_F12}, {0x71, VK_F15},
	{0x72, VK_INSERT, true}, {0x73, VK_HOME, true}, {0x74, VK_PRIOR, true}, {0x75, VK_DELETE, true}, {0x76, VK_F4},
	{0x77, VK_END, true}, {0x78, VK_F2}, {0x79, VK_NEXT, true}, {0x7A, VK_F1}, {0x7B, VK_LEFT, true},
	{0x7C, VK_RIGHT, true}, {0x7D, VK_DOWN, true}, {0x7E, VK_UP, true},
};

const KeyMap* FindKey(int mac)
{
	for (auto& k : kKeys) if (k.mac == mac) return &k;
	return nullptr;
}

HWND KeyTarget(void* topLevel)
{
	auto w = GetWindowObject(static_cast<HWND>(topLevel));
	if (!w || !w->enabled) return nullptr;
	auto& q = QueueOf(w->threadId);
	const HWND f = q.focus;
	if (f && GetWindowObject(f) && TopLevelOf(GetWindowObject(f)) == w) return f;
	return static_cast<HWND>(topLevel);
}

WPARAM MouseFlags(unsigned mods)
{
	WPARAM f = 0;
	if (mods & bridge::kLeft) f |= MK_LBUTTON;
	if (mods & bridge::kRight) f |= MK_RBUTTON;
	if (mods & bridge::kMiddle) f |= MK_MBUTTON;
	if (mods & bridge::kShift) f |= MK_SHIFT;
	if (mods & (bridge::kControl | bridge::kCommand)) f |= MK_CONTROL;
	return f;
}

}  // namespace

}  // namespace winport

namespace winport::bridge {

WindowInfo GetInfo(void* hwnd)
{
	WindowInfo info;
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(static_cast<HWND>(hwnd));
	if (!w) return info;
	info.valid = true;
	info.topLevel = w->parent == nullptr;
	info.visible = IsReallyVisible(w);
	info.selfVisible = (w->style & WS_VISIBLE) != 0;
	info.captioned = (w->style & WS_CAPTION) == WS_CAPTION && info.topLevel;
	info.resizable = (w->style & WS_THICKFRAME) != 0 || (w->style & WS_SIZEBOX) != 0 || ((w->style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW);
	info.minimizable = (w->style & WS_MINIMIZEBOX) != 0;
	info.parent = w->parent;
	if (auto o = GetWindowObject(w->owner)) info.owner = TopLevelOf(o);
	if (info.topLevel) {
		const POINT o = WindowOriginOnScreen(w);
		info.windowRect = {o.x, o.y, o.x + (w->rect.right - w->rect.left), o.y + (w->rect.bottom - w->rect.top)};
	} else {
		info.windowRect = ToB(w->rect);
	}
	RECT nc = NonClientInsets(w);
	if (info.captioned) nc.top -= backend::TitleBarHeight();
	const SIZE cs = ClientSize(w);
	info.clientInView = {nc.left, nc.top, nc.left + cs.cx, nc.top + cs.cy};
	info.title.assign(w->text.begin(), w->text.end());
	for (int b = 0; b < 2; ++b) {
		info.scrollVisible[b] = w->scrollVisible[b];
		info.scrollMin[b] = w->scroll[b].nMin;
		info.scrollMax[b] = w->scroll[b].nMax;
		info.scrollPage[b] = static_cast<int>(w->scroll[b].nPage);
		info.scrollPos[b] = w->scroll[b].nPos;
	}
	info.dblclks = (w->cls->style & CS_DBLCLKS) != 0;
	info.platform = w->platform;
	return info;
}

std::vector<void*> Children(void* hwnd)
{
	std::lock_guard lock(UserMutex());
	auto w = GetWindowObject(static_cast<HWND>(hwnd));
	std::vector<void*> out;
	if (w) for (auto c : w->children) out.push_back(c);
	return out;
}

void SetPlatform(void* hwnd, void* platform)
{
	std::lock_guard lock(UserMutex());
	if (auto w = GetWindowObject(static_cast<HWND>(hwnd))) w->platform = platform;
}

CGImageRef SnapshotSurface(void* hwnd, double* scale, int* width, int* height)
{
	std::shared_ptr<Surface> s;
	{
		std::lock_guard lock(UserMutex());
		auto w = GetWindowObject(static_cast<HWND>(hwnd));
		if (!w) return nullptr;
		s = w->surface;
	}
	if (!s || !s->ctx) return nullptr;
	std::lock_guard lock(s->mutex);
	*scale = s->scale;
	*width = s->width;
	*height = s->height;
	return CGBitmapContextCreateImage(s->ctx);
}

void DumpWindows()
{
	std::lock_guard lock(UserMutex());
	std::function<void(HWND, int)> dump = [&](HWND h, int depth) {
		auto w = reinterpret_cast<Window*>(h);
		const SIZE cs = ClientSize(w);
		std::fprintf(stderr, "%*s%p %s \"%s\" rect=(%ld,%ld)-(%ld,%ld) client=%ldx%ld style=%08x%s%s surface=%s platform=%p tid=%u\n",
		             depth * 2, "", static_cast<void*>(h), WideToUtf8(w->cls->name).c_str(), WideToUtf8(w->text.substr(0, 40)).c_str(),
		             static_cast<long>(w->rect.left), static_cast<long>(w->rect.top), static_cast<long>(w->rect.right), static_cast<long>(w->rect.bottom),
		             static_cast<long>(cs.cx), static_cast<long>(cs.cy), static_cast<unsigned>(w->style),
		             (w->style & WS_VISIBLE) ? " VISIBLE" : "", w->updatePending ? " DIRTY" : "",
		             w->surface ? "yes" : "no", w->platform, static_cast<unsigned>(w->threadId));
		if (w->surface && w->surface->ctx) {
			// 描画面を PNG で保存する（$WINPORT_DUMP_DIR、無ければ /tmp）
			const char* dir = std::getenv("WINPORT_DUMP_DIR");
			char path[512];
			std::snprintf(path, sizeof(path), "%s/winport_%p.png", dir ? dir : "/tmp", static_cast<void*>(h));
			std::lock_guard sl(w->surface->mutex);
			if (CGImageRef img = CGBitmapContextCreateImage(w->surface->ctx)) {
				CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path), std::strlen(path), false);
				if (CGImageDestinationRef dst = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr)) {
					CGImageDestinationAddImage(dst, img, nullptr);
					CGImageDestinationFinalize(dst);
					CFRelease(dst);
				}
				CFRelease(url);
				CGImageRelease(img);
			}
		}
		for (auto c : w->children) dump(c, depth + 1);
	};
	for (auto h : AllWindows()) if (!reinterpret_cast<Window*>(h)->parent) dump(h, 0);
}

void PostMouse(void* hwnd, Mouse kind, int button, int x, int y, unsigned modifiers)
{
	auto w = GetWindowObject(static_cast<HWND>(hwnd));
	if (!w) return;
	if (!w->enabled || !TopLevelOf(w)->enabled) return;  // Win32 と同じく無効なウィンドウは入力を受けない
	HWND target = static_cast<HWND>(hwnd);
	if (const HWND cap = QueueOf(w->threadId).capture; cap && GetWindowObject(cap)) target = cap;
	POINT pt{x, y};
	ScreenToClient(target, &pt);
	const LPARAM lp = MAKELPARAM(static_cast<short>(pt.x), static_cast<short>(pt.y));
	UINT msg = WM_MOUSEMOVE;
	static constexpr UINT kDown[] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN};
	static constexpr UINT kUp[] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP};
	static constexpr UINT kDbl[] = {WM_LBUTTONDBLCLK, WM_RBUTTONDBLCLK, WM_MBUTTONDBLCLK};
	const int b = std::clamp(button, 0, 2);
	switch (kind) {
	case Mouse::move: msg = WM_MOUSEMOVE; break;
	case Mouse::down: msg = kDown[b]; break;
	case Mouse::up: msg = kUp[b]; break;
	case Mouse::doubleClick: {
		auto t = GetWindowObject(target);
		msg = (t && (t->cls->style & CS_DBLCLKS)) ? kDbl[b] : kDown[b];
		break;
	}
	}
	if (std::getenv("WINPORT_TRACE") && kind != Mouse::move) {
		std::fprintf(stderr, "mouse %s msg=%04x screen=(%d,%d) client=(%ld,%ld)\n", WideToUtf8(GetWindowObject(target)->cls->name).c_str(), msg, x, y,
		             static_cast<long>(pt.x), static_cast<long>(pt.y));
	}
	if (kind == Mouse::move || kind == Mouse::down) Post(target, WM_WINPORT_SETCURSOR, 0, MAKELPARAM(HTCLIENT, msg));
	Post(target, msg, MouseFlags(modifiers), lp);
	if (kind == Mouse::up && b == 1) {
		// DefWindowProc は WM_RBUTTONUP で WM_CONTEXTMENU を出す
		Post(target, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(target), MAKELPARAM(static_cast<short>(x), static_cast<short>(y)));
	}
}

void PostWheel(void* hwnd, int delta, bool horizontal, int x, int y, unsigned modifiers)
{
	if (!GetWindowObject(static_cast<HWND>(hwnd))) return;
	Post(static_cast<HWND>(hwnd), horizontal ? WM_MOUSEHWHEEL : WM_MOUSEWHEEL,
	     MAKEWPARAM(MouseFlags(modifiers), static_cast<short>(delta)), MAKELPARAM(static_cast<short>(x), static_cast<short>(y)));
}

void ModifiersChanged(void* topLevel, unsigned modifiers)
{
	struct { unsigned bit; int vk; } const mods[] = {{kShift, VK_SHIFT}, {kControl | kCommand, VK_CONTROL}, {kAlt, VK_MENU}};
	const HWND target = KeyTarget(topLevel);
	for (auto& m : mods) {
		const bool down = (modifiers & m.bit) != 0;
		if (down == IsKeyDown(m.vk)) continue;
		SetKeyDown(m.vk, down);
		if (m.vk == VK_SHIFT) SetKeyDown(VK_LSHIFT, down);
		if (m.vk == VK_CONTROL) SetKeyDown(VK_LCONTROL, down);
		if (m.vk == VK_MENU) SetKeyDown(VK_LMENU, down);
		if (target) {
			const bool sys = (modifiers & kAlt) && !(modifiers & (kControl | kCommand));
			const UINT msg = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);
			LPARAM lp = 1 | (down ? 0 : (1u << 30) | (1u << 31));
			if (modifiers & kAlt) lp |= 1u << 29;
			PostKeyMessage(target, msg, m.vk, lp, {});
		}
	}
}

void PostKey(void* topLevel, int macKeyCode, bool down, bool repeat, const std::u16string& chars, unsigned modifiers)
{
	const HWND target = KeyTarget(topLevel);
	if (!target) return;
	const KeyMap* k = FindKey(macKeyCode);
	if (!k) return;
	ModifiersChanged(topLevel, modifiers);  // flagsChanged が先に来ない場合でも TranslateAccelerator が正しく判定できるように
	SetKeyDown(k->vk, down);
	const bool ctrl = (modifiers & (kControl | kCommand)) != 0;
	const bool alt = (modifiers & kAlt) != 0;
	const bool sys = alt && !ctrl;
	const UINT msg = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);
	LPARAM lp = 1;
	if (k->extended) lp |= 1u << 24;
	if (alt) lp |= 1u << 29;
	if (!down || repeat) lp |= 1u << 30;
	if (!down) lp |= 1u << 31;
	std::wstring text;
	if (down) {
		if (ctrl) {
			// Windows の TranslateMessage と同じく Ctrl+英字は制御文字（0x01-0x1A）になる
			if (k->vk >= 'A' && k->vk <= 'Z') text += static_cast<wchar_t>(k->vk - 'A' + 1);
		} else if (k->vk == VK_RETURN) {
			text += L'\r';
		} else if (k->vk == VK_BACK) {
			text += L'\b';
		} else if (k->vk == VK_TAB) {
			text += L'\t';
		} else if (k->vk == VK_ESCAPE) {
			text += L'\x1B';
		} else {
			for (char16_t c : chars) {
				// NSEvent の特殊キー（矢印等）は私用領域 0xF700-0xF8FF の文字として来るので除く
				if (c >= 0xF700 && c <= 0xF8FF) continue;
				if (c == 0x7F) continue;
				text += static_cast<wchar_t>(c);
			}
		}
	}
	if (std::getenv("WINPORT_TRACE")) std::fprintf(stderr, "key mac=%02x vk=%02x down=%d chars=%zu text=%zu mods=%x\n", macKeyCode, k->vk, down, chars.size(), text.size(), modifiers);
	PostKeyMessage(target, msg, k->vk, lp, text);
}

void PostDropFiles(void* hwnd, const std::vector<std::u16string>& paths, int x, int y)
{
	auto w = GetWindowObject(static_cast<HWND>(hwnd));
	if (!w) return;
	// DragAcceptFiles しているウィンドウを親へたどる。無ければトップレベルへ
	Window* target = w;
	while (target && !target->acceptFiles) target = GetWindowObject(target->parent);
	if (!target) target = TopLevelOf(w);
	if (!target->enabled || !TopLevelOf(target)->enabled) return;
	POINT pt{x, y};
	ScreenToClient(ToHwnd(target), &pt);
	std::vector<std::wstring> files;
	for (const auto& p : paths) files.emplace_back(p.begin(), p.end());
	Post(ToHwnd(target), WM_DROPFILES, reinterpret_cast<WPARAM>(CreateDropHandle(files, pt)), 0);
}

void PostImeResult(void* topLevel, const std::u16string& text)
{
	const HWND target = KeyTarget(topLevel);
	if (!target) return;
	Post(target, WM_WINPORT_IMERESULT, 0, reinterpret_cast<LPARAM>(new std::wstring(text.begin(), text.end())));
}

void* MenuOfWindow(void* hwnd) { return GetMenu(static_cast<HWND>(hwnd)); }

void InitMenuPopup(void* hwnd, void* hmenu, int position, int timeoutMs)
{
	auto done = std::make_shared<std::atomic<bool>>(false);
	Post(static_cast<HWND>(hwnd), WM_WINPORT_INITMENU, reinterpret_cast<WPARAM>(hmenu), reinterpret_cast<LPARAM>(new InitMenuRequest{position, done}));
	for (int i = 0; i < timeoutMs && !*done; ++i) Sleep(1);
}

void PostMenuCommand(void* hwnd, unsigned id) { Post(static_cast<HWND>(hwnd), WM_COMMAND, MAKEWPARAM(id, 0), 0); }

void CloseAllWindows()
{
	std::vector<HWND> tops;
	{
		std::lock_guard lock(UserMutex());
		for (auto h : AllWindows()) {
			auto w = reinterpret_cast<Window*>(h);
			if (!w->parent && (w->style & WS_VISIBLE) && (w->style & WS_CAPTION) == WS_CAPTION) tops.push_back(h);
		}
	}
	for (auto h : tops) CloseRequested(h);
}

void FrameMovedByUser(void* hwnd, const Rect& r)
{
	Post(static_cast<HWND>(hwnd), WM_WINPORT_FRAME, MAKEWPARAM(static_cast<short>(r.left), static_cast<short>(r.top)),
	     MAKELPARAM(static_cast<short>(r.Width()), static_cast<short>(r.Height())));
}

void Activated(void* hwnd, bool active) { Post(static_cast<HWND>(hwnd), WM_WINPORT_ACTIVATE, active, 0); }

void CloseRequested(void* hwnd) { Post(static_cast<HWND>(hwnd), WM_SYSCOMMAND, SC_CLOSE, 0); }

void PostScroll(void* hwnd, int bar, int code, int pos)
{
	auto w = GetWindowObject(static_cast<HWND>(hwnd));
	if (!w) return;
	{
		std::lock_guard lock(UserMutex());
		w->scroll[bar].nTrackPos = pos;
	}
	Post(static_cast<HWND>(hwnd), bar == 1 ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, static_cast<WORD>(pos)), 0);
}

}  // namespace winport::bridge
