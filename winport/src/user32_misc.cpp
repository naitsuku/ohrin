/*
	user32_misc.cpp — システム情報、矩形、キャレット、カーソル、スクロールバー、メニュー、
	アクセラレータ、フック、クリップボード、文字関数

	メニューはデータ構造のみ（mac のメニューバーへの反映は Week 5）。
	クリップボードは当面プロセス内のみ（NSPasteboard 連携は Week 6）。

	SPDX-License-Identifier: Zlib
*/
#include "gdi_internal.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>

#include "winport_internal.h"

using namespace winport;

namespace {

// ---- メニュー ----
struct MenuItem {
	UINT type = MFT_STRING;
	UINT state = 0;
	UINT id = 0;
	HMENU sub = nullptr;
	std::wstring text;
	ULONG_PTR data = 0;
};

struct Menu {
	DWORD magic = 0x4D454E55;  // 'MENU'
	bool popup = false;
	std::vector<MenuItem> items;
	MENUINFO info{sizeof(MENUINFO)};
};

std::mutex& MenuMutex() { static auto* m = new std::mutex; return *m; }

Menu* MenuOf(HMENU h)
{
	auto m = reinterpret_cast<Menu*>(h);
	return (m && m->magic == 0x4D454E55) ? m : nullptr;
}

int FindItem(Menu* m, UINT item, UINT flags)
{
	if (flags & MF_BYPOSITION) return item < m->items.size() ? static_cast<int>(item) : -1;
	for (size_t i = 0; i < m->items.size(); ++i) if (m->items[i].id == item && !m->items[i].sub) return static_cast<int>(i);
	// サブメニューの中も探す（Win32 と同じ）
	return -1;
}

MenuItem* FindItemDeep(Menu* m, UINT item, UINT flags, Menu** owner = nullptr)
{
	const int i = FindItem(m, item, flags);
	if (i >= 0) { if (owner) *owner = m; return &m->items[i]; }
	if (flags & MF_BYPOSITION) return nullptr;
	for (auto& it : m->items) {
		if (auto sub = MenuOf(it.sub)) if (auto r = FindItemDeep(sub, item, flags, owner)) return r;
	}
	return nullptr;
}

MenuItem MakeItem(UINT flags, UINT_PTR id, LPCWSTR text)
{
	MenuItem it;
	if (flags & MF_SEPARATOR) it.type = MFT_SEPARATOR;
	else if (flags & MF_OWNERDRAW) it.type = MFT_OWNERDRAW;
	if (flags & MF_POPUP) it.sub = reinterpret_cast<HMENU>(id);
	else it.id = static_cast<UINT>(id);
	if (flags & MF_CHECKED) it.state |= MFS_CHECKED;
	if (flags & (MF_GRAYED | MF_DISABLED)) it.state |= MFS_DISABLED;
	if ((flags & MF_OWNERDRAW) || (flags & MF_BITMAP)) it.data = reinterpret_cast<ULONG_PTR>(text);
	else if (text && !(flags & MF_SEPARATOR)) it.text = text;
	return it;
}

// ---- アクセラレータ ----
struct AccelTable {
	DWORD magic = 0x41434345;  // 'ACCE'
	std::vector<ACCEL> entries;
};

// ---- フック ----
struct Hook {
	int id;
	HOOKPROC proc;
	DWORD threadId;
};
std::mutex& HookMutex() { static auto* m = new std::mutex; return *m; }
std::vector<Hook*>& Hooks() { static auto* v = new std::vector<Hook*>; return *v; }

// ---- キャレット（スレッドごと） ----
struct Caret {
	HWND hwnd = nullptr;
	int width = 0, height = 0;
	POINT pos{};
	int hideCount = 1;  // CreateCaret 直後は非表示
};
thread_local Caret t_caret;

void UpdateCaret()
{
	const RECT r{t_caret.pos.x, t_caret.pos.y, t_caret.pos.x + std::max(t_caret.width, 1), t_caret.pos.y + t_caret.height};
	backend::CaretChanged(t_caret.hwnd, ToB(r), t_caret.hwnd && t_caret.hideCount <= 0);
}

// ---- カーソル ----
std::atomic<HCURSOR> g_cursor{nullptr};

// ---- クリップボード（プロセス内） ----
std::mutex& ClipMutex() { static auto* m = new std::mutex; return *m; }
std::map<UINT, HANDLE>& ClipData() { static auto* m = new std::map<UINT, HANDLE>; return *m; }
std::map<std::wstring, UINT>& ClipFormats() { static auto* m = new std::map<std::wstring, UINT>; return *m; }
DWORD g_clipOwnerThread = 0;

}  // namespace

namespace winport {

//! CreateWindowExW から呼ぶ。0 以外が返ったら生成を中止する
LRESULT CallCbtHooks(int code, WPARAM wParam, LPARAM lParam)
{
	std::vector<HOOKPROC> procs;
	{
		std::lock_guard lock(HookMutex());
		const DWORD tid = GetCurrentThreadId();
		for (auto it = Hooks().rbegin(); it != Hooks().rend(); ++it) {
			if ((*it)->id == WH_CBT && (*it)->threadId == tid) procs.push_back((*it)->proc);
		}
	}
	for (auto p : procs) {
		if (const LRESULT r = p(code, wParam, lParam)) return r;
	}
	return 0;
}

}  // namespace winport

