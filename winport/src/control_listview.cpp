/*
	control_listview.cpp — リストビュー（SysListView32）

	上流の使い方に合わせ、詳細表示（LVS_REPORT）と一覧表示（LVS_LIST、それ以外の表示形式もこれで描く）を持つ。
	ヘッダーは子ウィンドウにせず、クライアント領域の上端に自分で描く（上流はヘッダーを直接操作しない）。
	項目追加のたびにスクロールバーを更新すると遅いので、更新は描画の直前などへまとめる。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include "control_common.h"
#include "dialog_internal.h"
#include "user32_internal.h"

namespace {

using namespace winport;

constexpr int kCheckSize = 13;
constexpr int kTextPad = 6;
constexpr int kHeaderGrip = 4;
constexpr UINT_PTR kEditTimer = 0x4C56;
constexpr UINT kCheckedState = INDEXTOSTATEIMAGEMASK(2);
constexpr UINT kUncheckedState = INDEXTOSTATEIMAGEMASK(1);

struct Column {
	std::wstring text;
	int width = 60;
	int fmt = LVCFMT_LEFT;
};

struct Item {
	std::vector<std::wstring> text;
	int image = -1;
	LPARAM param = 0;
	UINT state = 0;
	const std::wstring& Text(size_t sub) const
	{
		static const std::wstring empty;
		return sub < text.size() ? text[sub] : empty;
	}
};

struct ListView {
	std::vector<Column> columns;
	std::vector<Item> items;
	int top = 0;			//!< 詳細表示の先頭の項目
	int offset = 0;			//!< 詳細表示は左へ送ったピクセル数、一覧表示は左へ送った列数
	int rowHeight = 17;
	int headerHeight = 20;
	int listWidth = 0;		//!< 一覧表示の列の幅（0 なら内容から決める）
	int autoListWidth = 0;
	bool listWidthDirty = true;
	HFONT font = nullptr;
	DWORD exStyle = 0;
	bool border = false;
	bool focused = false;
	bool dirty = true;
	bool syncing = false;
	int focus = -1;
	int anchor = -1;
	COLORREF textColor = CLR_DEFAULT;
	COLORREF textBack = CLR_DEFAULT;
	COLORREF back = CLR_DEFAULT;
	HIMAGELIST images[3] = {};
	HWND edit = nullptr;
	int editItem = -1;
	bool ending = false;
	int resizing = -1;
	int resizeStart = 0;
	int resizeWidth = 0;
	int pressedColumn = -1;
	int pendingEdit = -1;	//!< 選択済みの項目のクリック。ボタンを離したら編集を始める
	POINT pressPoint{};
	bool clickPending = false;
};

ListView* StateOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? static_cast<ListView*>(w->control) : nullptr;
}

DWORD Style(HWND hwnd) { return static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)); }
bool IsReport(HWND hwnd) { return (Style(hwnd) & LVS_TYPEMASK) == LVS_REPORT; }
bool SingleSelect(HWND hwnd) { return (Style(hwnd) & LVS_SINGLESEL) != 0; }
HFONT FontOf(const ListView* s) { return s->font ? s->font : DialogFont(); }
int Count(const ListView* s) { return static_cast<int>(s->items.size()); }
int HeaderHeight(HWND hwnd, const ListView* s) { return IsReport(hwnd) && !(Style(hwnd) & LVS_NOCOLUMNHEADER) ? s->headerHeight : 0; }
void Invalidate(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void MarkDirty(HWND hwnd, ListView* s)
{
	s->dirty = true;
	Invalidate(hwnd);
}

int ColumnsWidth(const ListView* s)
{
	int total = 0;
	for (const Column& c : s->columns) total += c.width;
	return total;
}

int RowsPerPage(HWND hwnd, const ListView* s)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	return std::max(1, (static_cast<int>(rc.bottom) - HeaderHeight(hwnd, s)) / s->rowHeight);
}

int ListColumnWidth(HWND hwnd, ListView* s)
{
	if (s->listWidth > 0) return s->listWidth;
	if (s->listWidthDirty) {
		HDC hdc = GetDC(hwnd);
		HGDIOBJ old = SelectObject(hdc, FontOf(s));
		int width = 40;
		for (const Item& item : s->items) width = std::max(width, TextWidth(hdc, item.Text(0)));
		SelectObject(hdc, old);
		ReleaseDC(hwnd, hdc);
		s->autoListWidth = width + kTextPad * 2 + 4;
		s->listWidthDirty = false;
	}
	return s->autoListWidth;
}

void Sync(HWND hwnd, ListView* s)
{
	if (!s->dirty || s->syncing) return;
	s->syncing = true;
	// スクロールバーの表示が変わるとクライアント領域も変わるので、落ち着くまで繰り返す
	for (int pass = 0; pass < 3; ++pass) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		const int before = rc.right * 65536 + rc.bottom;
		if (IsReport(hwnd)) {
			const int page = RowsPerPage(hwnd, s);
			s->top = std::clamp(s->top, 0, std::max(0, Count(s) - page));
			const int total = ColumnsWidth(s);
			s->offset = std::clamp(s->offset, 0, std::max(0, total - static_cast<int>(rc.right)));
			SCROLLINFO v{sizeof(v), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, Count(s) - 1), static_cast<UINT>(page), s->top};
			SetScrollInfo(hwnd, SB_VERT, &v, TRUE);
			SCROLLINFO h{sizeof(h), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, total - 1), static_cast<UINT>(rc.right), s->offset};
			SetScrollInfo(hwnd, SB_HORZ, &h, TRUE);
		} else {
			const int page = RowsPerPage(hwnd, s);
			const int columns = (Count(s) + page - 1) / page;
			const int visible = std::max(1, static_cast<int>(rc.right) / ListColumnWidth(hwnd, s));
			s->offset = std::clamp(s->offset, 0, std::max(0, columns - visible));
			SCROLLINFO h{sizeof(h), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, columns - 1), static_cast<UINT>(visible), s->offset};
			SetScrollInfo(hwnd, SB_HORZ, &h, TRUE);
		}
		GetClientRect(hwnd, &rc);
		if (before == rc.right * 65536 + rc.bottom) break;
	}
	s->dirty = false;
	s->syncing = false;
}

RECT ItemRect(HWND hwnd, ListView* s, int index)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	if (IsReport(hwnd)) {
		const int y = HeaderHeight(hwnd, s) + (index - s->top) * s->rowHeight;
		return {-s->offset, y, -s->offset + std::max(ColumnsWidth(s), static_cast<int>(rc.right) + s->offset), y + s->rowHeight};
	}
	const int page = RowsPerPage(hwnd, s);
	const int width = ListColumnWidth(hwnd, s);
	const int x = (index / page - s->offset) * width;
	const int y = (index % page) * s->rowHeight;
	return {x, y, x + width, y + s->rowHeight};
}

int LabelLeft(const ListView* s)
{
	int x = 2;
	if (s->exStyle & LVS_EX_CHECKBOXES) x += kCheckSize + 4;
	if (s->images[LVSIL_SMALL]) {
		int cx = 0, cy = 0;
		ImageList_GetIconSize(s->images[LVSIL_SMALL], &cx, &cy);
		x += cx + 2;
	}
	return x;
}

RECT CheckRect(const RECT& row, const ListView* s)
{
	const int y = row.top + (s->rowHeight - kCheckSize) / 2;
	return {row.left + 2, y, row.left + 2 + kCheckSize, y + kCheckSize};
}

int ColumnAt(const ListView* s, int x)
{
	int left = -s->offset;
	for (size_t i = 0; i < s->columns.size(); ++i) {
		left += s->columns[i].width;
		if (x < left) return static_cast<int>(i);
	}
	return -1;
}

//! 項目の当たり判定。詳細表示で全行選択でないときは先頭の列だけが項目
int ItemAt(HWND hwnd, ListView* s, POINT pt, int* subItem = nullptr)
{
	Sync(hwnd, s);
	int index = -1;
	if (IsReport(hwnd)) {
		const int header = HeaderHeight(hwnd, s);
		if (pt.y < header) return -1;
		index = s->top + (pt.y - header) / s->rowHeight;
		const int column = ColumnAt(s, pt.x);
		if (subItem) *subItem = std::max(column, 0);
		if (column < 0 || (column > 0 && !(s->exStyle & LVS_EX_FULLROWSELECT) && !subItem)) return -1;
	} else {
		const int page = RowsPerPage(hwnd, s);
		const int row = pt.y / s->rowHeight;
		if (row >= page) return -1;
		index = (pt.x / ListColumnWidth(hwnd, s) + s->offset) * page + row;
		if (subItem) *subItem = 0;
	}
	return index >= 0 && index < Count(s) ? index : -1;
}

void EnsureVisible(HWND hwnd, ListView* s, int index)
{
	if (index < 0 || index >= Count(s)) return;
	Sync(hwnd, s);
	const int page = RowsPerPage(hwnd, s);
	if (IsReport(hwnd)) {
		if (index < s->top) s->top = index;
		else if (index >= s->top + page) s->top = index - page + 1;
	} else {
		RECT rc;
		GetClientRect(hwnd, &rc);
		const int visible = std::max(1, static_cast<int>(rc.right) / ListColumnWidth(hwnd, s));
		const int column = index / page;
		if (column < s->offset) s->offset = column;
		else if (column >= s->offset + visible) s->offset = column - visible + 1;
	}
	MarkDirty(hwnd, s);
}

//! 項目の状態を変え、変わったら親へ通知する
void SetState(HWND hwnd, ListView* s, int index, UINT state, UINT mask)
{
	if (index < 0 || index >= Count(s)) return;
	const UINT oldState = s->items[index].state;
	const UINT newState = (oldState & ~mask) | (state & mask);
	if (newState == oldState) return;
	NMLISTVIEW n{};
	n.iItem = index;
	n.uOldState = oldState;
	n.uNewState = newState;
	n.uChanged = LVIF_STATE;
	n.lParam = s->items[index].param;
	if (NotifyParent(hwnd, &n.hdr, LVN_ITEMCHANGING)) return;
	if (index >= Count(s)) return;
	if ((newState & ~oldState) & LVIS_SELECTED && SingleSelect(hwnd)) {
		for (int i = 0; i < Count(s); ++i) {
			if (i != index && (s->items[i].state & LVIS_SELECTED)) SetState(hwnd, s, i, 0, LVIS_SELECTED);
		}
	}
	if ((newState & ~oldState) & LVIS_FOCUSED) {
		if (s->focus >= 0 && s->focus != index) SetState(hwnd, s, s->focus, 0, LVIS_FOCUSED);
		s->focus = index;
	} else if ((oldState & ~newState) & LVIS_FOCUSED && s->focus == index) {
		s->focus = -1;
	}
	if (index >= Count(s)) return;
	s->items[index].state = newState;
	Invalidate(hwnd);
	NotifyParent(hwnd, &n.hdr, LVN_ITEMCHANGED);
}

void DeselectAll(HWND hwnd, ListView* s, int except = -1)
{
	for (int i = 0; i < Count(s); ++i) {
		if (i != except && (s->items[i].state & LVIS_SELECTED)) SetState(hwnd, s, i, 0, LVIS_SELECTED);
	}
}

void ToggleCheck(HWND hwnd, ListView* s, int index)
{
	const bool checked = (s->items[index].state & LVIS_STATEIMAGEMASK) == kCheckedState;
	SetState(hwnd, s, index, checked ? kUncheckedState : kCheckedState, LVIS_STATEIMAGEMASK);
}

//! クリックやキーでの選択
void SelectByUser(HWND hwnd, ListView* s, int index, bool shift, bool control)
{
	if (index < 0 || index >= Count(s)) return;
	const bool single = SingleSelect(hwnd);
	if (shift && !single && s->anchor >= 0) {
		const int from = std::min(s->anchor, index), to = std::max(s->anchor, index);
		for (int i = 0; i < Count(s); ++i) {
			if (i < from || i > to) SetState(hwnd, s, i, 0, LVIS_SELECTED);
			else SetState(hwnd, s, i, LVIS_SELECTED, LVIS_SELECTED);
		}
		SetState(hwnd, s, index, LVIS_FOCUSED, LVIS_FOCUSED);
	} else if (control && !single) {
		SetState(hwnd, s, index, s->items[index].state ^ LVIS_SELECTED, LVIS_SELECTED);
		SetState(hwnd, s, index, LVIS_FOCUSED, LVIS_FOCUSED);
		s->anchor = index;
	} else {
		DeselectAll(hwnd, s, index);
		SetState(hwnd, s, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		s->anchor = index;
	}
	EnsureVisible(hwnd, s, index);
}

// ---- ラベル編集 ----
HWND BeginEdit(HWND hwnd, ListView* s, int index)
{
	if (s->edit || index < 0 || index >= Count(s)) return s->edit;
	EnsureVisible(hwnd, s, index);
	Sync(hwnd, s);
	NMLVDISPINFOW n{};
	n.item.iItem = index;
	n.item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE;
	n.item.lParam = s->items[index].param;
	std::wstring text = s->items[index].Text(0);
	n.item.pszText = text.data();
	n.item.cchTextMax = static_cast<int>(text.size()) + 1;
	s->editItem = index;
	RECT rc = ItemRect(hwnd, s, index);
	rc.left += LabelLeft(s);
	if (IsReport(hwnd) && !s->columns.empty()) rc.right = rc.left + std::max(s->columns[0].width - LabelLeft(s), 60);
	rc.top -= 1;
	rc.bottom += 1;
	s->edit = CreateLabelEdit(hwnd, rc, s->items[index].Text(0), FontOf(s));
	if (NotifyParent(hwnd, &n.hdr, LVN_BEGINLABELEDITW)) {
		CloseLabelEdit(hwnd, s->edit);
		s->edit = nullptr;
		s->editItem = -1;
		return nullptr;
	}
	if (s->edit) SetFocus(s->edit);
	return s->edit;
}

void EndEdit(HWND hwnd, ListView* s, bool save)
{
	if (!s->edit || s->ending) return;
	s->ending = true;
	HWND edit = s->edit;
	const int index = s->editItem;
	const bool hadFocus = GetFocus() == edit;
	const int length = GetWindowTextLengthW(edit);
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	GetWindowTextW(edit, text.data(), length + 1);
	NMLVDISPINFOW n{};
	n.item.iItem = index;
	n.item.mask = LVIF_TEXT | LVIF_PARAM;
	n.item.pszText = save ? text.data() : nullptr;
	n.item.cchTextMax = length + 1;
	if (index >= 0 && index < Count(s)) n.item.lParam = s->items[index].param;
	const LRESULT accepted = NotifyParent(hwnd, &n.hdr, LVN_ENDLABELEDITW);
	if (save && accepted && index >= 0 && index < Count(s)) {
		if (s->items[index].text.empty()) s->items[index].text.resize(1);
		s->items[index].text[0] = text.c_str();
		s->listWidthDirty = true;
	}
	s->edit = nullptr;
	s->editItem = -1;
	s->ending = false;
	CloseLabelEdit(hwnd, edit);
	if (hadFocus) SetFocus(hwnd);
	MarkDirty(hwnd, s);
}

// ---- 描画 ----
void DrawCheck(HDC hdc, const RECT& box, bool checked)
{
	FillColor(hdc, box, checked ? kControlAccent : RGB(255, 255, 255));
	FrameColor(hdc, box, checked ? kControlAccent : RGB(160, 160, 160));
	if (checked) {
		LineColor(hdc, box.left + 3, box.top + 6, box.left + 5, box.top + 9, RGB(255, 255, 255), 2);
		LineColor(hdc, box.left + 5, box.top + 9, box.left + 10, box.top + 3, RGB(255, 255, 255), 2);
	}
}

void DrawCell(HDC hdc, const std::wstring& text, RECT rc, int fmt, COLORREF color)
{
	UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
	if ((fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT) flags |= DT_RIGHT;
	else if ((fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_CENTER) flags |= DT_CENTER;
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, color);
	DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, flags);
}

//! NM_CUSTOMDRAW を親へ送り、返り値を返す
LRESULT CustomDraw(HWND hwnd, HDC hdc, DWORD stage, int index, UINT itemState, const RECT& rc, COLORREF* text, COLORREF* back, LPARAM param)
{
	NMLVCUSTOMDRAW cd{};
	cd.nmcd.dwDrawStage = stage;
	cd.nmcd.hdc = hdc;
	cd.nmcd.rc = rc;
	cd.nmcd.dwItemSpec = static_cast<DWORD_PTR>(index);
	cd.nmcd.uItemState = itemState;
	cd.nmcd.lItemlParam = param;
	cd.clrText = *text;
	cd.clrTextBk = *back;
	const LRESULT result = NotifyParent(hwnd, &cd.nmcd.hdr, NM_CUSTOMDRAW);
	*text = cd.clrText;
	*back = cd.clrTextBk;
	return result;
}

void PaintItem(HWND hwnd, ListView* s, HDC hdc, int index, const RECT& client, bool itemDraw)
{
	const Item& item = s->items[index];
	const bool enabled = IsWindowEnabled(hwnd) != FALSE;
	const bool report = IsReport(hwnd);
	RECT row = ItemRect(hwnd, s, index);
	const bool selected = (item.state & LVIS_SELECTED) && (s->focused || (Style(hwnd) & LVS_SHOWSELALWAYS));
	COLORREF text = !enabled ? kControlDisabledText : (s->textColor != CLR_DEFAULT ? s->textColor : RGB(0, 0, 0));
	COLORREF back = s->textBack != CLR_DEFAULT && s->textBack != CLR_NONE ? s->textBack : CLR_NONE;
	if (selected) {
		back = s->focused ? kControlAccent : kControlInactive;
		if (s->focused) text = RGB(255, 255, 255);
	}
	if (itemDraw) CustomDraw(hwnd, hdc, CDDS_ITEMPREPAINT, index, selected ? CDIS_SELECTED : 0, row, &text, &back, item.param);

	const bool wholeRow = report && (s->exStyle & LVS_EX_FULLROWSELECT);
	RECT highlight = row;
	if (report && !wholeRow && !s->columns.empty()) highlight.right = highlight.left + s->columns[0].width;
	if (wholeRow) highlight.right = std::min<int>(highlight.right, client.right);
	if (back != CLR_NONE) FillColor(hdc, highlight, back);

	const int labelLeft = LabelLeft(s);
	if (s->exStyle & LVS_EX_CHECKBOXES) DrawCheck(hdc, CheckRect(row, s), (item.state & LVIS_STATEIMAGEMASK) == kCheckedState);
	if (s->images[LVSIL_SMALL] && item.image >= 0) {
		int cx = 0, cy = 0;
		ImageList_GetIconSize(s->images[LVSIL_SMALL], &cx, &cy);
		ImageList_Draw(s->images[LVSIL_SMALL], item.image, hdc, row.left + labelLeft - cx - 2, row.top + (s->rowHeight - cy) / 2, ILD_TRANSPARENT);
	}
	if (!report) {
		RECT cell = row;
		cell.left += labelLeft + 2;
		DrawCell(hdc, item.Text(0), cell, LVCFMT_LEFT, text);
		return;
	}
	int x = row.left;
	for (size_t c = 0; c < s->columns.size(); ++c) {
		const Column& column = s->columns[c];
		RECT cell{x + (c == 0 ? labelLeft + 2 : kTextPad), row.top, x + column.width - kTextPad / 2, row.bottom};
		x += column.width;
		if (x <= 0 || cell.left >= client.right) continue;
		// 全行選択でないとき、選択の色は先頭の列だけ
		const COLORREF color = c > 0 && selected && !wholeRow ? (s->textColor != CLR_DEFAULT ? s->textColor : RGB(0, 0, 0)) : text;
		DrawCell(hdc, item.Text(c), cell, column.fmt, color);
	}
}

void PaintHeader(HWND hwnd, ListView* s, HDC hdc, const RECT& client)
{
	const int height = HeaderHeight(hwnd, s);
	if (!height) return;
	RECT bar{0, 0, client.right, height};
	FillColor(hdc, bar, RGB(242, 242, 242));
	LineColor(hdc, 0, height - 1, client.right, height - 1, kControlBorder);
	int x = -s->offset;
	for (const Column& column : s->columns) {
		RECT cell{x + kTextPad, 0, x + column.width - kTextPad / 2, height};
		DrawCell(hdc, column.text, cell, column.fmt, RGB(0, 0, 0));
		x += column.width;
		LineColor(hdc, x - 1, 3, x - 1, height - 4, kControlBorder);
	}
}

void Paint(HWND hwnd, ListView* s)
{
	Sync(hwnd, s);
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);
	RECT client;
	GetClientRect(hwnd, &client);
	HGDIOBJ oldFont = SelectObject(hdc, FontOf(s));
	const bool enabled = IsWindowEnabled(hwnd) != FALSE;
	const COLORREF back = s->back != CLR_DEFAULT && s->back != CLR_NONE ? s->back : (enabled ? RGB(255, 255, 255) : RGB(245, 245, 245));
	FillColor(hdc, client, back);
	COLORREF unusedText = 0, unusedBack = 0;
	const LRESULT mode = CustomDraw(hwnd, hdc, CDDS_PREPAINT, 0, 0, client, &unusedText, &unusedBack, 0);
	const bool itemDraw = (mode & CDRF_NOTIFYITEMDRAW) != 0;
	if (IsReport(hwnd)) {
		const int header = HeaderHeight(hwnd, s);
		const int last = std::min(Count(s), s->top + (static_cast<int>(client.bottom) - header) / s->rowHeight + 2);
		for (int i = s->top; i < last; ++i) PaintItem(hwnd, s, hdc, i, client, itemDraw);
		if (s->exStyle & LVS_EX_GRIDLINES) {
			for (int y = header + s->rowHeight - 1; y < client.bottom; y += s->rowHeight) LineColor(hdc, 0, y, client.right, y, RGB(225, 225, 225));
			int x = -s->offset;
			for (const Column& c : s->columns) {
				x += c.width;
				LineColor(hdc, x - 1, header, x - 1, client.bottom, RGB(225, 225, 225));
			}
		}
		PaintHeader(hwnd, s, hdc, client);
	} else {
		const int page = RowsPerPage(hwnd, s);
		const int width = ListColumnWidth(hwnd, s);
		const int first = s->offset * page;
		const int last = std::min(Count(s), first + (static_cast<int>(client.right) / width + 2) * page);
		for (int i = first; i < last; ++i) PaintItem(hwnd, s, hdc, i, client, itemDraw);
	}
	if (s->border) FrameColor(hdc, client, s->focused ? kControlAccent : kControlBorder);
	SelectObject(hdc, oldFont);
	EndPaint(hwnd, &ps);
}

// ---- 項目の操作 ----
void ReadItem(HWND hwnd, ListView* s, int index, const LVITEMW* src)
{
	Item& item = s->items[index];
	if (src->mask & LVIF_TEXT) {
		const size_t sub = static_cast<size_t>(std::max(src->iSubItem, 0));
		if (item.text.size() <= sub) item.text.resize(sub + 1);
		item.text[sub] = src->pszText && src->pszText != LPSTR_TEXTCALLBACKW ? src->pszText : L"";
		if (sub == 0) s->listWidthDirty = true;
	}
	if (src->iSubItem == 0) {
		if (src->mask & LVIF_IMAGE) item.image = src->iImage;
		if (src->mask & LVIF_PARAM) item.param = src->lParam;
	}
	(void)hwnd;
}

int InsertPosition(HWND hwnd, const ListView* s, const LVITEMW* src)
{
	const DWORD style = Style(hwnd);
	if ((style & (LVS_SORTASCENDING | LVS_SORTDESCENDING)) && (src->mask & LVIF_TEXT) && src->pszText && src->pszText != LPSTR_TEXTCALLBACKW) {
		const bool descending = (style & LVS_SORTDESCENDING) != 0;
		const auto it = std::upper_bound(s->items.begin(), s->items.end(), src->pszText, [&](const wchar_t* text, const Item& item) {
			const int order = _wcsicmp(text, item.Text(0).c_str());
			return descending ? order > 0 : order < 0;
		});
		return static_cast<int>(it - s->items.begin());
	}
	return std::clamp(src->iItem, 0, Count(s));
}

int Insert(HWND hwnd, ListView* s, const LVITEMW* src)
{
	const int at = InsertPosition(hwnd, s, src);
	Item item;
	if (s->exStyle & LVS_EX_CHECKBOXES) item.state = kUncheckedState;
	s->items.insert(s->items.begin() + at, std::move(item));
	if (s->focus >= at) ++s->focus;
	if (s->anchor >= at) ++s->anchor;
	if (s->top > at) ++s->top;
	LVITEMW copy = *src;
	copy.iSubItem = 0;
	ReadItem(hwnd, s, at, &copy);
	if (src->mask & LVIF_STATE) {
		const UINT mask = src->stateMask;
		s->items[at].state = (s->items[at].state & ~mask) | (src->state & mask);
		if (s->items[at].state & LVIS_FOCUSED) s->focus = at;
	}
	MarkDirty(hwnd, s);
	return at;
}

void Erase(HWND hwnd, ListView* s, int index)
{
	s->items.erase(s->items.begin() + index);
	if (s->focus == index) s->focus = -1;
	else if (s->focus > index) --s->focus;
	if (s->anchor == index) s->anchor = -1;
	else if (s->anchor > index) --s->anchor;
	if (s->top > index) --s->top;
	s->listWidthDirty = true;
	MarkDirty(hwnd, s);
}

int AutoWidth(HWND hwnd, ListView* s, int column, bool useHeader)
{
	HDC hdc = GetDC(hwnd);
	HGDIOBJ old = SelectObject(hdc, FontOf(s));
	int width = 0;
	for (const Item& item : s->items) width = std::max(width, TextWidth(hdc, item.Text(static_cast<size_t>(column))));
	if (column == 0) width += LabelLeft(s);
	if (useHeader) width = std::max(width, TextWidth(hdc, s->columns[column].text));
	SelectObject(hdc, old);
	ReleaseDC(hwnd, hdc);
	width += kTextPad * 2 + 2;
	if (useHeader && column == static_cast<int>(s->columns.size()) - 1) {
		// 最後の列は残りの幅を埋める
		RECT rc;
		GetClientRect(hwnd, &rc);
		int rest = rc.right;
		for (int i = 0; i < column; ++i) rest -= s->columns[i].width;
		width = std::max(width, rest);
	}
	return width;
}

void SortItems(HWND hwnd, ListView* s, PFNLVCOMPARE compare, LPARAM data)
{
	std::stable_sort(s->items.begin(), s->items.end(), [&](const Item& a, const Item& b) { return compare(a.param, b.param, data) < 0; });
	s->focus = -1;
	for (int i = 0; i < Count(s); ++i) {
		if (s->items[i].state & LVIS_FOCUSED) s->focus = i;
	}
	MarkDirty(hwnd, s);
}

int FindItem(const ListView* s, int start, const LVFINDINFOW* info)
{
	const int n = Count(s);
	for (int k = 1; k <= n; ++k) {
		const int i = start + k;
		if (i >= n && !(info->flags & LVFI_WRAP)) return -1;
		const Item& item = s->items[i % n];
		if (info->flags & LVFI_PARAM) {
			if (item.param == info->lParam) return i % n;
		} else if (info->flags & (LVFI_STRING | LVFI_PARTIAL)) {
			const size_t length = wcslen(info->psz);
			const bool hit = (info->flags & LVFI_PARTIAL) ? !_wcsnicmp(item.Text(0).c_str(), info->psz, length) : !_wcsicmp(item.Text(0).c_str(), info->psz);
			if (hit) return i % n;
		}
	}
	return -1;
}

POINT PointOf(LPARAM lParam) { return {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))}; }

//! NM_CLICK などの通知
LRESULT NotifyActivate(HWND hwnd, UINT code, int index, int subItem, POINT pt)
{
	NMITEMACTIVATE n{};
	n.iItem = index;
	n.iSubItem = subItem;
	n.ptAction = pt;
	if (GetKeyState(VK_SHIFT) & 0x8000) n.uKeyFlags |= LVKF_SHIFT;
	if (GetKeyState(VK_CONTROL) & 0x8000) n.uKeyFlags |= LVKF_CONTROL;
	return NotifyParent(hwnd, &n.hdr, code);
}

int HeaderGrip(HWND hwnd, const ListView* s, POINT pt)
{
	if (pt.y >= HeaderHeight(hwnd, s)) return -1;
	int x = -s->offset;
	for (size_t i = 0; i < s->columns.size(); ++i) {
		x += s->columns[i].width;
		if (std::abs(pt.x - x) <= kHeaderGrip) return static_cast<int>(i);
	}
	return -1;
}

void LeftDown(HWND hwnd, ListView* s, UINT msg, WPARAM keys, POINT pt)
{
	KillTimer(hwnd, kEditTimer);
	EndEdit(hwnd, s, true);
	if (GetFocus() != hwnd) SetFocus(hwnd);
	Sync(hwnd, s);
	if (IsReport(hwnd) && pt.y < HeaderHeight(hwnd, s)) {
		const int grip = HeaderGrip(hwnd, s, pt);
		if (grip >= 0) {
			s->resizing = grip;
			s->resizeStart = pt.x;
			s->resizeWidth = s->columns[grip].width;
			SetCapture(hwnd);
		} else if (!(Style(hwnd) & LVS_NOSORTHEADER)) {
			s->pressedColumn = ColumnAt(s, pt.x);
			SetCapture(hwnd);
		}
		return;
	}
	const int index = ItemAt(hwnd, s, pt);
	s->pendingEdit = -1;
	s->pressPoint = pt;
	if (index < 0) {
		if (!(keys & (MK_SHIFT | MK_CONTROL))) DeselectAll(hwnd, s);
		s->clickPending = true;
		SetCapture(hwnd);
		return;
	}
	const RECT check = CheckRect(ItemRect(hwnd, s, index), s);
	const bool checkHit = (s->exStyle & LVS_EX_CHECKBOXES) && PtInRect(&check, pt);
	if (checkHit && msg == WM_LBUTTONDOWN) {
		ToggleCheck(hwnd, s, index);
	} else {
		const bool wasCurrent = (s->items[index].state & (LVIS_SELECTED | LVIS_FOCUSED)) == (LVIS_SELECTED | LVIS_FOCUSED);
		SelectByUser(hwnd, s, index, (keys & MK_SHIFT) != 0, (keys & MK_CONTROL) != 0);
		if (wasCurrent && msg == WM_LBUTTONDOWN && (Style(hwnd) & LVS_EDITLABELS) && !(keys & (MK_SHIFT | MK_CONTROL))) s->pendingEdit = index;
	}
	s->clickPending = true;
	SetCapture(hwnd);
}

void LeftUp(HWND hwnd, ListView* s, POINT pt)
{
	if (GetCapture() == hwnd) ReleaseCapture();
	if (s->resizing >= 0) {
		s->resizing = -1;
		return;
	}
	if (s->pressedColumn >= 0) {
		const int column = s->pressedColumn;
		s->pressedColumn = -1;
		if (pt.y < HeaderHeight(hwnd, s) && ColumnAt(s, pt.x) == column) {
			NMLISTVIEW n{};
			n.iItem = -1;
			n.iSubItem = column;
			NotifyParent(hwnd, &n.hdr, LVN_COLUMNCLICK);
		}
		return;
	}
	if (s->clickPending) {
		s->clickPending = false;
		int sub = 0;
		const int index = ItemAt(hwnd, s, s->pressPoint, &sub);
		NotifyActivate(hwnd, NM_CLICK, index, sub, s->pressPoint);
		if (s->pendingEdit >= 0) SetTimer(hwnd, kEditTimer, GetDoubleClickTime(), nullptr);
	}
}

void RightDown(HWND hwnd, ListView* s, POINT pt)
{
	EndEdit(hwnd, s, true);
	if (GetFocus() != hwnd) SetFocus(hwnd);
	int sub = 0;
	const int index = ItemAt(hwnd, s, pt, &sub);
	if (index >= 0 && !(s->items[index].state & LVIS_SELECTED)) SelectByUser(hwnd, s, index, false, false);
	if (!NotifyActivate(hwnd, NM_RCLICK, index, sub, pt)) {
		POINT screen = pt;
		ClientToScreen(hwnd, &screen);
		SendMessageW(GetParent(hwnd), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(screen.x, screen.y));
	}
}

bool KeyDown(HWND hwnd, ListView* s, WPARAM key)
{
	NMLVKEYDOWN n{};
	n.wVKey = static_cast<WORD>(key);
	NotifyParent(hwnd, &n.hdr, LVN_KEYDOWN);
	if (!IsWindow(hwnd)) return true;
	const int count = Count(s);
	if (!count) return false;
	Sync(hwnd, s);
	const int page = RowsPerPage(hwnd, s);
	int index = s->focus >= 0 ? s->focus : 0;
	const bool report = IsReport(hwnd);
	switch (key) {
	case VK_UP: --index; break;
	case VK_DOWN: ++index; break;
	case VK_LEFT:
		if (report) { s->offset -= 20; MarkDirty(hwnd, s); return true; }
		index -= page;
		break;
	case VK_RIGHT:
		if (report) { s->offset += 20; MarkDirty(hwnd, s); return true; }
		index += page;
		break;
	case VK_PRIOR: index -= page; break;
	case VK_NEXT: index += page; break;
	case VK_HOME: index = 0; break;
	case VK_END: index = count - 1; break;
	case VK_SPACE:
		if ((s->exStyle & LVS_EX_CHECKBOXES) && s->focus >= 0) ToggleCheck(hwnd, s, s->focus);
		return true;
	case VK_RETURN: {
		NMHDR h{};
		NotifyParent(hwnd, &h, NM_RETURN);
		return true;
	}
	default:
		return false;
	}
	index = std::clamp(index, 0, count - 1);
	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	if (control && !shift) {
		SetState(hwnd, s, index, LVIS_FOCUSED, LVIS_FOCUSED);
		EnsureVisible(hwnd, s, index);
	} else {
		SelectByUser(hwnd, s, index, shift, false);
	}
	return true;
}

void CharKey(HWND hwnd, ListView* s, wchar_t c)
{
	if (c < L' ' || s->items.empty()) return;
	const wchar_t text[2] = {c, 0};
	LVFINDINFOW info{};
	info.flags = LVFI_PARTIAL | LVFI_WRAP;
	info.psz = text;
	const int found = FindItem(s, s->focus, &info);
	if (found >= 0) SelectByUser(hwnd, s, found, false, false);
}

}  // namespace

namespace winport {

LRESULT CALLBACK ListViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	ListView* s = StateOf(hwnd);
	if (msg == WM_NCCREATE) {
		auto w = GetWindowObject(hwnd);
		s = new ListView;
		w->control = s;
		s->border = (w->style & WS_BORDER) || (w->exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
		w->style &= ~WS_BORDER;
		w->exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
		if (!(w->style & LVS_NOSCROLL)) w->style |= WS_VSCROLL | WS_HSCROLL;
		s->rowHeight = FontLineHeight(DialogFont()) + 4;
		s->headerHeight = s->rowHeight + 3;
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	if (!s) return DefWindowProcW(hwnd, msg, wParam, lParam);
	const int index = static_cast<int>(wParam);
	const bool valid = index >= 0 && index < Count(s);
	switch (msg) {
	case WM_NCDESTROY:
		delete s;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	case WM_SETFONT:
		s->font = reinterpret_cast<HFONT>(wParam);
		s->rowHeight = FontLineHeight(FontOf(s)) + 4;
		s->headerHeight = s->rowHeight + 3;
		s->listWidthDirty = true;
		MarkDirty(hwnd, s);
		return 0;
	case WM_GETFONT: return reinterpret_cast<LRESULT>(s->font);
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE: Invalidate(hwnd); return 0;
	case WM_SIZE:
	case WM_STYLECHANGED:
		MarkDirty(hwnd, s);
		break;
	case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
	case WM_PAINT: Paint(hwnd, s); return 0;
	case WM_SETFOCUS:
	case WM_KILLFOCUS: {
		s->focused = msg == WM_SETFOCUS;
		Invalidate(hwnd);
		NMHDR h{};
		NotifyParent(hwnd, &h, s->focused ? NM_SETFOCUS : NM_KILLFOCUS);
		return 0;
	}
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		const POINT pt = PointOf(lParam);
		LeftDown(hwnd, s, msg, wParam, pt);
		if (msg == WM_LBUTTONDBLCLK && s->clickPending) {
			s->clickPending = false;
			if (GetCapture() == hwnd) ReleaseCapture();
			int sub = 0;
			const int hit = ItemAt(hwnd, s, pt, &sub);
			NotifyActivate(hwnd, NM_CLICK, hit, sub, pt);
			NotifyActivate(hwnd, NM_DBLCLK, hit, sub, pt);
		}
		return 0;
	}
	case WM_LBUTTONUP: LeftUp(hwnd, s, PointOf(lParam)); return 0;
	case WM_MOUSEMOVE:
		if (s->resizing >= 0) {
			s->columns[s->resizing].width = std::max(0, s->resizeWidth + PointOf(lParam).x - s->resizeStart);
			MarkDirty(hwnd, s);
		}
		return 0;
	case WM_RBUTTONDOWN: RightDown(hwnd, s, PointOf(lParam)); return 0;
	case WM_TIMER:
		if (wParam == kEditTimer) {
			KillTimer(hwnd, kEditTimer);
			const int item = s->pendingEdit;
			s->pendingEdit = -1;
			if (item >= 0) BeginEdit(hwnd, s, item);
			return 0;
		}
		break;
	case WM_KEYDOWN:
		if (KeyDown(hwnd, s, wParam)) return 0;
		break;
	case WM_CHAR:
		CharKey(hwnd, s, static_cast<wchar_t>(wParam));
		return 0;
	case WM_MOUSEWHEEL:
		if (IsReport(hwnd)) s->top += static_cast<short>(HIWORD(wParam)) > 0 ? -3 : 3;
		else s->offset += static_cast<short>(HIWORD(wParam)) > 0 ? -1 : 1;
		MarkDirty(hwnd, s);
		return 0;
	case WM_VSCROLL:
		switch (LOWORD(wParam)) {
		case SB_LINEUP: --s->top; break;
		case SB_LINEDOWN: ++s->top; break;
		case SB_PAGEUP: s->top -= RowsPerPage(hwnd, s); break;
		case SB_PAGEDOWN: s->top += RowsPerPage(hwnd, s); break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: s->top = static_cast<short>(HIWORD(wParam)); break;
		default: return 0;
		}
		MarkDirty(hwnd, s);
		return 0;
	case WM_HSCROLL: {
		const int step = IsReport(hwnd) ? 20 : 1;
		RECT rc;
		GetClientRect(hwnd, &rc);
		const int page = IsReport(hwnd) ? static_cast<int>(rc.right) : 1;
		switch (LOWORD(wParam)) {
		case SB_LINELEFT: s->offset -= step; break;
		case SB_LINERIGHT: s->offset += step; break;
		case SB_PAGELEFT: s->offset -= page; break;
		case SB_PAGERIGHT: s->offset += page; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: s->offset = static_cast<short>(HIWORD(wParam)); break;
		default: return 0;
		}
		MarkDirty(hwnd, s);
		return 0;
	}
	case kEndLabelEdit:
		EndEdit(hwnd, s, wParam != 0);
		return 0;

	case LVM_GETITEMCOUNT: return Count(s);
	case LVM_SETITEMCOUNT: s->items.reserve(static_cast<size_t>(std::max(index, 0))); return TRUE;
	case LVM_INSERTITEMW: return Insert(hwnd, s, reinterpret_cast<const LVITEMW*>(lParam));
	case LVM_DELETEITEM:
		if (!valid) return FALSE;
		Erase(hwnd, s, index);
		return TRUE;
	case LVM_DELETEALLITEMS:
		s->items.clear();
		s->focus = s->anchor = -1;
		s->top = 0;
		s->listWidthDirty = true;
		if (!IsReport(hwnd)) s->offset = 0;
		MarkDirty(hwnd, s);
		return TRUE;
	case LVM_GETITEMW: {
		auto* out = reinterpret_cast<LVITEMW*>(lParam);
		if (out->iItem < 0 || out->iItem >= Count(s)) return FALSE;
		const Item& item = s->items[out->iItem];
		if (out->mask & LVIF_TEXT) CopyText(item.Text(static_cast<size_t>(std::max(out->iSubItem, 0))), out->pszText, out->cchTextMax);
		if (out->mask & LVIF_IMAGE) out->iImage = item.image;
		if (out->mask & LVIF_PARAM) out->lParam = item.param;
		if (out->mask & LVIF_STATE) out->state = item.state & out->stateMask;
		return TRUE;
	}
	case LVM_SETITEMW: {
		const auto* in = reinterpret_cast<const LVITEMW*>(lParam);
		if (in->iItem < 0 || in->iItem >= Count(s)) return FALSE;
		ReadItem(hwnd, s, in->iItem, in);
		if (in->mask & LVIF_STATE) SetState(hwnd, s, in->iItem, in->state, in->stateMask);
		MarkDirty(hwnd, s);
		return TRUE;
	}
	case LVM_GETITEMTEXTW: {
		auto* out = reinterpret_cast<LVITEMW*>(lParam);
		if (!valid) return 0;
		const std::wstring& text = s->items[index].Text(static_cast<size_t>(std::max(out->iSubItem, 0)));
		CopyText(text, out->pszText, out->cchTextMax);
		return out->cchTextMax > 0 ? std::min(static_cast<int>(text.size()), out->cchTextMax - 1) : 0;
	}
	case LVM_SETITEMTEXTW: {
		if (!valid) return FALSE;
		LVITEMW in = *reinterpret_cast<const LVITEMW*>(lParam);
		in.mask = LVIF_TEXT;
		ReadItem(hwnd, s, index, &in);
		MarkDirty(hwnd, s);
		return TRUE;
	}
	case LVM_SETITEMSTATE: {
		const auto* in = reinterpret_cast<const LVITEMW*>(lParam);
		if (index == -1) {
			for (int i = 0; i < Count(s); ++i) SetState(hwnd, s, i, in->state, in->stateMask);
			return TRUE;
		}
		if (!valid) return FALSE;
		SetState(hwnd, s, index, in->state, in->stateMask);
		return TRUE;
	}
	case LVM_GETITEMSTATE: return valid ? s->items[index].state & static_cast<UINT>(lParam) : 0;
	case LVM_GETNEXTITEM: {
		UINT wanted = 0;
		if (LOWORD(lParam) & LVNI_SELECTED) wanted |= LVIS_SELECTED;
		if (LOWORD(lParam) & LVNI_FOCUSED) wanted |= LVIS_FOCUSED;
		for (int i = std::max(index + 1, 0); i < Count(s); ++i) {
			if ((s->items[i].state & wanted) == wanted) return i;
		}
		return -1;
	}
	case LVM_GETSELECTEDCOUNT:
		return std::count_if(s->items.begin(), s->items.end(), [](const Item& i) { return (i.state & LVIS_SELECTED) != 0; });
	case LVM_INSERTCOLUMNW: {
		const auto* in = reinterpret_cast<const LVCOLUMNW*>(lParam);
		const int at = std::clamp(index, 0, static_cast<int>(s->columns.size()));
		Column column;
		if (in->mask & LVCF_TEXT) column.text = in->pszText ? in->pszText : L"";
		if (in->mask & LVCF_WIDTH) column.width = in->cx;
		if (in->mask & LVCF_FMT) column.fmt = in->fmt;
		s->columns.insert(s->columns.begin() + at, std::move(column));
		if (at + 1 < static_cast<int>(s->columns.size())) {
			for (Item& item : s->items) {
				if (static_cast<size_t>(at) < item.text.size()) item.text.insert(item.text.begin() + at, L"");
			}
		}
		MarkDirty(hwnd, s);
		return at;
	}
	case LVM_DELETECOLUMN:
		if (index < 0 || index >= static_cast<int>(s->columns.size())) return FALSE;
		s->columns.erase(s->columns.begin() + index);
		for (Item& item : s->items) {
			if (static_cast<size_t>(index) < item.text.size()) item.text.erase(item.text.begin() + index);
		}
		MarkDirty(hwnd, s);
		return TRUE;
	case LVM_GETCOLUMNW: {
		auto* out = reinterpret_cast<LVCOLUMNW*>(lParam);
		if (index < 0 || index >= static_cast<int>(s->columns.size())) return FALSE;
		const Column& column = s->columns[index];
		if (out->mask & LVCF_TEXT) CopyText(column.text, out->pszText, out->cchTextMax);
		if (out->mask & LVCF_WIDTH) out->cx = column.width;
		if (out->mask & LVCF_FMT) out->fmt = column.fmt;
		if (out->mask & LVCF_SUBITEM) out->iSubItem = index;
		return TRUE;
	}
	case LVM_SETCOLUMNW: {
		const auto* in = reinterpret_cast<const LVCOLUMNW*>(lParam);
		if (index < 0 || index >= static_cast<int>(s->columns.size())) return FALSE;
		Column& column = s->columns[index];
		if (in->mask & LVCF_TEXT) column.text = in->pszText ? in->pszText : L"";
		if (in->mask & LVCF_WIDTH) column.width = in->cx;
		if (in->mask & LVCF_FMT) column.fmt = in->fmt;
		MarkDirty(hwnd, s);
		return TRUE;
	}
	case LVM_GETCOLUMNWIDTH:
		if (!IsReport(hwnd)) return ListColumnWidth(hwnd, s);
		return index >= 0 && index < static_cast<int>(s->columns.size()) ? s->columns[index].width : 0;
	case LVM_SETCOLUMNWIDTH: {
		const int width = static_cast<short>(LOWORD(lParam));
		if (!IsReport(hwnd)) {
			s->listWidth = std::max(width, 0);
		} else {
			if (index < 0 || index >= static_cast<int>(s->columns.size())) return FALSE;
			s->columns[index].width = width == LVSCW_AUTOSIZE ? AutoWidth(hwnd, s, index, false)
			                        : width == LVSCW_AUTOSIZE_USEHEADER ? AutoWidth(hwnd, s, index, true) : width;
		}
		MarkDirty(hwnd, s);
		return TRUE;
	}
	case LVM_GETEXTENDEDLISTVIEWSTYLE: return s->exStyle;
	case LVM_SETEXTENDEDLISTVIEWSTYLE: {
		const DWORD previous = s->exStyle;
		const DWORD mask = wParam ? static_cast<DWORD>(wParam) : 0xFFFFFFFF;
		s->exStyle = (s->exStyle & ~mask) | (static_cast<DWORD>(lParam) & mask);
		if ((s->exStyle & ~previous) & LVS_EX_CHECKBOXES) {
			for (Item& item : s->items) {
				if (!(item.state & LVIS_STATEIMAGEMASK)) item.state |= kUncheckedState;
			}
		}
		MarkDirty(hwnd, s);
		return previous;
	}
	case LVM_ENSUREVISIBLE: EnsureVisible(hwnd, s, index); return TRUE;
	case LVM_GETTOPINDEX: Sync(hwnd, s); return IsReport(hwnd) ? s->top : s->offset * RowsPerPage(hwnd, s);
	case LVM_GETCOUNTPERPAGE: return RowsPerPage(hwnd, s);
	case LVM_SCROLL:
		if (IsReport(hwnd)) {
			s->offset += index;
			s->top += static_cast<int>(lParam) / s->rowHeight;
		} else {
			s->offset += index / ListColumnWidth(hwnd, s);
		}
		MarkDirty(hwnd, s);
		return TRUE;
	case LVM_GETITEMRECT: {
		if (!valid) return FALSE;
		Sync(hwnd, s);
		auto* out = reinterpret_cast<RECT*>(lParam);
		const int code = out->left;
		*out = ItemRect(hwnd, s, index);
		if (IsReport(hwnd) && !s->columns.empty() && (code == LVIR_LABEL || code == LVIR_SELECTBOUNDS)) out->right = out->left + s->columns[0].width;
		if (code == LVIR_LABEL) out->left += LabelLeft(s);
		return TRUE;
	}
	case LVM_SUBITEMHITTEST:
	case LVM_HITTEST: {
		auto* info = reinterpret_cast<LVHITTESTINFO*>(lParam);
		int sub = 0;
		info->iItem = ItemAt(hwnd, s, info->pt, msg == LVM_SUBITEMHITTEST ? &sub : nullptr);
		info->iSubItem = sub;
		info->flags = info->iItem >= 0 ? LVHT_ONITEMLABEL : LVHT_NOWHERE;
		return info->iItem;
	}
	case LVM_FINDITEMW: return FindItem(s, index, reinterpret_cast<const LVFINDINFOW*>(lParam));
	case LVM_SORTITEMS: SortItems(hwnd, s, reinterpret_cast<PFNLVCOMPARE>(lParam), static_cast<LPARAM>(wParam)); return TRUE;
	case LVM_EDITLABELW: return reinterpret_cast<LRESULT>(BeginEdit(hwnd, s, index));
	case LVM_GETEDITCONTROL: return reinterpret_cast<LRESULT>(s->edit);
	case LVM_CANCELEDITLABEL: EndEdit(hwnd, s, false); return 0;
	case LVM_SETTEXTCOLOR: s->textColor = static_cast<COLORREF>(lParam); Invalidate(hwnd); return TRUE;
	case LVM_GETTEXTCOLOR: return s->textColor;
	case LVM_SETTEXTBKCOLOR: s->textBack = static_cast<COLORREF>(lParam); Invalidate(hwnd); return TRUE;
	case LVM_GETTEXTBKCOLOR: return s->textBack;
	case LVM_SETBKCOLOR: s->back = static_cast<COLORREF>(lParam); Invalidate(hwnd); return TRUE;
	case LVM_GETBKCOLOR: return s->back;
	case LVM_SETIMAGELIST: {
		if (index < 0 || index > 2) return 0;
		HIMAGELIST previous = s->images[index];
		s->images[index] = reinterpret_cast<HIMAGELIST>(lParam);
		MarkDirty(hwnd, s);
		return reinterpret_cast<LRESULT>(previous);
	}
	case LVM_GETIMAGELIST: return index >= 0 && index <= 2 ? reinterpret_cast<LRESULT>(s->images[index]) : 0;
	case LVM_GETSTRINGWIDTHW: {
		HDC hdc = GetDC(hwnd);
		HGDIOBJ old = SelectObject(hdc, FontOf(s));
		const int width = TextWidth(hdc, reinterpret_cast<LPCWSTR>(lParam));
		SelectObject(hdc, old);
		ReleaseDC(hwnd, hdc);
		return width;
	}
	case LVM_REDRAWITEMS:
	case LVM_UPDATE:
	case LVM_ARRANGE:
		Invalidate(hwnd);
		return TRUE;
	case LVM_GETHEADER:
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace winport
