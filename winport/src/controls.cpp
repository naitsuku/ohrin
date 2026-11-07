/*
	controls.cpp — 組み込みコントロール（SCROLLBAR / ステータスバー / ツールバー）とイメージリスト

	Win32 のウィンドウプロシージャとして実装し、GDI で描く（所有スレッドで動くので Cocoa を介さない）。
	見た目は mac に寄せるが、メッセージの意味は Win32 と同じにする。未対応のクラスは DefWindowProcW のまま。

	SPDX-License-Identifier: Zlib
*/
#include "gdi_internal.h"

#include <algorithm>
#include <cstring>
#include <cwchar>

#include "dialog_internal.h"
#include "winport_internal.h"

using namespace winport;

namespace {

template <typename T>
T* StateOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? static_cast<T*>(w->control) : nullptr;
}

void FillSolid(HDC hdc, const RECT& r, COLORREF c)
{
	HBRUSH b = CreateSolidBrush(c);
	FillRect(hdc, &r, b);
	DeleteObject(b);
}

// ---- SCROLLBAR ----
struct ScrollBar {
	bool dragging = false;
	int grabOffset = 0;  //!< つまみの先頭からつかんだ位置まで
};

struct ScrollGeometry {
	bool vertical;
	int length;
	int thumbPos, thumbLen;
	bool hasThumb;
	int minPos, maxPos;  //!< nPos の範囲
};

ScrollGeometry GeometryOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	ScrollGeometry g{};
	std::lock_guard lock(UserMutex());
	const SIZE cs = ClientSize(w);
	const auto& si = w->scroll[SB_CTL];
	g.vertical = (w->style & SBS_VERT) != 0;
	g.length = g.vertical ? cs.cy : cs.cx;
	const int range = si.nMax - si.nMin + 1;
	const int page = std::max<int>(static_cast<int>(si.nPage), 0);
	g.minPos = si.nMin;
	g.maxPos = std::max(si.nMin, si.nMax - std::max(page - 1, 0));
	g.hasThumb = range > 0 && page < range && g.length > 0 && !(w->style & (SBS_SIZEBOX | SBS_SIZEGRIP));
	if (g.hasThumb) {
		g.thumbLen = std::clamp(static_cast<int>(static_cast<long long>(g.length) * page / range), std::min(24, g.length), g.length);
		const int span = g.maxPos - g.minPos;
		g.thumbPos = span > 0 ? static_cast<int>(static_cast<long long>(g.length - g.thumbLen) * (si.nPos - si.nMin) / span) : 0;
	}
	return g;
}

void NotifyScroll(HWND hwnd, const ScrollGeometry& g, int code, int pos)
{
	SendMessageW(GetParent(hwnd), g.vertical ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(code, static_cast<WORD>(pos)), reinterpret_cast<LPARAM>(hwnd));
}