extern "C" {

/* ---- システム情報 ---- */
int WINAPI GetSystemMetrics(int nIndex)
{
	const RECT scr = ToRECT(backend::ScreenRect());
	const RECT work = ToRECT(backend::WorkArea());
	switch (nIndex) {
	case SM_CXSCREEN: case SM_CXVIRTUALSCREEN: return scr.right - scr.left;
	case SM_CYSCREEN: case SM_CYVIRTUALSCREEN: return scr.bottom - scr.top;
	case SM_XVIRTUALSCREEN: case SM_YVIRTUALSCREEN: return 0;
	case SM_CXFULLSCREEN: case SM_CXMAXIMIZED: return work.right - work.left;
	case SM_CYFULLSCREEN: case SM_CYMAXIMIZED: return work.bottom - work.top;
	case SM_CXVSCROLL: case SM_CYHSCROLL: return 15;
	case SM_CYVSCROLL: case SM_CXHSCROLL: return 15;
	case SM_CXHTHUMB: case SM_CYVTHUMB: return 15;
	case SM_CYCAPTION: return backend::TitleBarHeight();
	case SM_CYMENU: return 0;  // mac のメニューバーはウィンドウの外
	case SM_CXBORDER: case SM_CYBORDER: return 1;
	case SM_CXEDGE: case SM_CYEDGE: return 2;
	case SM_CXFRAME: case SM_CYFRAME: return 0;            // = SM_CXSIZEFRAME / SM_CYSIZEFRAME
	case SM_CXFIXEDFRAME: case SM_CYFIXEDFRAME: return 0;  // = SM_CXDLGFRAME / SM_CYDLGFRAME
	case SM_CXICON: case SM_CYICON: return 32;
	case SM_CXSMICON: case SM_CYSMICON: return 16;
	case SM_CXCURSOR: case SM_CYCURSOR: return 32;
	case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
	case SM_CXDRAG: case SM_CYDRAG: return 4;
	case SM_CXMIN: return 100;
	case SM_CYMIN: return 40;
	case SM_CXMINTRACK: return 100;
	case SM_CYMINTRACK: return 40;
	case SM_CMONITORS: return 1;
	case SM_MOUSEPRESENT: case SM_MOUSEWHEELPRESENT: return 1;
	case SM_CMOUSEBUTTONS: return 3;
	case SM_DBCSENABLED: return 1;
	case SM_IMMENABLED: return 1;
	case SM_SWAPBUTTON: case SM_REMOTESESSION: case SM_TABLETPC: return 0;
	case SM_CXMENUCHECK: case SM_CYMENUCHECK: return 13;
	default: return 0;
	}
}

DWORD WINAPI GetSysColor(int nIndex)
{
	// Windows 10 の既定値
	switch (nIndex) {
	case COLOR_SCROLLBAR: return RGB(200, 200, 200);
	case COLOR_BACKGROUND: return RGB(0, 0, 0);
	case COLOR_ACTIVECAPTION: return RGB(153, 180, 209);
	case COLOR_INACTIVECAPTION: return RGB(191, 205, 219);
	case COLOR_MENU: return RGB(240, 240, 240);
	case COLOR_WINDOW: return RGB(255, 255, 255);
	case COLOR_WINDOWFRAME: return RGB(100, 100, 100);
	case COLOR_MENUTEXT: return RGB(0, 0, 0);
	case COLOR_WINDOWTEXT: return RGB(0, 0, 0);
	case COLOR_CAPTIONTEXT: return RGB(0, 0, 0);
	case COLOR_ACTIVEBORDER: return RGB(180, 180, 180);
	case COLOR_INACTIVEBORDER: return RGB(244, 247, 252);
	case COLOR_APPWORKSPACE: return RGB(171, 171, 171);
	case COLOR_HIGHLIGHT: return RGB(0, 120, 215);
	case COLOR_HIGHLIGHTTEXT: return RGB(255, 255, 255);
	case COLOR_BTNFACE: return RGB(240, 240, 240);
	case COLOR_BTNSHADOW: return RGB(160, 160, 160);
	case COLOR_GRAYTEXT: return RGB(109, 109, 109);
	case COLOR_BTNTEXT: return RGB(0, 0, 0);
	case COLOR_INACTIVECAPTIONTEXT: return RGB(0, 0, 0);
	case COLOR_BTNHIGHLIGHT: return RGB(255, 255, 255);
	case COLOR_3DDKSHADOW: return RGB(105, 105, 105);
	case COLOR_3DLIGHT: return RGB(227, 227, 227);
	case COLOR_INFOTEXT: return RGB(0, 0, 0);
	case COLOR_INFOBK: return RGB(255, 255, 225);
	case COLOR_HOTLIGHT: return RGB(0, 102, 204);
	case COLOR_GRADIENTACTIVECAPTION: return RGB(185, 209, 234);
	case COLOR_GRADIENTINACTIVECAPTION: return RGB(215, 228, 242);
	case COLOR_MENUHILIGHT: return RGB(0, 120, 215);
	case COLOR_MENUBAR: return RGB(240, 240, 240);
	default: return 0;
	}
}

HBRUSH WINAPI GetSysColorBrush(int nIndex)
{
	static HBRUSH brushes[COLOR_MENUBAR + 1] = {};
	static std::once_flag once;
	std::call_once(once, [] {
		for (int i = 0; i <= COLOR_MENUBAR; ++i) {
			brushes[i] = CreateSolidBrush(GetSysColor(i));
			if (auto o = gdi::ObjectOf(brushes[i])) o->stock = true;
		}
	});
	return (nIndex >= 0 && nIndex <= COLOR_MENUBAR) ? brushes[nIndex] : nullptr;
}

WINBOOL WINAPI SystemParametersInfoW(UINT uiAction, UINT uiParam, PVOID pvParam, UINT)
{
	switch (uiAction) {
	case SPI_GETWORKAREA:
		*static_cast<RECT*>(pvParam) = ToRECT(backend::WorkArea());
		return TRUE;
	case SPI_GETWHEELSCROLLLINES: case SPI_GETWHEELSCROLLCHARS:
		*static_cast<UINT*>(pvParam) = 3;
		return TRUE;
	case SPI_GETNONCLIENTMETRICS: {
		auto m = static_cast<NONCLIENTMETRICSW*>(pvParam);
		const UINT size = m->cbSize ? m->cbSize : sizeof(NONCLIENTMETRICSW);
		std::memset(m, 0, size);
		m->cbSize = size;
		m->iBorderWidth = 1;
		m->iScrollWidth = m->iScrollHeight = 15;
		m->iCaptionWidth = m->iCaptionHeight = backend::TitleBarHeight();
		m->iMenuWidth = m->iMenuHeight = 19;
		LOGFONTW lf{};
		lf.lfHeight = -12;
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = SHIFTJIS_CHARSET;
		std::wcscpy(lf.lfFaceName, L"Yu Gothic UI");
		m->lfCaptionFont = m->lfSmCaptionFont = m->lfMenuFont = m->lfStatusFont = m->lfMessageFont = lf;
		return TRUE;
	}
	case SPI_GETICONTITLELOGFONT: {
		auto lf = static_cast<LOGFONTW*>(pvParam);
		*lf = {};
		lf->lfHeight = -12;
		lf->lfWeight = FW_NORMAL;
		lf->lfCharSet = SHIFTJIS_CHARSET;
		std::wcscpy(lf->lfFaceName, L"Yu Gothic UI");
		return TRUE;
	}
	case SPI_GETKEYBOARDDELAY: *static_cast<int*>(pvParam) = 1; return TRUE;
	case SPI_GETKEYBOARDSPEED: *static_cast<DWORD*>(pvParam) = 31; return TRUE;
	case SPI_GETFLATMENU: case SPI_GETHIGHCONTRAST: case SPI_GETMENUDROPALIGNMENT: case SPI_GETFONTSMOOTHING:
		if (uiAction == SPI_GETHIGHCONTRAST) { static_cast<HIGHCONTRASTW*>(pvParam)->dwFlags = 0; return TRUE; }
		*static_cast<BOOL*>(pvParam) = uiAction == SPI_GETFONTSMOOTHING;
		return TRUE;
	default:
		(void)uiParam;
		return FALSE;
	}
}

HMONITOR WINAPI MonitorFromWindow(HWND, DWORD) { return reinterpret_cast<HMONITOR>(intptr_t{1}); }
HMONITOR WINAPI MonitorFromPoint(POINT, DWORD) { return reinterpret_cast<HMONITOR>(intptr_t{1}); }
HMONITOR WINAPI MonitorFromRect(LPCRECT, DWORD) { return reinterpret_cast<HMONITOR>(intptr_t{1}); }

WINBOOL WINAPI GetMonitorInfoW(HMONITOR, LPMONITORINFO lpmi)
{
	lpmi->rcMonitor = ToRECT(backend::ScreenRect());
	lpmi->rcWork = ToRECT(backend::WorkArea());
	lpmi->dwFlags = MONITORINFOF_PRIMARY;
	if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) std::wcscpy(reinterpret_cast<MONITORINFOEXW*>(lpmi)->szDevice, L"\\\\.\\DISPLAY1");
	return TRUE;
}

