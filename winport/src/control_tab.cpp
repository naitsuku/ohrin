/*
	control_tab.cpp — タブコントロール（SysTabControl32）

	上流の CTabWnd がサブクラス化して使う。ドラッグ・閉じるボタン・ウィンドウの切り替えは CTabWnd が処理するので、
	ここは項目の保持、配置、当たり判定、描画（TCS_OWNERDRAWFIXED なら親へ WM_DRAWITEM）だけを持つ。
	複数行の折り返しと、単一行で入りきらないときの選択項目までの送りに対応する。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

#include "dialog_internal.h"
#include "user32_internal.h"

namespace {

using winport::GetWindowObject;

struct TabItem {
	std::wstring text;
	int image = -1;
	LPARAM param = 0;
	DWORD state = 0;
	RECT rc{};
};

struct TabControl {
	std::vector<TabItem> items;
	int selected = -1;
	int itemWidth = 96;
	int itemHeight = 20;
	int padX = 6;
	int padY = 3;
	int scroll = 0;	// 単一行で左へ送った幅
	int rows = 1;
	HFONT font = nullptr;
	HIMAGELIST images = nullptr;
	HWND tooltip = nullptr;
};

constexpr int kEdgeX = 2;	// 左端の余白
constexpr int kEdgeY = 2;	// 上端の余白

TabControl* StateOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? static_cast<TabControl*>(w->control) : nullptr;
}

int ItemWidth(HWND hwnd, const TabControl* t, const TabItem& item)
{
	if (GetWindowLongPtrW(hwnd, GWL_STYLE) & TCS_FIXEDWIDTH) return t->itemWidth;
	HDC dc = GetDC(hwnd);
	HGDIOBJ old = SelectObject(dc, t->font ? t->font : winport::DialogFont());
	SIZE size{};
	GetTextExtentPoint32W(dc, item.text.c_str(), static_cast<int>(item.text.size()), &size);
	SelectObject(dc, old);
	ReleaseDC(hwnd, dc);
	int width = size.cx + t->padX * 2;
	if (t->images && item.image >= 0) {
		int cx = 0, cy = 0;
		ImageList_GetIconSize(t->images, &cx, &cy);
		width += cx + t->padX;
	}
	return width;
}

//! 全項目の矩形を決める。単一行のときは選択項目が見えるように送る
void Layout(HWND hwnd, TabControl* t)
{
	RECT client;
	GetClientRect(hwnd, &client);
	const bool multiLine = GetWindowLongPtrW(hwnd, GWL_STYLE) & TCS_MULTILINE;
	int x = kEdgeX;
	int y = kEdgeY;
	t->rows = 1;
	for (TabItem& item : t->items) {
		const int width = ItemWidth(hwnd, t, item);
		if (multiLine && x > kEdgeX && x + width > client.right) {
			x = kEdgeX;
			y += t->itemHeight;
			++t->rows;
		}
		item.rc = {x, y, x + width, y + t->itemHeight};
		x += width;
	}
	if (multiLine) {
		t->scroll = 0;
		return;
	}
	const int total = x + kEdgeX;
	t->scroll = std::clamp(t->scroll, 0, std::max(0, total - static_cast<int>(client.right)));
	if (t->selected >= 0 && t->selected < static_cast<int>(t->items.size())) {
		const RECT& rc = t->items[t->selected].rc;
		if (rc.left - t->scroll < kEdgeX) t->scroll = rc.left - kEdgeX;
		else if (rc.right - t->scroll > client.right) t->scroll = rc.right - client.right;
	}
	for (TabItem& item : t->items) OffsetRect(&item.rc, -t->scroll, 0);
}

void Refresh(HWND hwnd, TabControl* t)
{
	Layout(hwnd, t);
	InvalidateRect(hwnd, nullptr, TRUE);
}

int HitTest(const TabControl* t, POINT pt)
{
	for (size_t i = 0; i < t->items.size(); ++i) {
		if (PtInRect(&t->items[i].rc, pt)) return static_cast<int>(i);
	}
	return -1;
}

void ReadItem(TabItem& item, const TCITEMW* src)
{
	if (src->mask & TCIF_TEXT) item.text = src->pszText ? src->pszText : L"";
	if (src->mask & TCIF_IMAGE) item.image = src->iImage;
	if (src->mask & TCIF_PARAM) item.param = src->lParam;
	if (src->mask & TCIF_STATE) item.state = (item.state & ~src->dwStateMask) | (src->dwState & src->dwStateMask);
}

void WriteItem(const TabItem& item, TCITEMW* dest)
{
	if ((dest->mask & TCIF_TEXT) && dest->pszText && dest->cchTextMax > 0) {
		const size_t n = std::min(item.text.size(), static_cast<size_t>(dest->cchTextMax - 1));
		std::wmemcpy(dest->pszText, item.text.data(), n);
		dest->pszText[n] = L'\0';
	}
	if (dest->mask & TCIF_IMAGE) dest->iImage = item.image;
	if (dest->mask & TCIF_PARAM) dest->lParam = item.param;
	if (dest->mask & TCIF_STATE) dest->dwState = item.state & dest->dwStateMask;
}

LRESULT Notify(HWND hwnd, UINT code)
{
	NMHDR header{hwnd, static_cast<UINT_PTR>(GetDlgCtrlID(hwnd)), code};
	return SendMessageW(GetParent(hwnd), WM_NOTIFY, header.idFrom, reinterpret_cast<LPARAM>(&header));
}

void DrawItem(HWND hwnd, HDC dc, const TabControl* t, int index, bool ownerDraw)
{
	const TabItem& item = t->items[index];
	const bool selected = index == t->selected;
	RECT rc = item.rc;
	if (!selected) rc.top += 2;
	if (ownerDraw) {
		DRAWITEMSTRUCT dis{};
		dis.CtlType = ODT_TAB;
		dis.CtlID = GetDlgCtrlID(hwnd);
		dis.itemID = index;
		dis.itemAction = ODA_DRAWENTIRE;
		dis.itemState = selected ? ODS_SELECTED : 0;
		dis.hwndItem = hwnd;
		dis.hDC = dc;
		dis.rcItem = rc;
		dis.itemData = item.param;
		SendMessageW(GetParent(hwnd), WM_DRAWITEM, dis.CtlID, reinterpret_cast<LPARAM>(&dis));
		return;
	}
	HBRUSH fill = GetSysColorBrush(selected ? COLOR_WINDOW : COLOR_BTNFACE);
	FillRect(dc, &rc, fill);
	HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
	HGDIOBJ oldPen = SelectObject(dc, pen);
	MoveToEx(dc, rc.left, rc.bottom - 1, nullptr);
	LineTo(dc, rc.left, rc.top);
	LineTo(dc, rc.right - 1, rc.top);
	LineTo(dc, rc.right - 1, rc.bottom);
	SelectObject(dc, oldPen);
	DeleteObject(pen);

	RECT text = rc;
	text.left += t->padX;
	text.right -= t->padX;
	if (t->images && item.image >= 0) {
		int cx = 0, cy = 0;
		ImageList_GetIconSize(t->images, &cx, &cy);
		ImageList_Draw(t->images, item.image, dc, text.left, rc.top + (rc.bottom - rc.top - cy) / 2, ILD_TRANSPARENT);
		text.left += cx + t->padX / 2;
	}
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
	DrawTextW(dc, item.text.c_str(), static_cast<int>(item.text.size()), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void Paint(HWND hwnd, TabControl* t)
{
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);
	RECT client;
	GetClientRect(hwnd, &client);
	FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
	HGDIOBJ oldFont = SelectObject(dc, t->font ? t->font : winport::DialogFont());
	const bool ownerDraw = GetWindowLongPtrW(hwnd, GWL_STYLE) & TCS_OWNERDRAWFIXED;
	for (int i = 0; i < static_cast<int>(t->items.size()); ++i) {
		if (i != t->selected) DrawItem(hwnd, dc, t, i, ownerDraw);
	}
	if (t->selected >= 0 && t->selected < static_cast<int>(t->items.size())) DrawItem(hwnd, dc, t, t->selected, ownerDraw);	// 選択項目を最前面に
	SelectObject(dc, oldFont);
	EndPaint(hwnd, &ps);
}

}  // namespace

namespace winport {

LRESULT CALLBACK TabControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TabControl* t = StateOf(hwnd);
	if (msg == WM_NCCREATE) {
		GetWindowObject(hwnd)->control = new TabControl;
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	if (!t) return DefWindowProcW(hwnd, msg, wParam, lParam);
	const int count = static_cast<int>(t->items.size());
	switch (msg) {
	case WM_NCDESTROY:
		if (t->tooltip) DestroyWindow(t->tooltip);
		delete t;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	case WM_SIZE:
	case WM_STYLECHANGED:
		Refresh(hwnd, t);
		break;
	case WM_SETFONT:
		t->font = reinterpret_cast<HFONT>(wParam);
		Refresh(hwnd, t);
		return 0;
	case WM_GETFONT:
		return reinterpret_cast<LRESULT>(t->font);
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		Paint(hwnd, t);
		return 0;
	case WM_LBUTTONDOWN: {
		const POINT pt{static_cast<SHORT>(LOWORD(lParam)), static_cast<SHORT>(HIWORD(lParam))};
		const int hit = HitTest(t, pt);
		if (hit >= 0 && hit != t->selected && !Notify(hwnd, TCN_SELCHANGING)) {
			t->selected = hit;
			Refresh(hwnd, t);
			Notify(hwnd, TCN_SELCHANGE);
		}
		return 0;
	}
	case TCM_GETITEMCOUNT:
		return count;
	case TCM_INSERTITEMW: {
		const int at = std::clamp(static_cast<int>(wParam), 0, count);
		TabItem item;
		ReadItem(item, reinterpret_cast<const TCITEMW*>(lParam));
		t->items.insert(t->items.begin() + at, std::move(item));
		if (t->selected >= at) ++t->selected;
		Refresh(hwnd, t);
		return at;
	}
	case TCM_DELETEITEM: {
		const int at = static_cast<int>(wParam);
		if (at < 0 || at >= count) return FALSE;
		t->items.erase(t->items.begin() + at);
		if (t->selected == at) t->selected = -1;
		else if (t->selected > at) --t->selected;
		Refresh(hwnd, t);
		return TRUE;
	}
	case TCM_DELETEALLITEMS:
		t->items.clear();
		t->selected = -1;
		t->scroll = 0;
		Refresh(hwnd, t);
		return TRUE;
	case TCM_GETITEMW: {
		const int at = static_cast<int>(wParam);
		if (at < 0 || at >= count) return FALSE;
		WriteItem(t->items[at], reinterpret_cast<TCITEMW*>(lParam));
		return TRUE;
	}
	case TCM_SETITEMW: {
		const int at = static_cast<int>(wParam);
		if (at < 0 || at >= count) return FALSE;
		ReadItem(t->items[at], reinterpret_cast<const TCITEMW*>(lParam));
		Refresh(hwnd, t);
		return TRUE;
	}
	case TCM_GETCURSEL:
	case TCM_GETCURFOCUS:
		return t->selected;
	case TCM_SETCURSEL:
	case TCM_SETCURFOCUS: {
		const int previous = t->selected;
		const int at = static_cast<int>(wParam);
		if (at >= count) return -1;
		t->selected = at;
		Refresh(hwnd, t);
		return previous;
	}
	case TCM_GETITEMRECT: {
		const int at = static_cast<int>(wParam);
		if (at < 0 || at >= count) return FALSE;
		*reinterpret_cast<RECT*>(lParam) = t->items[at].rc;
		return TRUE;
	}
	case TCM_HITTEST: {
		auto* info = reinterpret_cast<TCHITTESTINFO*>(lParam);
		const int hit = HitTest(t, info->pt);
		info->flags = hit >= 0 ? TCHT_ONITEM : TCHT_NOWHERE;
		return hit;
	}
	case TCM_SETITEMSIZE: {
		const LRESULT previous = MAKELONG(t->itemWidth, t->itemHeight);
		t->itemWidth = static_cast<SHORT>(LOWORD(lParam));
		t->itemHeight = static_cast<SHORT>(HIWORD(lParam));
		Refresh(hwnd, t);
		return previous;
	}
	case TCM_SETPADDING:
		t->padX = static_cast<SHORT>(LOWORD(lParam));
		t->padY = static_cast<SHORT>(HIWORD(lParam));
		Refresh(hwnd, t);
		return 0;
	case TCM_GETROWCOUNT:
		return t->rows;
	case TCM_ADJUSTRECT: {
		auto* rc = reinterpret_cast<RECT*>(lParam);
		const int header = kEdgeY + t->rows * t->itemHeight;
		if (wParam) rc->top -= header;	// 表示領域 → ウィンドウ
		else rc->top += header;			// ウィンドウ → 表示領域
		return 0;
	}
	case TCM_SETIMAGELIST: {
		HIMAGELIST previous = t->images;
		t->images = reinterpret_cast<HIMAGELIST>(lParam);
		Refresh(hwnd, t);
		return reinterpret_cast<LRESULT>(previous);
	}
	case TCM_GETIMAGELIST:
		return reinterpret_cast<LRESULT>(t->images);
	case TCM_GETTOOLTIPS:
		if (!t->tooltip && (GetWindowLongPtrW(hwnd, GWL_STYLE) & TCS_TOOLTIPS)) {
			t->tooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, L"", WS_POPUP, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
		}
		return reinterpret_cast<LRESULT>(t->tooltip);
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace winport