LRESULT CALLBACK ScrollBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<ScrollBar>(hwnd);
	switch (msg) {
	case WM_NCCREATE:
		GetWindowObject(hwnd)->control = new ScrollBar;
		break;
	case WM_NCDESTROY:
		delete s;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillSolid(hdc, rc, RGB(246, 246, 246));
		const auto g = GeometryOf(hwnd);
		if (g.hasThumb) {
			RECT t = rc;
			if (g.vertical) { t.top = g.thumbPos; t.bottom = g.thumbPos + g.thumbLen; InflateRect(&t, -3, -1); }
			else { t.left = g.thumbPos; t.right = g.thumbPos + g.thumbLen; InflateRect(&t, -1, -3); }
			HBRUSH b = CreateSolidBrush(s && s->dragging ? RGB(130, 130, 130) : RGB(170, 170, 170));
			HGDIOBJ oldB = SelectObject(hdc, b), oldP = SelectObject(hdc, GetStockObject(NULL_PEN));
			RoundRect(hdc, t.left, t.top, t.right, t.bottom, 8, 8);
			SelectObject(hdc, oldB);
			SelectObject(hdc, oldP);
			DeleteObject(b);
		}
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
		const auto g = GeometryOf(hwnd);
		if (!g.hasThumb || !s) return 0;
		const int p = g.vertical ? static_cast<short>(HIWORD(lParam)) : static_cast<short>(LOWORD(lParam));
		if (p < g.thumbPos) NotifyScroll(hwnd, g, SB_PAGEUP, 0);
		else if (p >= g.thumbPos + g.thumbLen) NotifyScroll(hwnd, g, SB_PAGEDOWN, 0);
		else {
			s->dragging = true;
			s->grabOffset = p - g.thumbPos;
			SetCapture(hwnd);
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;
	}
	case WM_MOUSEMOVE:
		if (s && s->dragging) {
			const auto g = GeometryOf(hwnd);
			const int p = (g.vertical ? static_cast<short>(HIWORD(lParam)) : static_cast<short>(LOWORD(lParam))) - s->grabOffset;
			const int travel = std::max(g.length - g.thumbLen, 1);
			const int pos = g.minPos + static_cast<int>(static_cast<long long>(g.maxPos - g.minPos) * std::clamp(p, 0, travel) / travel);
			{
				std::lock_guard lock(UserMutex());
				GetWindowObject(hwnd)->scroll[SB_CTL].nTrackPos = pos;
			}
			NotifyScroll(hwnd, g, SB_THUMBTRACK, pos);
		}
		return 0;
	case WM_LBUTTONUP:
		if (s && s->dragging) {
			s->dragging = false;
			ReleaseCapture();
			const auto g = GeometryOf(hwnd);
			int pos;
			{
				std::lock_guard lock(UserMutex());
				pos = GetWindowObject(hwnd)->scroll[SB_CTL].nTrackPos;
			}
			NotifyScroll(hwnd, g, SB_THUMBPOSITION, pos);
			NotifyScroll(hwnd, g, SB_ENDSCROLL, 0);
			InvalidateRect(hwnd, nullptr, FALSE);
		}
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- ステータスバー ----
constexpr int kStatusHeight = 22;

struct StatusPart {
	int right = -1;
	std::wstring text;
	UINT type = 0;
	LPARAM data = 0;
};

struct StatusBar {
	std::vector<StatusPart> parts{StatusPart{}};
	HFONT font = nullptr;
};

RECT StatusPartRect(HWND hwnd, const StatusBar* s, size_t i)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	if (i > 0) rc.left = s->parts[i - 1].right;
	if (s->parts[i].right >= 0) rc.right = s->parts[i].right;
	return rc;
}