WINBOOL WINAPI EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC lpfnEnum, LPARAM dwData)
{
	RECT r = ToRECT(backend::ScreenRect());
	return lpfnEnum(reinterpret_cast<HMONITOR>(intptr_t{1}), nullptr, &r, dwData);
}

UINT WINAPI GetDoubleClickTime(void) { return 500; }
UINT WINAPI GetCaretBlinkTime(void) { return 530; }
WINBOOL WINAPI SetCaretBlinkTime(UINT) { return TRUE; }
WINBOOL WINAPI MessageBeep(UINT) { backend::Beep(); return TRUE; }
DWORD WINAPI GetGuiResources(HANDLE, DWORD) { return 0; }

/* ---- 矩形 ---- */
WINBOOL WINAPI SetRect(LPRECT lprc, int xLeft, int yTop, int xRight, int yBottom) { *lprc = {xLeft, yTop, xRight, yBottom}; return TRUE; }
WINBOOL WINAPI SetRectEmpty(LPRECT lprc) { *lprc = {}; return TRUE; }
WINBOOL WINAPI CopyRect(LPRECT lprcDst, const RECT* lprcSrc) { *lprcDst = *lprcSrc; return TRUE; }
WINBOOL WINAPI IsRectEmpty(const RECT* lprc) { return lprc->right <= lprc->left || lprc->bottom <= lprc->top; }
WINBOOL WINAPI EqualRect(const RECT* a, const RECT* b)
{
	return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}
WINBOOL WINAPI PtInRect(const RECT* lprc, POINT pt) { return pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom; }
WINBOOL WINAPI InflateRect(LPRECT lprc, int dx, int dy) { lprc->left -= dx; lprc->right += dx; lprc->top -= dy; lprc->bottom += dy; return TRUE; }
WINBOOL WINAPI OffsetRect(LPRECT lprc, int dx, int dy) { lprc->left += dx; lprc->right += dx; lprc->top += dy; lprc->bottom += dy; return TRUE; }

WINBOOL WINAPI IntersectRect(LPRECT dst, const RECT* a, const RECT* b)
{
	RECT r{std::max(a->left, b->left), std::max(a->top, b->top), std::min(a->right, b->right), std::min(a->bottom, b->bottom)};
	if (r.right <= r.left || r.bottom <= r.top) { *dst = {}; return FALSE; }
	*dst = r;
	return TRUE;
}

WINBOOL WINAPI UnionRect(LPRECT dst, const RECT* a, const RECT* b)
{
	if (IsRectEmpty(a)) { *dst = *b; return !IsRectEmpty(b); }
	if (IsRectEmpty(b)) { *dst = *a; return TRUE; }
	*dst = {std::min(a->left, b->left), std::min(a->top, b->top), std::max(a->right, b->right), std::max(a->bottom, b->bottom)};
	return TRUE;
}

