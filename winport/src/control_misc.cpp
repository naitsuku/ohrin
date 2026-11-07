/*
	control_misc.cpp — トラックバー、ホットキー、ラベル編集のエディット

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <string>

#include "control_common.h"
#include "dialog_internal.h"
#include "user32_internal.h"

namespace {

using namespace winport;

// ---- トラックバー ----
struct TrackBar {
	int low = 0;
	int high = 100;
	int pos = 0;
	int page = 20;
	int line = 1;
	bool dragging = false;
	bool focused = false;
};

constexpr int kThumbWidth = 10;

TrackBar* TrackOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? static_cast<TrackBar*>(w->control) : nullptr;
}

int TrackThumbX(HWND hwnd, const TrackBar* t)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const int span = std::max<int>(rc.right - kThumbWidth - 2, 1);
	return 1 + (t->high > t->low ? span * (t->pos - t->low) / (t->high - t->low) : 0);
}

int TrackPosAt(HWND hwnd, const TrackBar* t, int x)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const int span = std::max<int>(rc.right - kThumbWidth - 2, 1);
	return std::clamp(t->low + (x - kThumbWidth / 2 - 1) * (t->high - t->low) / span, t->low, t->high);
}

void TrackNotify(HWND hwnd, const TrackBar* t, WORD code)
{
	const UINT msg = (GetWindowLongW(hwnd, GWL_STYLE) & TBS_VERT) ? WM_VSCROLL : WM_HSCROLL;
	SendMessageW(GetParent(hwnd), msg, MAKEWPARAM(code, t->pos), reinterpret_cast<LPARAM>(hwnd));
}

void TrackMove(HWND hwnd, TrackBar* t, int pos, WORD code)
{
	pos = std::clamp(pos, t->low, t->high);
	if (pos == t->pos) return;
	t->pos = pos;
	InvalidateRect(hwnd, nullptr, FALSE);
	TrackNotify(hwnd, t, code);
}

LRESULT CALLBACK TrackBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TrackBar* t = TrackOf(hwnd);
	if (msg == WM_NCCREATE) {
		GetWindowObject(hwnd)->control = new TrackBar;
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	if (!t) return DefWindowProcW(hwnd, msg, wParam, lParam);
	switch (msg) {
	case WM_NCDESTROY:
		delete t;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
	case WM_GETDLGCODE: return DLGC_WANTARROWS;
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		t->focused = msg == WM_SETFOCUS;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
		const int mid = rc.bottom / 2;
		const RECT groove{2, mid - 2, rc.right - 2, mid + 2};
		FillColor(hdc, groove, kControlInactive);
		const int x = TrackThumbX(hwnd, t);
		const RECT thumb{x, 1, x + kThumbWidth, rc.bottom - 1};
		const bool enabled = IsWindowEnabled(hwnd) != FALSE;
		FillColor(hdc, thumb, enabled ? RGB(255, 255, 255) : RGB(235, 235, 235));
		FrameColor(hdc, thumb, t->focused ? kControlAccent : RGB(140, 140, 140));
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_LBUTTONDOWN: {
		SetFocus(hwnd);
		const int x = static_cast<short>(LOWORD(lParam));
		const int thumb = TrackThumbX(hwnd, t);
		if (x >= thumb && x < thumb + kThumbWidth) {
			t->dragging = true;
			SetCapture(hwnd);
		} else {
			TrackMove(hwnd, t, t->pos + (x < thumb ? -t->page : t->page), x < thumb ? TB_PAGEUP : TB_PAGEDOWN);
		}
		return 0;
	}
	case WM_MOUSEMOVE:
		if (t->dragging) TrackMove(hwnd, t, TrackPosAt(hwnd, t, static_cast<short>(LOWORD(lParam))), TB_THUMBTRACK);
		return 0;
	case WM_LBUTTONUP:
		if (t->dragging) {
			t->dragging = false;
			ReleaseCapture();
			TrackNotify(hwnd, t, TB_THUMBPOSITION);
			TrackNotify(hwnd, t, TB_ENDTRACK);
		}
		return 0;
	case WM_KEYDOWN:
		switch (wParam) {
		case VK_LEFT: case VK_UP: TrackMove(hwnd, t, t->pos - t->line, TB_LINEUP); return 0;
		case VK_RIGHT: case VK_DOWN: TrackMove(hwnd, t, t->pos + t->line, TB_LINEDOWN); return 0;
		case VK_PRIOR: TrackMove(hwnd, t, t->pos - t->page, TB_PAGEUP); return 0;
		case VK_NEXT: TrackMove(hwnd, t, t->pos + t->page, TB_PAGEDOWN); return 0;
		case VK_HOME: TrackMove(hwnd, t, t->low, TB_TOP); return 0;
		case VK_END: TrackMove(hwnd, t, t->high, TB_BOTTOM); return 0;
		default: break;
		}
		break;
	case TBM_GETPOS: return t->pos;
	case TBM_SETPOS:
		t->pos = std::clamp(static_cast<int>(lParam), t->low, t->high);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case TBM_SETRANGE:
		t->low = static_cast<short>(LOWORD(lParam));
		t->high = static_cast<short>(HIWORD(lParam));
		t->pos = std::clamp(t->pos, t->low, t->high);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case TBM_SETRANGEMIN: t->low = static_cast<int>(lParam); t->pos = std::max(t->pos, t->low); return 0;
	case TBM_SETRANGEMAX: t->high = static_cast<int>(lParam); t->pos = std::min(t->pos, t->high); return 0;
	case TBM_GETRANGEMIN: return t->low;
	case TBM_GETRANGEMAX: return t->high;
	case TBM_SETPAGESIZE: { const int previous = t->page; t->page = static_cast<int>(lParam); return previous; }
	case TBM_GETPAGESIZE: return t->page;
	case TBM_SETLINESIZE: { const int previous = t->line; t->line = static_cast<int>(lParam); return previous; }
	case TBM_GETLINESIZE: return t->line;
	case TBM_SETTICFREQ:
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- ホットキー ----
struct HotKey {
	WORD key = 0;	//!< LOBYTE が仮想キー、HIBYTE が HOTKEYF_*
	bool focused = false;
	bool border = false;
};

HotKey* HotKeyOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? static_cast<HotKey*>(w->control) : nullptr;
}

std::wstring KeyName(BYTE vk)
{
	if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk));
	if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
	if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
	switch (vk) {
	case VK_SPACE: return L"Space";
	case VK_TAB: return L"Tab";
	case VK_RETURN: return L"Enter";
	case VK_INSERT: return L"Insert";
	case VK_DELETE: return L"Delete";
	case VK_HOME: return L"Home";
	case VK_END: return L"End";
	case VK_PRIOR: return L"Page Up";
	case VK_NEXT: return L"Page Down";
	case VK_LEFT: return L"Left";
	case VK_RIGHT: return L"Right";
	case VK_UP: return L"Up";
	case VK_DOWN: return L"Down";
	default: return vk ? L"#" + std::to_wstring(vk) : L"";
	}
}

std::wstring HotKeyText(WORD key)
{
	const BYTE flags = HIBYTE(key);
	std::wstring text;
	// Windows の Ctrl は mac の Cmd に置き換えている
	if (flags & HOTKEYF_CONTROL) text += L"Cmd + ";
	if (flags & HOTKEYF_SHIFT) text += L"Shift + ";
	if (flags & HOTKEYF_ALT) text += L"Option + ";
	text += KeyName(LOBYTE(key));
	return text.empty() ? L"なし" : text;
}

LRESULT CALLBACK HotKeyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HotKey* h = HotKeyOf(hwnd);
	if (msg == WM_NCCREATE) {
		auto w = GetWindowObject(hwnd);
		h = new HotKey;
		w->control = h;
		h->border = (w->style & WS_BORDER) || (w->exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
		w->style &= ~WS_BORDER;
		w->exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	if (!h) return DefWindowProcW(hwnd, msg, wParam, lParam);
	switch (msg) {
	case WM_NCDESTROY:
		delete h;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
	case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_WANTARROWS;
	case WM_LBUTTONDOWN: SetFocus(hwnd); return 0;
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
		h->focused = msg == WM_SETFOCUS;
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		const bool enabled = IsWindowEnabled(hwnd) != FALSE;
		FillColor(hdc, rc, enabled ? RGB(255, 255, 255) : RGB(245, 245, 245));
		FrameColor(hdc, rc, h->focused ? kControlAccent : kControlBorder);
		HGDIOBJ old = SelectObject(hdc, DialogFont());
		const std::wstring text = HotKeyText(h->key);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, enabled ? RGB(0, 0, 0) : kControlDisabledText);
		RECT inner = rc;
		inner.left += 4;
		DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &inner, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		SelectObject(hdc, old);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN: {
		if (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU) return 0;
		if ((wParam == VK_BACK || wParam == VK_DELETE) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
			h->key = 0;
		} else {
			BYTE flags = 0;
			if (GetKeyState(VK_SHIFT) & 0x8000) flags |= HOTKEYF_SHIFT;
			if (GetKeyState(VK_CONTROL) & 0x8000) flags |= HOTKEYF_CONTROL;
			if (GetKeyState(VK_MENU) & 0x8000) flags |= HOTKEYF_ALT;
			h->key = MAKEWORD(static_cast<BYTE>(wParam), flags);
		}
		InvalidateRect(hwnd, nullptr, FALSE);
		SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), EN_CHANGE), reinterpret_cast<LPARAM>(hwnd));
		return 0;
	}
	case WM_CHAR:
	case WM_SYSCHAR:
		return 0;
	case HKM_SETHOTKEY:
		h->key = static_cast<WORD>(wParam);
		InvalidateRect(hwnd, nullptr, FALSE);
		return 0;
	case HKM_GETHOTKEY:
		return h->key;
	case HKM_SETRULES:
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- ラベル編集 ----
LRESULT CALLBACK LabelEditSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR owner)
{
	const HWND target = reinterpret_cast<HWND>(owner);
	switch (msg) {
	case WM_GETDLGCODE:
		return DefSubclassProc(hwnd, msg, wParam, lParam) | DLGC_WANTALLKEYS;
	case WM_KEYDOWN:
		if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
			SendMessageW(target, kEndLabelEdit, wParam == VK_RETURN, 0);
			return 0;
		}
		break;
	case WM_CHAR:
		if (wParam == L'\r' || wParam == 27) return 0;
		break;
	case WM_KILLFOCUS: {
		const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
		SendMessageW(target, kEndLabelEdit, TRUE, 0);
		return result;
	}
	default:
		break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

namespace winport {

WNDPROC SmallControlProc(const wchar_t* name)
{
	if (!_wcsicmp(name, L"msctls_trackbar32")) return TrackBarProc;
	if (!_wcsicmp(name, L"msctls_hotkey32")) return HotKeyProc;
	return nullptr;
}

HWND CreateLabelEdit(HWND owner, const RECT& rc, const std::wstring& text, HFONT font)
{
	HWND edit = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
	                            owner, nullptr, nullptr, nullptr);
	if (!edit) return nullptr;
	SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
	SendMessageW(edit, EM_SETSEL, 0, -1);
	SetWindowSubclass(edit, LabelEditSubclass, 1, reinterpret_cast<DWORD_PTR>(owner));
	return edit;
}

void CloseLabelEdit(HWND, HWND edit)
{
	if (!edit) return;
	RemoveWindowSubclass(edit, LabelEditSubclass, 1);
	ShowWindow(edit, SW_HIDE);
	PostMessageW(edit, WM_CLOSE, 0, 0);
}

}  // namespace winport