//! Win32 のステータスバーと同じく、親の下端いっぱいに自分を合わせる
void StatusAlign(HWND hwnd)
{
	RECT p, r;
	GetClientRect(GetParent(hwnd), &p);
	auto w = GetWindowObject(hwnd);
	r = RECT{0, p.bottom - kStatusHeight, p.right, p.bottom};
	if (!EqualRect(&r, &w->rect)) MoveWindow(hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}

LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<StatusBar>(hwnd);
	switch (msg) {
	case WM_NCCREATE:
		GetWindowObject(hwnd)->control = new StatusBar;
		break;
	case WM_NCDESTROY:
		delete s;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	case WM_SIZE:
		StatusAlign(hwnd);
		return 0;
	case WM_SHOWWINDOW:
		if (wParam) StatusAlign(hwnd);
		break;
	case WM_SETFONT:
		s->font = reinterpret_cast<HFONT>(wParam);
		InvalidateRect(hwnd, nullptr, TRUE);
		return 0;
	case WM_GETFONT:
		return reinterpret_cast<LRESULT>(s->font);
	case SB_SETPARTS: {
		const int n = std::clamp(static_cast<int>(wParam), 1, 256);
		s->parts.resize(n);
		for (int i = 0; i < n; ++i) s->parts[i].right = reinterpret_cast<const int*>(lParam)[i];
		InvalidateRect(hwnd, nullptr, TRUE);
		return TRUE;
	}
	case SB_GETPARTS: {
		if (lParam) for (size_t i = 0; i < s->parts.size() && i < wParam; ++i) reinterpret_cast<int*>(lParam)[i] = s->parts[i].right;
		return static_cast<LRESULT>(s->parts.size());
	}
	case SB_SETTEXTW: {
		const size_t i = wParam & 0xFF;
		if (i >= s->parts.size()) return FALSE;
		auto& p = s->parts[i];
		p.type = static_cast<UINT>(wParam & 0xFF00);
		if (p.type & SBT_OWNERDRAW) { p.data = lParam; p.text.clear(); }
		else p.text = lParam ? reinterpret_cast<LPCWSTR>(lParam) : L"";
		const RECT rc = StatusPartRect(hwnd, s, i);
		InvalidateRect(hwnd, &rc, TRUE);
		return TRUE;
	}
	case SB_GETTEXTLENGTHW: {
		const size_t i = wParam & 0xFF;
		if (i >= s->parts.size()) return 0;
		return MAKELONG(static_cast<WORD>(s->parts[i].text.size()), static_cast<WORD>(s->parts[i].type));
	}
	case SB_GETTEXTW: {
		const size_t i = wParam & 0xFF;
		if (i >= s->parts.size()) return 0;
		if (s->parts[i].type & SBT_OWNERDRAW) return s->parts[i].data;
		if (lParam) std::wcscpy(reinterpret_cast<LPWSTR>(lParam), s->parts[i].text.c_str());
		return MAKELONG(static_cast<WORD>(s->parts[i].text.size()), static_cast<WORD>(s->parts[i].type));
	}
	case SB_GETRECT:
		if (wParam >= s->parts.size()) return FALSE;
		*reinterpret_cast<RECT*>(lParam) = StatusPartRect(hwnd, s, wParam);
		return TRUE;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
		RECT top = rc;
		top.bottom = 1;
		FillRect(hdc, &top, GetSysColorBrush(COLOR_BTNSHADOW));
		HGDIOBJ oldFont = SelectObject(hdc, s->font ? s->font : GetStockObject(DEFAULT_GUI_FONT));
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
		const HWND parent = GetParent(hwnd);
		const int ctlId = GetDlgCtrlID(hwnd);
		for (size_t i = 0; i < s->parts.size(); ++i) {
			RECT r = StatusPartRect(hwnd, s, i);
			if (!(s->parts[i].type & SBT_NOBORDERS) && i > 0) {
				RECT sep{r.left, r.top + 3, r.left + 1, r.bottom - 3};
				FillRect(hdc, &sep, GetSysColorBrush(COLOR_BTNSHADOW));
			}
			InflateRect(&r, -3, -1);
			if (s->parts[i].type & SBT_OWNERDRAW) {
				DRAWITEMSTRUCT dis{};
				dis.CtlID = ctlId;
				dis.itemID = static_cast<UINT>(i);
				dis.hwndItem = hwnd;
				dis.hDC = hdc;
				dis.rcItem = r;
				dis.itemData = static_cast<ULONG_PTR>(s->parts[i].data);
				SendMessageW(parent, WM_DRAWITEM, ctlId, reinterpret_cast<LPARAM>(&dis));
			} else {
				// 先頭の \t は中央寄せ、\t\t は右寄せ（Win32 の仕様）
				std::wstring_view t = s->parts[i].text;
				UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
				if (t.starts_with(L"\t\t")) { fmt |= DT_RIGHT; t.remove_prefix(2); }
				else if (t.starts_with(L"\t")) { fmt |= DT_CENTER; t.remove_prefix(1); }
				DrawTextW(hdc, t.data(), static_cast<int>(t.size()), &r, fmt);
			}
		}
		SelectObject(hdc, oldFont);
		EndPaint(hwnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- イメージリスト ----
struct ImageList {
	DWORD magic = 0x494D4C53;  // 'IMLS'
	int cx = 16, cy = 16;
	std::vector<std::vector<uint32_t>> images;
};

ImageList* ImlOf(HIMAGELIST h)
{
	auto p = reinterpret_cast<ImageList*>(h);
	return p && p->magic == 0x494D4C53 ? p : nullptr;
}

// ---- ツールバー ----
struct Toolbar {
	struct Button {
		TBBUTTON b;
		int cx;      //!< 0 なら既定の幅
		RECT rc;
	};
	std::vector<Button> buttons;
	int cx = 22, cy = 22;
	HIMAGELIST images = nullptr, disabled = nullptr;
	int pressed = -1, hot = -1;
	int rows = 1;
	DWORD extended = 0;
};

constexpr int kSeparatorWidth = 8;
constexpr int kToolbarPad = 2;

void ToolbarLayout(Toolbar* t)
{
	int x = kToolbarPad, y = kToolbarPad;
	t->rows = 1;
	for (auto& e : t->buttons) {
		const int w = e.cx ? e.cx : (e.b.fsStyle & TBSTYLE_SEP) ? kSeparatorWidth : t->cx;
		e.rc = RECT{x, y, x + w, y + t->cy};
		x += w;
		if (e.b.fsState & TBSTATE_WRAP) { x = kToolbarPad; y += t->cy; ++t->rows; }
	}
}

void ToolbarAlign(HWND hwnd, Toolbar* t)
{
	auto w = GetWindowObject(hwnd);
	if (w->style & CCS_NORESIZE) return;
	RECT p;
	GetClientRect(GetParent(hwnd), &p);
	const int h = t->rows * t->cy + kToolbarPad * 2;
	const RECT r{0, 0, p.right, h};
	if (!EqualRect(&r, &w->rect)) MoveWindow(hwnd, 0, 0, p.right, h, TRUE);
}

int ToolbarFind(Toolbar* t, WPARAM id)
{
	for (size_t i = 0; i < t->buttons.size(); ++i) {
		if (t->buttons[i].b.idCommand == static_cast<int>(id)) return static_cast<int>(i);
	}
	return -1;
}

int ToolbarHit(Toolbar* t, POINT pt)
{
	for (size_t i = 0; i < t->buttons.size(); ++i) {
		if (PtInRect(&t->buttons[i].rc, pt)) return static_cast<int>(i);
	}
	return -1;
}

void ToolbarInvalidate(HWND hwnd, Toolbar* t, int i)
{
	if (i >= 0 && i < static_cast<int>(t->buttons.size())) InvalidateRect(hwnd, &t->buttons[i].rc, TRUE);
}

void ToolbarSetState(HWND hwnd, Toolbar* t, int i, BYTE bits, bool on)
{
	if (i < 0) return;
	auto& st = t->buttons[i].b.fsState;
	const BYTE v = on ? (st | bits) : (st & ~bits);
	if (v != st) { st = v; ToolbarInvalidate(hwnd, t, i); }
}

LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto t = StateOf<Toolbar>(hwnd);
	switch (msg) {
	case WM_NCCREATE:
		GetWindowObject(hwnd)->control = new Toolbar;
		break;
	case WM_NCDESTROY:
		delete t;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	case WM_SIZE:
		ToolbarAlign(hwnd, t);
		return 0;
	case WM_SHOWWINDOW:
		if (wParam) ToolbarAlign(hwnd, t);
		break;
	case TB_AUTOSIZE:
		ToolbarAlign(hwnd, t);
		return 0;
	case TB_BUTTONSTRUCTSIZE: case TB_SETBITMAPSIZE: case TB_LOADIMAGES:
		return TRUE;
	case TB_SETBUTTONSIZE:
		t->cx = LOWORD(lParam);
		t->cy = HIWORD(lParam);
		ToolbarLayout(t);
		return TRUE;
	case TB_GETBUTTONSIZE:
		return MAKELONG(t->cx, t->cy);
	case TB_SETIMAGELIST: { auto old = t->images; t->images = reinterpret_cast<HIMAGELIST>(lParam); return reinterpret_cast<LRESULT>(old); }
	case TB_SETDISABLEDIMAGELIST: { auto old = t->disabled; t->disabled = reinterpret_cast<HIMAGELIST>(lParam); return reinterpret_cast<LRESULT>(old); }
	case TB_SETEXTENDEDSTYLE: { const DWORD old = t->extended; t->extended = static_cast<DWORD>(lParam); return old; }
	case TB_GETEXTENDEDSTYLE: return t->extended;
	case TB_ADDBUTTONSW: {
		auto b = reinterpret_cast<const TBBUTTON*>(lParam);
		for (WPARAM i = 0; i < wParam; ++i) t->buttons.push_back({b[i], 0, {}});
		ToolbarLayout(t);
		InvalidateRect(hwnd, nullptr, TRUE);
		return TRUE;
	}
	case TB_BUTTONCOUNT: return static_cast<LRESULT>(t->buttons.size());
	case TB_GETBUTTON:
		if (wParam >= t->buttons.size()) return FALSE;
		*reinterpret_cast<TBBUTTON*>(lParam) = t->buttons[wParam].b;
		return TRUE;
	case TB_COMMANDTOINDEX: return ToolbarFind(t, wParam);
	case TB_GETITEMRECT:
		if (wParam >= t->buttons.size()) return FALSE;
		*reinterpret_cast<RECT*>(lParam) = t->buttons[wParam].rc;
		return TRUE;
	case TB_GETROWS: return t->rows;
	case TB_GETBITMAP: { const int i = ToolbarFind(t, wParam); return i < 0 ? -1 : t->buttons[i].b.iBitmap; }
	case TB_HITTEST: { const int i = ToolbarHit(t, *reinterpret_cast<POINT*>(lParam)); return i < 0 ? -1 : i; }
	case TB_SETBUTTONINFOW: {
		const int i = ToolbarFind(t, wParam);
		auto info = reinterpret_cast<const TBBUTTONINFOW*>(lParam);
		if (i < 0) return FALSE;
		if (info->dwMask & TBIF_SIZE) t->buttons[i].cx = info->cx;
		if (info->dwMask & TBIF_STATE) t->buttons[i].b.fsState = info->fsState;
		if (info->dwMask & TBIF_STYLE) t->buttons[i].b.fsStyle = info->fsStyle;
		ToolbarLayout(t);
		InvalidateRect(hwnd, nullptr, TRUE);
		return TRUE;
	}
	case TB_GETSTATE: { const int i = ToolbarFind(t, wParam); return i < 0 ? -1 : t->buttons[i].b.fsState; }
	case TB_SETSTATE: {
		const int i = ToolbarFind(t, wParam);
		if (i < 0) return FALSE;
		if (t->buttons[i].b.fsState != static_cast<BYTE>(lParam)) { t->buttons[i].b.fsState = static_cast<BYTE>(lParam); ToolbarInvalidate(hwnd, t, i); }
		return TRUE;
	}
	case TB_ENABLEBUTTON: ToolbarSetState(hwnd, t, ToolbarFind(t, wParam), TBSTATE_ENABLED, LOWORD(lParam) != 0); return TRUE;
	case TB_CHECKBUTTON: ToolbarSetState(hwnd, t, ToolbarFind(t, wParam), TBSTATE_CHECKED, LOWORD(lParam) != 0); return TRUE;
	case TB_PRESSBUTTON: ToolbarSetState(hwnd, t, ToolbarFind(t, wParam), TBSTATE_PRESSED, LOWORD(lParam) != 0); return TRUE;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
		RECT bottom = rc;
		bottom.top = bottom.bottom - 1;
		FillRect(hdc, &bottom, GetSysColorBrush(COLOR_BTNSHADOW));
		for (size_t i = 0; i < t->buttons.size(); ++i) {
			const auto& e = t->buttons[i];
			if (e.b.fsStyle & TBSTYLE_SEP) {
				RECT sep{(e.rc.left + e.rc.right) / 2, e.rc.top + 3, (e.rc.left + e.rc.right) / 2 + 1, e.rc.bottom - 3};
				FillRect(hdc, &sep, GetSysColorBrush(COLOR_BTNSHADOW));
				continue;
			}
			const bool enabled = (e.b.fsState & TBSTATE_ENABLED) != 0;
			const bool down = static_cast<int>(i) == t->pressed || (e.b.fsState & (TBSTATE_CHECKED | TBSTATE_PRESSED));
			if (enabled && down) FillSolid(hdc, e.rc, RGB(205, 205, 205));
			else if (enabled && static_cast<int>(i) == t->hot) FillSolid(hdc, e.rc, RGB(225, 225, 225));
			if (auto iml = enabled ? t->images : (t->disabled ? t->disabled : t->images)) {
				auto l = ImlOf(iml);
				if (l) ImageList_Draw(iml, e.b.iBitmap, hdc, e.rc.left + (t->cx - l->cx) / 2, e.rc.top + (t->cy - l->cy) / 2, ILD_TRANSPARENT);
			}
		}
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_MOUSEMOVE: {
		const POINT pt{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		const int i = ToolbarHit(t, pt);
		if (i != t->hot && t->pressed < 0) {
			const int old = t->hot;
			t->hot = i;
			ToolbarInvalidate(hwnd, t, old);
			ToolbarInvalidate(hwnd, t, i);
		}
		return 0;
	}
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
		const POINT pt{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		const int i = ToolbarHit(t, pt);
		if (i < 0 || (t->buttons[i].b.fsStyle & TBSTYLE_SEP) || !(t->buttons[i].b.fsState & TBSTATE_ENABLED)) return 0;
		if (t->buttons[i].b.fsStyle & TBSTYLE_DROPDOWN) {
			NMTOOLBARW nm{};
			nm.hdr = {hwnd, static_cast<UINT_PTR>(GetDlgCtrlID(hwnd)), static_cast<UINT>(TBN_DROPDOWN)};
			nm.iItem = t->buttons[i].b.idCommand;
			nm.tbButton = t->buttons[i].b;
			nm.rcButton = t->buttons[i].rc;
			SendMessageW(GetParent(hwnd), WM_NOTIFY, nm.hdr.idFrom, reinterpret_cast<LPARAM>(&nm));
			return 0;
		}
		t->pressed = i;
		SetCapture(hwnd);
		ToolbarInvalidate(hwnd, t, i);
		return 0;
	}
	case WM_LBUTTONUP: {
		const int i = t->pressed;
		if (i < 0) return 0;
		t->pressed = -1;
		ReleaseCapture();
		ToolbarInvalidate(hwnd, t, i);
		const POINT pt{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
		if (ToolbarHit(t, pt) == i) {
			SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(t->buttons[i].b.idCommand, BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
		}
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- プログレスバー（状態を持つだけ。表示は単純な棒） ----
struct Progress {
	int lo = 0, hi = 100, pos = 0;
};

LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto p = StateOf<Progress>(hwnd);
	switch (msg) {
	case WM_NCCREATE: GetWindowObject(hwnd)->control = new Progress; break;
	case WM_NCDESTROY: delete p; GetWindowObject(hwnd)->control = nullptr; break;
	case PBM_SETRANGE: p->lo = LOWORD(lParam); p->hi = HIWORD(lParam); return 0;
	case PBM_SETRANGE32: p->lo = static_cast<int>(wParam); p->hi = static_cast<int>(lParam); return 0;
	case PBM_SETPOS: { const int old = p->pos; p->pos = static_cast<int>(wParam); InvalidateRect(hwnd, nullptr, FALSE); return old; }
	case PBM_GETPOS: return p->pos;
	case WM_ERASEBKGND: return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc, bar;
		GetClientRect(hwnd, &rc);
		FillSolid(hdc, rc, RGB(225, 225, 225));
		bar = rc;
		bar.right = p->hi > p->lo ? rc.left + (rc.right - rc.left) * (p->pos - p->lo) / (p->hi - p->lo) : rc.left;
		FillSolid(hdc, bar, RGB(0, 120, 215));
		EndPaint(hwnd, &ps);
		return 0;
	}
	default: break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

namespace winport {

LRESULT CALLBACK TabControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC BuiltinControlProc(const wchar_t* name)
{
	if (!_wcsicmp(name, L"SCROLLBAR")) return ScrollBarProc;
	if (!_wcsicmp(name, L"msctls_statusbar32")) return StatusBarProc;
	if (!_wcsicmp(name, L"ToolbarWindow32")) return ToolbarProc;
	if (!_wcsicmp(name, L"msctls_progress32")) return ProgressProc;
	if (!_wcsicmp(name, L"SysTabControl32")) return TabControlProc;
	if (WNDPROC proc = DialogControlProc(name)) return proc;
	return DefWindowProcW;
}

}  // namespace winport

extern "C" {

HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT, int, int)
{
	auto l = new ImageList;
	l->cx = cx;
	l->cy = cy;
	return reinterpret_cast<HIMAGELIST>(l);
}

WINBOOL WINAPI ImageList_Destroy(HIMAGELIST himl)
{
	auto l = ImlOf(himl);
	if (!l) return FALSE;
	l->magic = 0;
	delete l;
	return TRUE;
}

int WINAPI ImageList_Add(HIMAGELIST himl, HBITMAP hbmImage, HBITMAP)
{
	auto l = ImlOf(himl);
	auto b = static_cast<gdi::Bitmap*>(gdi::ObjectOf(hbmImage));
	if (!l || !b || !b->ctx) return -1;
	auto data = static_cast<unsigned char*>(CGBitmapContextGetData(b->ctx));
	const size_t bpr = CGBitmapContextGetBytesPerRow(b->ctx);
	const int first = static_cast<int>(l->images.size());
	for (int x0 = 0; x0 + l->cx <= b->width; x0 += l->cx) {
		std::vector<uint32_t> px(static_cast<size_t>(l->cx) * l->cy);
		for (int y = 0; y < std::min(l->cy, b->height); ++y) {
			std::memcpy(&px[static_cast<size_t>(y) * l->cx], data + y * bpr + x0 * 4, static_cast<size_t>(l->cx) * 4);
		}
		l->images.push_back(std::move(px));
	}
	return first;
}

WINBOOL WINAPI ImageList_Draw(HIMAGELIST himl, int i, HDC hdcDst, int x, int y, UINT)
{
	auto l = ImlOf(himl);
	if (!l || i < 0 || i >= static_cast<int>(l->images.size())) return FALSE;
	gdi::DrawArgbPixels(hdcDst, x, y, l->cx, l->cy, l->images[i].data(), l->cx, 1.0);
	return TRUE;
}

int WINAPI ImageList_GetImageCount(HIMAGELIST himl) { auto l = ImlOf(himl); return l ? static_cast<int>(l->images.size()) : 0; }

WINBOOL WINAPI ImageList_GetIconSize(HIMAGELIST himl, int* cx, int* cy)
{
	auto l = ImlOf(himl);
	if (!l) return FALSE;
	*cx = l->cx;
	*cy = l->cy;
	return TRUE;
}

HIMAGELIST WINAPI ImageList_Duplicate(HIMAGELIST himl)
{
	auto l = ImlOf(himl);
	return l ? reinterpret_cast<HIMAGELIST>(new ImageList(*l)) : nullptr;
}

COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST, COLORREF) { return CLR_NONE; }

}  // extern "C"