WINBOOL WINAPI SubtractRect(LPRECT dst, const RECT* a, const RECT* b)
{
	RECT i;
	*dst = *a;
	if (!IntersectRect(&i, a, b)) return !IsRectEmpty(a);
	if (i.top == a->top && i.bottom == a->bottom) {
		if (i.left == a->left) dst->left = i.right;
		else if (i.right == a->right) dst->right = i.left;
	} else if (i.left == a->left && i.right == a->right) {
		if (i.top == a->top) dst->top = i.bottom;
		else if (i.bottom == a->bottom) dst->bottom = i.top;
	}
	return !IsRectEmpty(dst);
}

int WINAPI MapDialogRect(HWND, LPRECT lpRect)
{
	// ダイアログ単位: 横 1/4 平均文字幅、縦 1/8 文字高さ。9pt 相当（6x13）で近似
	lpRect->left = lpRect->left * 6 / 4;
	lpRect->right = lpRect->right * 6 / 4;
	lpRect->top = lpRect->top * 13 / 8;
	lpRect->bottom = lpRect->bottom * 13 / 8;
	return TRUE;
}

/* ---- キャレット ---- */
WINBOOL WINAPI CreateCaret(HWND hWnd, HBITMAP, int nWidth, int nHeight)
{
	t_caret.hwnd = hWnd;
	t_caret.width = nWidth ? nWidth : 1;
	t_caret.height = nHeight ? nHeight : 16;
	t_caret.hideCount = 1;
	UpdateCaret();
	return TRUE;
}

WINBOOL WINAPI DestroyCaret(void)
{
	t_caret = Caret{};
	backend::CaretChanged(nullptr, backend::Rect{}, false);
	return TRUE;
}

WINBOOL WINAPI SetCaretPos(int X, int Y) { t_caret.pos = {X, Y}; UpdateCaret(); return TRUE; }
WINBOOL WINAPI GetCaretPos(LPPOINT lpPoint) { *lpPoint = t_caret.pos; return TRUE; }

WINBOOL WINAPI ShowCaret(HWND hWnd)
{
	if (hWnd && hWnd != t_caret.hwnd) return FALSE;
	if (t_caret.hideCount > 0) --t_caret.hideCount;
	UpdateCaret();
	return TRUE;
}

WINBOOL WINAPI HideCaret(HWND hWnd)
{
	if (hWnd && hWnd != t_caret.hwnd) return FALSE;
	++t_caret.hideCount;
	UpdateCaret();
	return TRUE;
}

/* ---- カーソル・アイコン・画像 ---- */
HCURSOR WINAPI LoadCursorW(HINSTANCE, LPCWSTR lpCursorName) { return reinterpret_cast<HCURSOR>(const_cast<LPWSTR>(lpCursorName)); }
HICON WINAPI LoadIconW(HINSTANCE, LPCWSTR lpIconName) { return reinterpret_cast<HICON>(const_cast<LPWSTR>(lpIconName)); }
WINBOOL WINAPI DestroyIcon(HICON) { return TRUE; }
WINBOOL WINAPI DestroyCursor(HCURSOR) { return TRUE; }

HANDLE WINAPI LoadImageW(HINSTANCE, LPCWSTR name, UINT type, int, int, UINT)
{
	if (type == IMAGE_CURSOR || type == IMAGE_ICON) return const_cast<LPWSTR>(name);
	// TODO(winport): IMAGE_BITMAP（ツールバーのアイコン等）。リソース変換に BITMAP を足してから
	ReportUnimplemented("user32", "LoadImageW(IMAGE_BITMAP)");
	return nullptr;
}

HCURSOR WINAPI SetCursor(HCURSOR hCursor)
{
	const HCURSOR old = g_cursor.exchange(hCursor);
	if (old != hCursor) backend::SetCursorShape(reinterpret_cast<uintptr_t>(hCursor));
	return old;
}

HCURSOR WINAPI GetCursor(void) { return g_cursor.load(); }
WINBOOL WINAPI GetCursorPos(LPPOINT lpPoint)
{
	int x, y;
	backend::CursorPos(&x, &y);
	*lpPoint = {x, y};
	return TRUE;
}

WINBOOL WINAPI GetPhysicalCursorPos(LPPOINT lpPoint) { return GetCursorPos(lpPoint); }

int WINAPI ShowCursor(WINBOOL bShow) { return bShow ? 0 : -1; }
WINBOOL WINAPI SetCursorPos(int, int) { return TRUE; }
WINBOOL WINAPI ClipCursor(const RECT*) { return TRUE; }

/* ---- スクロールバー ---- */
int WINAPI SetScrollInfo(HWND hwnd, int nBar, LPCSCROLLINFO lpsi, WINBOOL)
{
	auto w = GetWindowObject(hwnd);
	if (!w || (nBar != SB_HORZ && nBar != SB_VERT)) return 0;
	bool visibilityChanged = false;
	int pos;
	{
		std::lock_guard lock(UserMutex());
		auto& si = w->scroll[nBar];
		if (lpsi->fMask & SIF_RANGE) { si.nMin = lpsi->nMin; si.nMax = lpsi->nMax; }
		if (lpsi->fMask & SIF_PAGE) si.nPage = lpsi->nPage;
		if (lpsi->fMask & SIF_POS) si.nPos = lpsi->nPos;
		if (lpsi->fMask & SIF_TRACKPOS) si.nTrackPos = lpsi->nTrackPos;
		const int maxPos = std::max(si.nMin, si.nMax - std::max<int>(static_cast<int>(si.nPage) - 1, 0));
		si.nPos = std::clamp(si.nPos, si.nMin, maxPos);
		pos = si.nPos;
		const DWORD styleBit = nBar == SB_VERT ? WS_VSCROLL : WS_HSCROLL;
		const bool needed = (si.nMax > si.nMin) && (si.nPage == 0 || static_cast<int>(si.nPage) <= si.nMax - si.nMin);
		const bool show = (w->style & styleBit) && (needed || (lpsi->fMask & SIF_DISABLENOSCROLL));
		if (show != w->scrollVisible[nBar]) { w->scrollVisible[nBar] = show; visibilityChanged = true; }
	}
	if (visibilityChanged) SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	backend::ScrollBarsChanged(hwnd);
	return pos;
}

WINBOOL WINAPI GetScrollInfo(HWND hwnd, int nBar, LPSCROLLINFO lpsi)
{
	auto w = GetWindowObject(hwnd);
	if (!w || (nBar != SB_HORZ && nBar != SB_VERT)) return FALSE;
	std::lock_guard lock(UserMutex());
	const auto& si = w->scroll[nBar];
	if (lpsi->fMask & SIF_RANGE) { lpsi->nMin = si.nMin; lpsi->nMax = si.nMax; }
	if (lpsi->fMask & SIF_PAGE) lpsi->nPage = si.nPage;
	if (lpsi->fMask & SIF_POS) lpsi->nPos = si.nPos;
	if (lpsi->fMask & SIF_TRACKPOS) lpsi->nTrackPos = si.nTrackPos;
	return TRUE;
}

int WINAPI SetScrollPos(HWND hWnd, int nBar, int nPos, WINBOOL bRedraw)
{
	SCROLLINFO si{sizeof(si), SIF_POS};
	GetScrollInfo(hWnd, nBar, &si);
	const int old = si.nPos;
	si.nPos = nPos;
	SetScrollInfo(hWnd, nBar, &si, bRedraw);
	return old;
}

int WINAPI GetScrollPos(HWND hWnd, int nBar)
{
	SCROLLINFO si{sizeof(si), SIF_POS};
	GetScrollInfo(hWnd, nBar, &si);
	return si.nPos;
}

WINBOOL WINAPI SetScrollRange(HWND hWnd, int nBar, int nMinPos, int nMaxPos, WINBOOL bRedraw)
{
	SCROLLINFO si{sizeof(si), SIF_RANGE, nMinPos, nMaxPos};
	SetScrollInfo(hWnd, nBar, &si, bRedraw);
	return TRUE;
}

WINBOOL WINAPI GetScrollRange(HWND hWnd, int nBar, LPINT lpMinPos, LPINT lpMaxPos)
{
	SCROLLINFO si{sizeof(si), SIF_RANGE};
	GetScrollInfo(hWnd, nBar, &si);
	*lpMinPos = si.nMin;
	*lpMaxPos = si.nMax;
	return TRUE;
}

WINBOOL WINAPI ShowScrollBar(HWND hWnd, int wBar, WINBOOL bShow)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	{
		std::lock_guard lock(UserMutex());
		if (wBar == SB_HORZ || wBar == SB_BOTH) w->scrollVisible[SB_HORZ] = bShow && (w->style & WS_HSCROLL);
		if (wBar == SB_VERT || wBar == SB_BOTH) w->scrollVisible[SB_VERT] = bShow && (w->style & WS_VSCROLL);
	}
	SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	backend::ScrollBarsChanged(hWnd);
	return TRUE;
}

WINBOOL WINAPI EnableScrollBar(HWND, UINT, UINT) { return TRUE; }

/* ---- メニュー（データのみ） ---- */
HMENU WINAPI CreateMenu(void) { return reinterpret_cast<HMENU>(new Menu); }
HMENU WINAPI CreatePopupMenu(void) { auto m = new Menu; m->popup = true; return reinterpret_cast<HMENU>(m); }

WINBOOL WINAPI DestroyMenu(HMENU hMenu)
{
	auto m = MenuOf(hMenu);
	if (!m) return FALSE;
	for (auto& it : m->items) if (it.sub) DestroyMenu(it.sub);
	m->magic = 0;
	delete m;
	return TRUE;
}

WINBOOL WINAPI IsMenu(HMENU hMenu) { return MenuOf(hMenu) != nullptr; }

WINBOOL WINAPI AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return FALSE;
	m->items.push_back(MakeItem(uFlags, uIDNewItem, lpNewItem));
	return TRUE;
}

WINBOOL WINAPI InsertMenuW(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return FALSE;
	int idx = FindItem(m, uPosition, uFlags);
	if (idx < 0) idx = static_cast<int>(m->items.size());
	m->items.insert(m->items.begin() + idx, MakeItem(uFlags, uIDNewItem, lpNewItem));
	return TRUE;
}

WINBOOL WINAPI InsertMenuItemW(HMENU hmenu, UINT item, WINBOOL fByPosition, LPCMENUITEMINFOW lpmi)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hmenu);
	if (!m) return FALSE;
	MenuItem it;
	if (lpmi->fMask & MIIM_FTYPE) it.type = lpmi->fType;
	if (lpmi->fMask & MIIM_TYPE) it.type = lpmi->fType;
	if (lpmi->fMask & MIIM_STATE) it.state = lpmi->fState;
	if (lpmi->fMask & MIIM_ID) it.id = lpmi->wID;
	if (lpmi->fMask & MIIM_SUBMENU) it.sub = lpmi->hSubMenu;
	if (lpmi->fMask & MIIM_DATA) it.data = lpmi->dwItemData;
	if (((lpmi->fMask & MIIM_STRING) || ((lpmi->fMask & MIIM_TYPE) && !(it.type & (MFT_SEPARATOR | MFT_BITMAP | MFT_OWNERDRAW)))) && lpmi->dwTypeData) {
		it.text = lpmi->dwTypeData;
	}
	int idx = fByPosition ? static_cast<int>(item) : FindItem(m, item, MF_BYCOMMAND);
	if (idx < 0 || idx > static_cast<int>(m->items.size())) idx = static_cast<int>(m->items.size());
	m->items.insert(m->items.begin() + idx, it);
	return TRUE;
}

WINBOOL WINAPI DeleteMenu(HMENU hMenu, UINT uPosition, UINT uFlags)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return FALSE;
	const int idx = FindItem(m, uPosition, uFlags);
	if (idx < 0) return FALSE;
	if (m->items[idx].sub) DestroyMenu(m->items[idx].sub);
	m->items.erase(m->items.begin() + idx);
	return TRUE;
}

WINBOOL WINAPI RemoveMenu(HMENU hMenu, UINT uPosition, UINT uFlags)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return FALSE;
	const int idx = FindItem(m, uPosition, uFlags);
	if (idx < 0) return FALSE;
	m->items.erase(m->items.begin() + idx);
	return TRUE;
}

int WINAPI GetMenuItemCount(HMENU hMenu) { auto m = MenuOf(hMenu); return m ? static_cast<int>(m->items.size()) : -1; }

UINT WINAPI GetMenuItemID(HMENU hMenu, int nPos)
{
	auto m = MenuOf(hMenu);
	if (!m || nPos < 0 || nPos >= static_cast<int>(m->items.size())) return static_cast<UINT>(-1);
	return m->items[nPos].sub ? static_cast<UINT>(-1) : m->items[nPos].id;
}

HMENU WINAPI GetSubMenu(HMENU hMenu, int nPos)
{
	auto m = MenuOf(hMenu);
	if (!m || nPos < 0 || nPos >= static_cast<int>(m->items.size())) return nullptr;
	return m->items[nPos].sub;
}

WINBOOL WINAPI GetMenuItemInfoW(HMENU hmenu, UINT item, WINBOOL fByPosition, LPMENUITEMINFOW lpmii)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hmenu);
	if (!m) return FALSE;
	auto it = FindItemDeep(m, item, fByPosition ? MF_BYPOSITION : MF_BYCOMMAND);
	if (!it) return FALSE;
	if (lpmii->fMask & (MIIM_FTYPE | MIIM_TYPE)) lpmii->fType = it->type;
	if (lpmii->fMask & MIIM_STATE) lpmii->fState = it->state;
	if (lpmii->fMask & MIIM_ID) lpmii->wID = it->id;
	if (lpmii->fMask & MIIM_SUBMENU) lpmii->hSubMenu = it->sub;
	if (lpmii->fMask & MIIM_DATA) lpmii->dwItemData = it->data;
	if (lpmii->fMask & (MIIM_STRING | MIIM_TYPE)) {
		if (lpmii->dwTypeData && lpmii->cch) wcsncpy_s(lpmii->dwTypeData, lpmii->cch, it->text.c_str(), _TRUNCATE);
		lpmii->cch = static_cast<UINT>(it->text.size());
	}
	return TRUE;
}

WINBOOL WINAPI SetMenuItemInfoW(HMENU hmenu, UINT item, WINBOOL fByPositon, LPCMENUITEMINFOW lpmii)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hmenu);
	if (!m) return FALSE;
	auto it = FindItemDeep(m, item, fByPositon ? MF_BYPOSITION : MF_BYCOMMAND);
	if (!it) return FALSE;
	if (lpmii->fMask & (MIIM_FTYPE | MIIM_TYPE)) it->type = lpmii->fType;
	if (lpmii->fMask & MIIM_STATE) it->state = lpmii->fState;
	if (lpmii->fMask & MIIM_ID) it->id = lpmii->wID;
	if (lpmii->fMask & MIIM_SUBMENU) it->sub = lpmii->hSubMenu;
	if (lpmii->fMask & MIIM_DATA) it->data = lpmii->dwItemData;
	if ((lpmii->fMask & (MIIM_STRING | MIIM_TYPE)) && lpmii->dwTypeData) it->text = lpmii->dwTypeData;
	return TRUE;
}

int WINAPI GetMenuStringW(HMENU hMenu, UINT uIDItem, LPWSTR lpString, int cchMax, UINT flags)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return 0;
	auto it = FindItemDeep(m, uIDItem, flags);
	if (!it) return 0;
	if (lpString && cchMax > 0) wcsncpy_s(lpString, cchMax, it->text.c_str(), _TRUNCATE);
	return static_cast<int>(it->text.size());
}

UINT WINAPI GetMenuState(HMENU hMenu, UINT uId, UINT uFlags)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return static_cast<UINT>(-1);
	auto it = FindItemDeep(m, uId, uFlags);
	return it ? it->state : static_cast<UINT>(-1);
}

DWORD WINAPI CheckMenuItem(HMENU hMenu, UINT uIDCheckItem, UINT uCheck)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return static_cast<DWORD>(-1);
	auto it = FindItemDeep(m, uIDCheckItem, uCheck & MF_BYPOSITION);
	if (!it) return static_cast<DWORD>(-1);
	const DWORD old = (it->state & MFS_CHECKED) ? MF_CHECKED : MF_UNCHECKED;
	if (uCheck & MF_CHECKED) it->state |= MFS_CHECKED;
	else it->state &= ~MFS_CHECKED;
	return old;
}

WINBOOL WINAPI CheckMenuRadioItem(HMENU hmenu, UINT first, UINT last, UINT check, UINT flags)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hmenu);
	if (!m) return FALSE;
	for (UINT i = first; i <= last; ++i) {
		if (auto it = FindItemDeep(m, i, flags)) {
			if (i == check) it->state |= MFS_CHECKED;
			else it->state &= ~MFS_CHECKED;
			it->type |= MFT_RADIOCHECK;
		}
	}
	return TRUE;
}

WINBOOL WINAPI EnableMenuItem(HMENU hMenu, UINT uIDEnableItem, UINT uEnable)
{
	std::lock_guard lock(MenuMutex());
	auto m = MenuOf(hMenu);
	if (!m) return -1;
	auto it = FindItemDeep(m, uIDEnableItem, uEnable & MF_BYPOSITION);
	if (!it) return -1;
	const WINBOOL old = (it->state & MFS_DISABLED) ? MF_GRAYED : MF_ENABLED;
	if (uEnable & (MF_GRAYED | MF_DISABLED)) it->state |= MFS_DISABLED;
	else it->state &= ~MFS_DISABLED;
	return old;
}

WINBOOL WINAPI SetMenuInfo(HMENU hMenu, LPCMENUINFO lpcmi)
{
	auto m = MenuOf(hMenu);
	if (!m) return FALSE;
	m->info = *lpcmi;
	return TRUE;
}

WINBOOL WINAPI GetMenuInfo(HMENU hMenu, LPMENUINFO lpcmi)
{
	auto m = MenuOf(hMenu);
	if (!m) return FALSE;
	*lpcmi = m->info;
	return TRUE;
}

WINBOOL WINAPI SetMenu(HWND hWnd, HMENU hMenu)
{
	auto w = GetWindowObject(hWnd);
	if (!w) return FALSE;
	w->menu = hMenu;
	// TODO(winport): mac のメニューバーへ反映する（Week 5）
	return TRUE;
}

HMENU WINAPI GetMenu(HWND hWnd)
{
	auto w = GetWindowObject(hWnd);
	return w && !w->parent ? w->menu : nullptr;
}

WINBOOL WINAPI DrawMenuBar(HWND) { return TRUE; }
HMENU WINAPI GetSystemMenu(HWND, WINBOOL) { return nullptr; }

WINBOOL WINAPI TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT*)
{
	// TODO(winport): NSMenu でポップアップ表示する（Week 5）
	ReportUnimplemented("user32", "TrackPopupMenu");
	return 0;
}

WINBOOL WINAPI TrackPopupMenuEx(HMENU hMenu, UINT uFlags, int x, int y, HWND hwnd, LPTPMPARAMS)
{
	return TrackPopupMenu(hMenu, uFlags, x, y, 0, hwnd, nullptr);
}

/* ---- アクセラレータ ---- */
HACCEL WINAPI CreateAcceleratorTableW(LPACCEL paccel, int cAccel)
{
	auto t = new AccelTable;
	t->entries.assign(paccel, paccel + cAccel);
	return reinterpret_cast<HACCEL>(t);
}

WINBOOL WINAPI DestroyAcceleratorTable(HACCEL hAccel)
{
	auto t = reinterpret_cast<AccelTable*>(hAccel);
	if (!t || t->magic != 0x41434345) return FALSE;
	t->magic = 0;
	delete t;
	return TRUE;
}

int WINAPI CopyAcceleratorTableW(HACCEL hAccelSrc, LPACCEL lpAccelDst, int cAccelEntries)
{
	auto t = reinterpret_cast<AccelTable*>(hAccelSrc);
	if (!t || t->magic != 0x41434345) return 0;
	if (!lpAccelDst) return static_cast<int>(t->entries.size());
	const int n = std::min<int>(cAccelEntries, static_cast<int>(t->entries.size()));
	std::copy(t->entries.begin(), t->entries.begin() + n, lpAccelDst);
	return n;
}

int WINAPI TranslateAcceleratorW(HWND hWnd, HACCEL hAccTable, LPMSG lpMsg)
{
	auto t = reinterpret_cast<AccelTable*>(hAccTable);
	if (!t || t->magic != 0x41434345 || !hWnd) return 0;
	if (lpMsg->message != WM_KEYDOWN && lpMsg->message != WM_SYSKEYDOWN && lpMsg->message != WM_CHAR && lpMsg->message != WM_SYSCHAR) return 0;
	const bool ctrl = IsKeyDown(VK_CONTROL), shift = IsKeyDown(VK_SHIFT), alt = IsKeyDown(VK_MENU);
	for (const auto& a : t->entries) {
		if (a.fVirt & FVIRTKEY) {
			if (lpMsg->message != WM_KEYDOWN && lpMsg->message != WM_SYSKEYDOWN) continue;
			if (a.key != lpMsg->wParam) continue;
			if (((a.fVirt & FCONTROL) != 0) != ctrl || ((a.fVirt & FSHIFT) != 0) != shift || ((a.fVirt & FALT) != 0) != alt) continue;
		} else {
			if (lpMsg->message != WM_CHAR && lpMsg->message != WM_SYSCHAR) continue;
			if (a.key != lpMsg->wParam) continue;
		}
		if (std::getenv("WINPORT_TRACE")) std::fprintf(stderr, "accel vk=%02x fVirt=%02x cmd=%04x\n", a.key, a.fVirt, a.cmd);
		SendMessageW(hWnd, (lpMsg->message == WM_SYSKEYDOWN || lpMsg->message == WM_SYSCHAR) ? WM_SYSCOMMAND : WM_COMMAND, MAKEWPARAM(a.cmd, 1), 0);
		// WM_SYSCOMMAND は sakura では使わないので WM_COMMAND と同じ扱いでよい
		return 1;
	}
	return 0;
}

/* ---- フック（WH_CBT の HCBT_CREATEWND のみ。sakura の CWnd はこれでウィンドウとオブジェクトを結び付ける） ---- */
HHOOK WINAPI SetWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE, DWORD dwThreadId)
{
	auto h = new Hook{idHook, lpfn, dwThreadId ? dwThreadId : GetCurrentThreadId()};
	std::lock_guard lock(HookMutex());
	Hooks().push_back(h);
	return reinterpret_cast<HHOOK>(h);
}

WINBOOL WINAPI UnhookWindowsHookEx(HHOOK hhk)
{
	std::lock_guard lock(HookMutex());
	auto& v = Hooks();
	auto it = std::find(v.begin(), v.end(), reinterpret_cast<Hook*>(hhk));
	if (it == v.end()) return FALSE;
	delete *it;
	v.erase(it);
	return TRUE;
}

// フックは CallCbtHooks が順に呼ぶので、次のフックへの受け渡しはしない
LRESULT WINAPI CallNextHookEx(HHOOK, int, WPARAM, LPARAM) { return 0; }
WINBOOL WINAPI RegisterHotKey(HWND, int, UINT, UINT) { return TRUE; }
WINBOOL WINAPI UnregisterHotKey(HWND, int) { return TRUE; }

/* ---- クリップボード（プロセス内。HANDLE は GlobalAlloc のもの） ---- */
WINBOOL WINAPI OpenClipboard(HWND) { ClipMutex().lock(); g_clipOwnerThread = GetCurrentThreadId(); return TRUE; }
WINBOOL WINAPI CloseClipboard(void) { g_clipOwnerThread = 0; ClipMutex().unlock(); return TRUE; }

WINBOOL WINAPI EmptyClipboard(void)
{
	for (auto& [fmt, h] : ClipData()) GlobalFree(h);
	ClipData().clear();
	return TRUE;
}

HANDLE WINAPI SetClipboardData(UINT uFormat, HANDLE hMem)
{
	auto& d = ClipData();
	if (auto it = d.find(uFormat); it != d.end()) GlobalFree(it->second);
	d[uFormat] = hMem;
	return hMem;
}

HANDLE WINAPI GetClipboardData(UINT uFormat)
{
	auto& d = ClipData();
	auto it = d.find(uFormat);
	return it == d.end() ? nullptr : it->second;
}

WINBOOL WINAPI IsClipboardFormatAvailable(UINT format)
{
	std::lock_guard lock(ClipMutex());
	return ClipData().count(format) != 0;
}

UINT WINAPI EnumClipboardFormats(UINT format)
{
	auto& d = ClipData();
	auto it = format ? d.upper_bound(format) : d.begin();
	return it == d.end() ? 0 : it->first;
}

UINT WINAPI RegisterClipboardFormatW(LPCWSTR lpszFormat)
{
	static UINT next = 0xC000;
	auto& f = ClipFormats();
	auto [it, inserted] = f.try_emplace(lpszFormat ? lpszFormat : L"", next);
	if (inserted) ++next;
	return it->second;
}

int WINAPI GetClipboardFormatNameW(UINT format, LPWSTR lpszFormatName, int cchMaxCount)
{
	for (auto& [name, id] : ClipFormats()) {
		if (id == format) { wcsncpy_s(lpszFormatName, cchMaxCount, name.c_str(), _TRUNCATE); return static_cast<int>(name.size()); }
	}
	return 0;
}

int WINAPI CountClipboardFormats(void) { return static_cast<int>(ClipData().size()); }

/* ---- 文字関数 ---- */
LPWSTR WINAPI CharNextW(LPCWSTR lpsz)
{
	if (!*lpsz) return const_cast<LPWSTR>(lpsz);
	if ((lpsz[0] & 0xFC00) == 0xD800 && (lpsz[1] & 0xFC00) == 0xDC00) return const_cast<LPWSTR>(lpsz + 2);
	return const_cast<LPWSTR>(lpsz + 1);
}

LPWSTR WINAPI CharPrevW(LPCWSTR lpszStart, LPCWSTR lpszCurrent)
{
	if (lpszCurrent <= lpszStart) return const_cast<LPWSTR>(lpszStart);
	const LPCWSTR p = lpszCurrent - 1;
	if (p > lpszStart && (p[0] & 0xFC00) == 0xDC00 && (p[-1] & 0xFC00) == 0xD800) return const_cast<LPWSTR>(p - 1);
	return const_cast<LPWSTR>(p);
}

LPWSTR WINAPI CharLowerW(LPWSTR lpsz)
{
	if (IS_INTRESOURCE(lpsz)) return reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(std::towlower(static_cast<wint_t>(reinterpret_cast<ULONG_PTR>(lpsz)))));
	for (auto p = lpsz; *p; ++p) *p = static_cast<wchar_t>(std::towlower(*p));
	return lpsz;
}

LPWSTR WINAPI CharUpperW(LPWSTR lpsz)
{
	if (IS_INTRESOURCE(lpsz)) return reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(std::towupper(static_cast<wint_t>(reinterpret_cast<ULONG_PTR>(lpsz)))));
	for (auto p = lpsz; *p; ++p) *p = static_cast<wchar_t>(std::towupper(*p));
	return lpsz;
}

WINBOOL WINAPI IsCharAlphaW(WCHAR ch) { return std::iswalpha(ch) != 0; }
WINBOOL WINAPI IsCharAlphaNumericW(WCHAR ch) { return std::iswalnum(ch) != 0; }
WINBOOL WINAPI IsCharUpperW(WCHAR ch) { return std::iswupper(ch) != 0; }
WINBOOL WINAPI IsCharLowerW(WCHAR ch) { return std::iswlower(ch) != 0; }

UINT WINAPI MapVirtualKeyW(UINT uCode, UINT uMapType)
{
	if (uMapType == MAPVK_VK_TO_CHAR) {
		if ((uCode >= '0' && uCode <= '9') || (uCode >= 'A' && uCode <= 'Z')) return uCode;
		return 0;
	}
	return uCode;
}

}  // extern "C"
