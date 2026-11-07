/*
	control_treeview.cpp — ツリービュー（SysTreeView32）

	HTREEITEM は Node へのポインタ。削除済みのハンドルが渡っても落ちないように、有効な Node の集合を持つ。
	表示中の項目の一覧は変更のたびに作らず、描画や当たり判定の直前に作り直す（アウトライン解析は数千項目を一度に入れる）。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <unordered_set>
#include <vector>

#include "control_common.h"
#include "dialog_internal.h"
#include "user32_internal.h"

namespace {

using namespace winport;

constexpr UINT_PTR kEditTimer = 0x5456;
constexpr int kButtonSize = 9;

struct Node {
	Node* parent = nullptr;
	Node* first = nullptr;
	Node* last = nullptr;
	Node* next = nullptr;
	Node* prev = nullptr;
	std::wstring text;
	UINT state = 0;
	LPARAM param = 0;
	int image = -1;
	int selectedImage = -1;
	int children = 0;	//!< TVIF_CHILDREN で指定された値
	int level = 0;
	int width = -1;		//!< 文字列の幅（-1 は未計測）
};

struct TreeView {
	Node root;
	std::unordered_set<Node*> nodes;
	std::vector<Node*> visible;
	Node* selected = nullptr;
	Node* hot = nullptr;		//!< TVGN_DROPHILITE
	int top = 0;
	int offset = 0;
	int rowHeight = 18;
	int indent = 19;
	int contentWidth = 0;
	HFONT font = nullptr;
	COLORREF textColor = CLR_DEFAULT;
	COLORREF back = CLR_DEFAULT;
	HIMAGELIST images = nullptr;
	bool border = false;
	bool focused = false;
	bool dirty = true;
	bool syncing = false;
	HWND edit = nullptr;
	Node* editNode = nullptr;
	bool ending = false;
	Node* pendingEdit = nullptr;
	Node* pressed = nullptr;
	POINT pressPoint{};
	bool clickPending = false;
};

TreeView* StateOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? static_cast<TreeView*>(w->control) : nullptr;
}

DWORD Style(HWND hwnd) { return static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)); }
HFONT FontOf(const TreeView* s) { return s->font ? s->font : DialogFont(); }
void Invalidate(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void MarkDirty(HWND hwnd, TreeView* s)
{
	s->dirty = true;
	Invalidate(hwnd);
}

Node* Find(TreeView* s, HTREEITEM h)
{
	auto n = reinterpret_cast<Node*>(h);
	return s->nodes.count(n) ? n : nullptr;
}

HTREEITEM ToHandle(Node* n) { return reinterpret_cast<HTREEITEM>(n); }
bool HasButton(const Node* n) { return n->first || n->children != 0; }
bool IsExpanded(const Node* n) { return (n->state & TVIS_EXPANDED) != 0; }

void CollectVisible(TreeView* s, Node* parent)
{
	for (Node* n = parent->first; n; n = n->next) {
		s->visible.push_back(n);
		if (n->first && IsExpanded(n)) CollectVisible(s, n);
	}
}

int RowsPerPage(HWND hwnd, const TreeView* s)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	return std::max(1, static_cast<int>(rc.bottom) / s->rowHeight);
}

//! 項目の文字列の左端（送り量を引く前）
int LabelLeft(HWND hwnd, const TreeView* s, const Node* n)
{
	const int base = (Style(hwnd) & TVS_LINESATROOT) ? 1 : 0;
	int x = (n->level + base) * s->indent + 4;
	if (s->images) {
		int cx = 0, cy = 0;
		ImageList_GetIconSize(s->images, &cx, &cy);
		x += cx + 2;
	}
	return x;
}

void Sync(HWND hwnd, TreeView* s)
{
	if (!s->dirty || s->syncing) return;
	s->syncing = true;
	s->visible.clear();
	CollectVisible(s, &s->root);
	HDC hdc = GetDC(hwnd);
	HGDIOBJ old = SelectObject(hdc, FontOf(s));
	s->contentWidth = 0;
	for (Node* n : s->visible) {
		if (n->width < 0) n->width = TextWidth(hdc, n->text);
		s->contentWidth = std::max(s->contentWidth, LabelLeft(hwnd, s, n) + n->width + 8);
	}
	SelectObject(hdc, old);
	ReleaseDC(hwnd, hdc);
	const int count = static_cast<int>(s->visible.size());
	for (int pass = 0; pass < 3; ++pass) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		const int before = rc.right * 65536 + rc.bottom;
		const int page = RowsPerPage(hwnd, s);
		s->top = std::clamp(s->top, 0, std::max(0, count - page));
		s->offset = std::clamp(s->offset, 0, std::max(0, s->contentWidth - static_cast<int>(rc.right)));
		SCROLLINFO v{sizeof(v), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, count - 1), static_cast<UINT>(page), s->top};
		SetScrollInfo(hwnd, SB_VERT, &v, TRUE);
		SCROLLINFO h{sizeof(h), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, s->contentWidth - 1), static_cast<UINT>(rc.right), s->offset};
		SetScrollInfo(hwnd, SB_HORZ, &h, TRUE);
		GetClientRect(hwnd, &rc);
		if (before == rc.right * 65536 + rc.bottom) break;
	}
	s->dirty = false;
	s->syncing = false;
}

int VisibleIndex(HWND hwnd, TreeView* s, const Node* n)
{
	Sync(hwnd, s);
	const auto it = std::find(s->visible.begin(), s->visible.end(), n);
	return it == s->visible.end() ? -1 : static_cast<int>(it - s->visible.begin());
}

void ExpandAncestors(TreeView* s, Node* n)
{
	for (Node* p = n->parent; p && p != &s->root; p = p->parent) {
		if (!IsExpanded(p)) {
			p->state |= TVIS_EXPANDED | TVIS_EXPANDEDONCE;
			s->dirty = true;
		}
	}
}

void EnsureVisible(HWND hwnd, TreeView* s, Node* n)
{
	ExpandAncestors(s, n);
	const int index = VisibleIndex(hwnd, s, n);
	if (index < 0) return;
	const int page = RowsPerPage(hwnd, s);
	if (index < s->top) s->top = index;
	else if (index >= s->top + page) s->top = index - page + 1;
	MarkDirty(hwnd, s);
}

void FillTreeItem(TVITEMW* out, const Node* n)
{
	out->mask = TVIF_HANDLE | TVIF_PARAM | TVIF_STATE;
	out->hItem = ToHandle(const_cast<Node*>(n));
	out->state = n->state;
	out->lParam = n->param;
}

//! 選択を変える。親が TVN_SELCHANGING で止めたら false
bool Select(HWND hwnd, TreeView* s, Node* n, UINT action)
{
	if (n == s->selected) {
		if (n) EnsureVisible(hwnd, s, n);
		return true;
	}
	NMTREEVIEWW info{};
	info.action = action;
	if (s->selected) FillTreeItem(&info.itemOld, s->selected);
	if (n) FillTreeItem(&info.itemNew, n);
	if (NotifyParent(hwnd, &info.hdr, TVN_SELCHANGINGW)) return false;
	if (s->selected) s->selected->state &= ~TVIS_SELECTED;
	s->selected = n;
	if (n) {
		n->state |= TVIS_SELECTED;
		EnsureVisible(hwnd, s, n);
	}
	Invalidate(hwnd);
	NotifyParent(hwnd, &info.hdr, TVN_SELCHANGEDW);
	return true;
}

bool IsDescendant(const TreeView* s, const Node* n, const Node* ancestor)
{
	for (const Node* p = n ? n->parent : nullptr; p && p != &s->root; p = p->parent) {
		if (p == ancestor) return true;
	}
	return false;
}

//! 開閉する。byUser のときと、初めて開くときだけ親へ通知する（Win32 と同じ）
bool Expand(HWND hwnd, TreeView* s, Node* n, UINT action, bool byUser)
{
	const UINT what = action & 0x0003;
	const bool open = what == TVE_TOGGLE ? !IsExpanded(n) : what == TVE_EXPAND;
	if (open == IsExpanded(n)) return false;
	if (open && !HasButton(n)) return false;
	const bool notify = byUser || (open && !(n->state & TVIS_EXPANDEDONCE));
	NMTREEVIEWW info{};
	info.action = open ? TVE_EXPAND : TVE_COLLAPSE;
	FillTreeItem(&info.itemNew, n);
	if (notify && NotifyParent(hwnd, &info.hdr, TVN_ITEMEXPANDINGW)) return false;
	if (!s->nodes.count(n)) return false;
	if (open) n->state |= TVIS_EXPANDED | TVIS_EXPANDEDONCE;
	else n->state &= ~TVIS_EXPANDED;
	if (!open && IsDescendant(s, s->selected, n)) Select(hwnd, s, n, TVC_UNKNOWN);
	MarkDirty(hwnd, s);
	if (notify) {
		FillTreeItem(&info.itemNew, n);
		NotifyParent(hwnd, &info.hdr, TVN_ITEMEXPANDEDW);
	}
	return true;
}

void Link(Node* parent, Node* after, Node* n)
{
	n->parent = parent;
	n->prev = after;
	n->next = after ? after->next : parent->first;
	if (n->prev) n->prev->next = n; else parent->first = n;
	if (n->next) n->next->prev = n; else parent->last = n;
}

void Unlink(Node* n)
{
	Node* parent = n->parent;
	if (n->prev) n->prev->next = n->next; else parent->first = n->next;
	if (n->next) n->next->prev = n->prev; else parent->last = n->prev;
	n->prev = n->next = nullptr;
}

void ReadItem(Node* n, const TVITEMW* in)
{
	if (in->mask & TVIF_TEXT) {
		n->text = in->pszText && in->pszText != LPSTR_TEXTCALLBACKW ? in->pszText : L"";
		n->width = -1;
	}
	if (in->mask & TVIF_PARAM) n->param = in->lParam;
	if (in->mask & TVIF_IMAGE) n->image = in->iImage;
	if (in->mask & TVIF_SELECTEDIMAGE) n->selectedImage = in->iSelectedImage;
	if (in->mask & TVIF_CHILDREN) n->children = in->cChildren;
	if (in->mask & TVIF_STATE) n->state = (n->state & ~in->stateMask) | (in->state & in->stateMask & ~static_cast<UINT>(TVIS_SELECTED));
}

Node* Insert(HWND hwnd, TreeView* s, const TVINSERTSTRUCTW* in)
{
	Node* parent = &s->root;
	if (in->hParent && in->hParent != TVI_ROOT) {
		parent = Find(s, in->hParent);
		if (!parent) return nullptr;
	}
	auto n = new Node;
	ReadItem(n, &in->item);
	n->level = parent == &s->root ? 0 : parent->level + 1;
	Node* after = parent->last;
	if (in->hInsertAfter == TVI_FIRST) {
		after = nullptr;
	} else if (in->hInsertAfter == TVI_SORT) {
		after = nullptr;
		for (Node* c = parent->first; c && _wcsicmp(c->text.c_str(), n->text.c_str()) <= 0; c = c->next) after = c;
	} else if (in->hInsertAfter && in->hInsertAfter != TVI_LAST) {
		if (Node* a = Find(s, in->hInsertAfter); a && a->parent == parent) after = a;
	}
	Link(parent, after, n);
	s->nodes.insert(n);
	MarkDirty(hwnd, s);
	return n;
}

void Delete(HWND hwnd, TreeView* s, Node* n)
{
	while (n->first) Delete(hwnd, s, n->first);
	NMTREEVIEWW info{};
	FillTreeItem(&info.itemOld, n);
	NotifyParent(hwnd, &info.hdr, TVN_DELETEITEMW);
	if (s->hot == n) s->hot = nullptr;
	if (s->pendingEdit == n) s->pendingEdit = nullptr;
	if (s->pressed == n) s->pressed = nullptr;
	Unlink(n);
	s->nodes.erase(n);
	delete n;
}

void DeleteTree(HWND hwnd, TreeView* s, Node* n)
{
	if (s->selected && (s->selected == n || IsDescendant(s, s->selected, n))) {
		Node* next = n->next ? n->next : n->prev ? n->prev : (n->parent != &s->root ? n->parent : nullptr);
		if (!Select(hwnd, s, next, TVC_UNKNOWN) || s->selected == n) {
			s->selected = nullptr;
		}
	}
	Delete(hwnd, s, n);
	MarkDirty(hwnd, s);
}

void DeleteAll(HWND hwnd, TreeView* s)
{
	s->selected = nullptr;
	while (s->root.first) Delete(hwnd, s, s->root.first);
	s->top = s->offset = 0;
	MarkDirty(hwnd, s);
}

void SortChildren(HWND hwnd, TreeView* s, Node* parent, PFNTVCOMPARE compare, LPARAM data, bool recurse)
{
	std::vector<Node*> list;
	for (Node* c = parent->first; c; c = c->next) list.push_back(c);
	if (compare) std::stable_sort(list.begin(), list.end(), [&](Node* a, Node* b) { return compare(a->param, b->param, data) < 0; });
	else std::stable_sort(list.begin(), list.end(), [](Node* a, Node* b) { return _wcsicmp(a->text.c_str(), b->text.c_str()) < 0; });
	parent->first = parent->last = nullptr;
	Node* previous = nullptr;
	for (Node* c : list) {
		c->prev = previous;
		c->next = nullptr;
		if (previous) previous->next = c; else parent->first = c;
		previous = c;
		if (recurse) SortChildren(hwnd, s, c, compare, data, true);
	}
	parent->last = previous;
	MarkDirty(hwnd, s);
}

Node* NextItem(HWND hwnd, TreeView* s, UINT code, Node* n)
{
	switch (code) {
	case TVGN_ROOT: return s->root.first;
	case TVGN_CARET: return s->selected;
	case TVGN_DROPHILITE: return s->hot;
	case TVGN_FIRSTVISIBLE:
		Sync(hwnd, s);
		return s->top < static_cast<int>(s->visible.size()) ? s->visible[s->top] : nullptr;
	case TVGN_LASTVISIBLE:
		Sync(hwnd, s);
		return s->visible.empty() ? nullptr : s->visible.back();
	default:
		break;
	}
	if (!n) return code == TVGN_CHILD ? s->root.first : nullptr;
	switch (code) {
	case TVGN_NEXT: return n->next;
	case TVGN_PREVIOUS: return n->prev;
	case TVGN_PARENT: return n->parent == &s->root ? nullptr : n->parent;
	case TVGN_CHILD: return n->first;
	case TVGN_NEXTVISIBLE:
	case TVGN_PREVIOUSVISIBLE: {
		const int index = VisibleIndex(hwnd, s, n) + (code == TVGN_NEXTVISIBLE ? 1 : -1);
		return index >= 0 && index < static_cast<int>(s->visible.size()) ? s->visible[index] : nullptr;
	}
	default:
		return nullptr;
	}
}

// ---- 当たり判定 ----
Node* NodeAtY(HWND hwnd, TreeView* s, int y)
{
	Sync(hwnd, s);
	if (y < 0) return nullptr;
	const int index = s->top + y / s->rowHeight;
	return index < static_cast<int>(s->visible.size()) ? s->visible[index] : nullptr;
}

RECT ButtonRect(HWND hwnd, const TreeView* s, const Node* n, int y)
{
	const int base = (Style(hwnd) & TVS_LINESATROOT) ? 1 : 0;
	const int cx = (n->level + base) * s->indent - s->indent / 2 + 2 - s->offset;
	const int cy = y + s->rowHeight / 2;
	return {cx - kButtonSize / 2, cy - kButtonSize / 2, cx + kButtonSize / 2 + 1, cy + kButtonSize / 2 + 1};
}

UINT HitFlags(HWND hwnd, TreeView* s, Node* n, POINT pt)
{
	const int y = (pt.y / s->rowHeight) * s->rowHeight;
	if (HasButton(n) && (Style(hwnd) & TVS_HASBUTTONS)) {
		RECT button = ButtonRect(hwnd, s, n, y);
		InflateRect(&button, 3, 3);
		if (PtInRect(&button, pt)) return TVHT_ONITEMBUTTON;
	}
	const int left = LabelLeft(hwnd, s, n) - s->offset;
	if (pt.x < left) return TVHT_ONITEMINDENT;
	if (pt.x < left + n->width + 8) return TVHT_ONITEMLABEL;
	return TVHT_ONITEMRIGHT;
}

// ---- ラベル編集 ----
HWND BeginEdit(HWND hwnd, TreeView* s, Node* n)
{
	if (s->edit || !n) return s->edit;
	EnsureVisible(hwnd, s, n);
	Sync(hwnd, s);
	RECT client;
	GetClientRect(hwnd, &client);
	const int y = (VisibleIndex(hwnd, s, n) - s->top) * s->rowHeight;
	const int left = LabelLeft(hwnd, s, n) - s->offset;
	const RECT rc{left, y - 1, std::min<int>(client.right - 2, left + std::max(n->width + 40, 100)), y + s->rowHeight + 1};
	s->editNode = n;
	s->edit = CreateLabelEdit(hwnd, rc, n->text, FontOf(s));
	NMTVDISPINFOW info{};
	info.item.mask = TVIF_HANDLE | TVIF_PARAM | TVIF_STATE | TVIF_TEXT;
	info.item.hItem = ToHandle(n);
	info.item.lParam = n->param;
	info.item.state = n->state;
	std::wstring text = n->text;
	info.item.pszText = text.data();
	info.item.cchTextMax = static_cast<int>(text.size()) + 1;
	if (NotifyParent(hwnd, &info.hdr, TVN_BEGINLABELEDITW)) {
		CloseLabelEdit(hwnd, s->edit);
		s->edit = nullptr;
		s->editNode = nullptr;
		return nullptr;
	}
	if (s->edit) SetFocus(s->edit);
	return s->edit;
}

void EndEdit(HWND hwnd, TreeView* s, bool save)
{
	if (!s->edit || s->ending) return;
	s->ending = true;
	HWND edit = s->edit;
	Node* n = s->nodes.count(s->editNode) ? s->editNode : nullptr;
	const bool hadFocus = GetFocus() == edit;
	const int length = GetWindowTextLengthW(edit);
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	GetWindowTextW(edit, text.data(), length + 1);
	NMTVDISPINFOW info{};
	info.item.mask = TVIF_HANDLE | TVIF_PARAM | TVIF_TEXT;
	info.item.hItem = ToHandle(n);
	info.item.pszText = save ? text.data() : nullptr;
	info.item.cchTextMax = length + 1;
	if (n) info.item.lParam = n->param;
	const LRESULT accepted = NotifyParent(hwnd, &info.hdr, TVN_ENDLABELEDITW);
	if (save && accepted && s->nodes.count(n)) {
		n->text = text.c_str();
		n->width = -1;
	}
	s->edit = nullptr;
	s->editNode = nullptr;
	s->ending = false;
	CloseLabelEdit(hwnd, edit);
	if (hadFocus) SetFocus(hwnd);
	MarkDirty(hwnd, s);
}

// ---- 描画 ----
void DrawDots(HDC hdc, int x1, int y1, int x2, int y2)
{
	if (y1 == y2) {
		for (int x = x1; x < x2; x += 2) SetPixel(hdc, x, y1, RGB(160, 160, 160));
	} else {
		for (int y = y1; y < y2; y += 2) SetPixel(hdc, x1, y, RGB(160, 160, 160));
	}
}

void DrawNode(HWND hwnd, TreeView* s, HDC hdc, Node* n, int y, const RECT& client, bool itemDraw)
{
	const DWORD style = Style(hwnd);
	const bool enabled = IsWindowEnabled(hwnd) != FALSE;
	const int base = (style & TVS_LINESATROOT) ? 1 : 0;
	const int mid = y + s->rowHeight / 2;
	const int bottom = y + s->rowHeight;
	if ((style & TVS_HASLINES) && n->level + base > 0) {
		const int x = (n->level + base) * s->indent - s->indent / 2 + 2 - s->offset;
		DrawDots(hdc, x, n->prev || n->parent != &s->root ? y : mid, x, n->next ? bottom : mid);
		DrawDots(hdc, x, mid, x + s->indent / 2 + 1, mid);
		int level = n->level + base - 1;
		for (const Node* p = n->parent; p && p != &s->root && level > 0; p = p->parent, --level) {
			if (p->next) {
				const int px = level * s->indent - s->indent / 2 + 2 - s->offset;
				DrawDots(hdc, px, y, px, bottom);
			}
		}
	}
	if (HasButton(n) && (style & TVS_HASBUTTONS) && n->level + base > 0) {
		const RECT b = ButtonRect(hwnd, s, n, y);
		FillColor(hdc, b, RGB(255, 255, 255));
		FrameColor(hdc, b, RGB(130, 130, 130));
		LineColor(hdc, b.left + 2, mid, b.right - 2, mid, RGB(0, 0, 0));
		if (!IsExpanded(n)) LineColor(hdc, (b.left + b.right - 1) / 2, b.top + 2, (b.left + b.right - 1) / 2, b.bottom - 2, RGB(0, 0, 0));
	}
	int left = LabelLeft(hwnd, s, n) - s->offset;
	if (s->images) {
		int cx = 0, cy = 0;
		ImageList_GetIconSize(s->images, &cx, &cy);
		const int image = (n->state & TVIS_SELECTED) && n->selectedImage >= 0 ? n->selectedImage : n->image;
		if (image >= 0) ImageList_Draw(s->images, image, hdc, left - cx - 2, y + (s->rowHeight - cy) / 2, ILD_TRANSPARENT);
	}
	const bool selected = n == s->selected && (s->focused || (style & TVS_SHOWSELALWAYS));
	COLORREF text = !enabled ? kControlDisabledText : (s->textColor != CLR_DEFAULT ? s->textColor : RGB(0, 0, 0));
	COLORREF back = CLR_NONE;
	if (selected || n == s->hot) {
		back = s->focused || n == s->hot ? kControlAccent : kControlInactive;
		if (s->focused || n == s->hot) text = RGB(255, 255, 255);
	}
	if (itemDraw) {
		NMTVCUSTOMDRAW cd{};
		cd.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
		cd.nmcd.hdc = hdc;
		cd.nmcd.rc = {0, y, client.right, bottom};
		cd.nmcd.dwItemSpec = reinterpret_cast<DWORD_PTR>(n);
		cd.nmcd.uItemState = selected ? CDIS_SELECTED : 0;
		cd.nmcd.lItemlParam = n->param;
		cd.clrText = text;
		cd.clrTextBk = back;
		NotifyParent(hwnd, &cd.nmcd.hdr, NM_CUSTOMDRAW);
		text = cd.clrText;
		back = cd.clrTextBk;
	}
	RECT label{left - 2, y, left + n->width + 4, bottom};
	if (back != CLR_NONE) FillColor(hdc, label, back);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, text);
	label.left += 2;
	DrawTextW(hdc, n->text.c_str(), static_cast<int>(n->text.size()), &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_NOCLIP);
}

void Paint(HWND hwnd, TreeView* s)
{
	Sync(hwnd, s);
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);
	RECT client;
	GetClientRect(hwnd, &client);
	HGDIOBJ oldFont = SelectObject(hdc, FontOf(s));
	const bool enabled = IsWindowEnabled(hwnd) != FALSE;
	FillColor(hdc, client, s->back != CLR_DEFAULT ? s->back : (enabled ? RGB(255, 255, 255) : RGB(245, 245, 245)));
	NMTVCUSTOMDRAW cd{};
	cd.nmcd.dwDrawStage = CDDS_PREPAINT;
	cd.nmcd.hdc = hdc;
	cd.nmcd.rc = client;
	const bool itemDraw = (NotifyParent(hwnd, &cd.nmcd.hdr, NM_CUSTOMDRAW) & CDRF_NOTIFYITEMDRAW) != 0;
	const int last = std::min(static_cast<int>(s->visible.size()), s->top + static_cast<int>(client.bottom) / s->rowHeight + 2);
	for (int i = s->top; i < last; ++i) DrawNode(hwnd, s, hdc, s->visible[i], (i - s->top) * s->rowHeight, client, itemDraw);
	if (s->border) FrameColor(hdc, client, s->focused ? kControlAccent : kControlBorder);
	SelectObject(hdc, oldFont);
	EndPaint(hwnd, &ps);
}

POINT PointOf(LPARAM lParam) { return {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))}; }

void LeftDown(HWND hwnd, TreeView* s, POINT pt, bool doubleClick)
{
	KillTimer(hwnd, kEditTimer);
	EndEdit(hwnd, s, true);
	if (GetFocus() != hwnd) SetFocus(hwnd);
	s->pendingEdit = nullptr;
	Node* n = NodeAtY(hwnd, s, pt.y);
	s->pressPoint = pt;
	s->clickPending = true;
	if (!n) return;
	const UINT flags = HitFlags(hwnd, s, n, pt);
	if (flags == TVHT_ONITEMBUTTON) {
		Expand(hwnd, s, n, TVE_TOGGLE, true);
		return;
	}
	if (flags == TVHT_ONITEMLABEL || (Style(hwnd) & TVS_FULLROWSELECT)) {
		const bool wasSelected = n == s->selected;
		Select(hwnd, s, n, TVC_BYMOUSE);
		if (wasSelected && !doubleClick && (Style(hwnd) & TVS_EDITLABELS)) s->pendingEdit = n;
	}
}

void KeyDown(HWND hwnd, TreeView* s, WPARAM key)
{
	NMTVKEYDOWN info{};
	info.wVKey = static_cast<WORD>(key);
	NotifyParent(hwnd, &info.hdr, TVN_KEYDOWN);
	if (!IsWindow(hwnd)) return;
	Sync(hwnd, s);
	if (s->visible.empty()) return;
	Node* n = s->selected;
	const int count = static_cast<int>(s->visible.size());
	const int index = n ? VisibleIndex(hwnd, s, n) : 0;
	Node* target = nullptr;
	switch (key) {
	case VK_UP: target = s->visible[std::max(index - 1, 0)]; break;
	case VK_DOWN: target = s->visible[std::min(index + 1, count - 1)]; break;
	case VK_PRIOR: target = s->visible[std::max(index - RowsPerPage(hwnd, s), 0)]; break;
	case VK_NEXT: target = s->visible[std::min(index + RowsPerPage(hwnd, s), count - 1)]; break;
	case VK_HOME: target = s->visible.front(); break;
	case VK_END: target = s->visible.back(); break;
	case VK_LEFT:
		if (n && HasButton(n) && IsExpanded(n)) Expand(hwnd, s, n, TVE_COLLAPSE, true);
		else if (n && n->parent != &s->root) target = n->parent;
		break;
	case VK_RIGHT:
		if (n && HasButton(n) && !IsExpanded(n)) Expand(hwnd, s, n, TVE_EXPAND, true);
		else if (n && n->first) target = n->first;
		break;
	case VK_ADD:
		if (n) Expand(hwnd, s, n, TVE_EXPAND, true);
		break;
	case VK_SUBTRACT:
		if (n) Expand(hwnd, s, n, TVE_COLLAPSE, true);
		break;
	case VK_RETURN: {
		NMHDR h{};
		NotifyParent(hwnd, &h, NM_RETURN);
		break;
	}
	default:
		break;
	}
	if (target) Select(hwnd, s, target, TVC_BYKEYBOARD);
}

}  // namespace

namespace winport {

LRESULT CALLBACK TreeViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TreeView* s = StateOf(hwnd);
	if (msg == WM_NCCREATE) {
		auto w = GetWindowObject(hwnd);
		s = new TreeView;
		w->control = s;
		s->border = (w->style & WS_BORDER) || (w->exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
		w->style &= ~WS_BORDER;
		w->exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
		w->style |= WS_VSCROLL | WS_HSCROLL;
		s->rowHeight = FontLineHeight(DialogFont()) + 5;
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	if (!s) return DefWindowProcW(hwnd, msg, wParam, lParam);
	switch (msg) {
	case WM_NCDESTROY: {
		// ここでは親へ通知しない（親はすでに破棄の途中のことがある）
		std::vector<Node*> all(s->nodes.begin(), s->nodes.end());
		for (Node* n : all) delete n;
		delete s;
		GetWindowObject(hwnd)->control = nullptr;
		break;
	}
	case WM_SETFONT:
		s->font = reinterpret_cast<HFONT>(wParam);
		s->rowHeight = FontLineHeight(FontOf(s)) + 5;
		for (Node* n : s->nodes) n->width = -1;
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
		LeftDown(hwnd, s, PointOf(lParam), false);
		return 0;
	case WM_LBUTTONDBLCLK: {
		const POINT pt = PointOf(lParam);
		LeftDown(hwnd, s, pt, true);
		s->clickPending = false;
		NMHDR h{};
		const LRESULT handled = NotifyParent(hwnd, &h, NM_DBLCLK);
		if (!handled && IsWindow(hwnd)) {
			Node* n = NodeAtY(hwnd, s, pt.y);
			if (n && HitFlags(hwnd, s, n, pt) == TVHT_ONITEMLABEL) Expand(hwnd, s, n, TVE_TOGGLE, true);
		}
		return 0;
	}
	case WM_LBUTTONUP:
		if (s->clickPending) {
			s->clickPending = false;
			NMHDR h{};
			NotifyParent(hwnd, &h, NM_CLICK);
			if (IsWindow(hwnd) && s->pendingEdit) SetTimer(hwnd, kEditTimer, GetDoubleClickTime(), nullptr);
		}
		return 0;
	case WM_RBUTTONDOWN: {
		EndEdit(hwnd, s, true);
		if (GetFocus() != hwnd) SetFocus(hwnd);
		NMHDR h{};
		if (!NotifyParent(hwnd, &h, NM_RCLICK) && IsWindow(hwnd)) {
			POINT screen = PointOf(lParam);
			ClientToScreen(hwnd, &screen);
			SendMessageW(GetParent(hwnd), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(screen.x, screen.y));
		}
		return 0;
	}
	case WM_TIMER:
		if (wParam == kEditTimer) {
			KillTimer(hwnd, kEditTimer);
			Node* n = s->nodes.count(s->pendingEdit) ? s->pendingEdit : nullptr;
			s->pendingEdit = nullptr;
			if (n) BeginEdit(hwnd, s, n);
			return 0;
		}
		break;
	case WM_KEYDOWN:
		KeyDown(hwnd, s, wParam);
		return 0;
	case WM_MOUSEWHEEL:
		s->top += static_cast<short>(HIWORD(wParam)) > 0 ? -3 : 3;
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
		RECT rc;
		GetClientRect(hwnd, &rc);
		switch (LOWORD(wParam)) {
		case SB_LINELEFT: s->offset -= 20; break;
		case SB_LINERIGHT: s->offset += 20; break;
		case SB_PAGELEFT: s->offset -= rc.right; break;
		case SB_PAGERIGHT: s->offset += rc.right; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: s->offset = static_cast<short>(HIWORD(wParam)); break;
		default: return 0;
		}
		MarkDirty(hwnd, s);
		return 0;
	}
	case kEndLabelEdit:
		EndEdit(hwnd, s, wParam != 0);
		return 0;

	case TVM_INSERTITEMW: return reinterpret_cast<LRESULT>(Insert(hwnd, s, reinterpret_cast<const TVINSERTSTRUCTW*>(lParam)));
	case TVM_DELETEITEM: {
		const auto h = reinterpret_cast<HTREEITEM>(lParam);
		if (!h || h == TVI_ROOT) {
			DeleteAll(hwnd, s);
			return TRUE;
		}
		Node* n = Find(s, h);
		if (!n) return FALSE;
		DeleteTree(hwnd, s, n);
		return TRUE;
	}
	case TVM_GETITEMW: {
		auto* out = reinterpret_cast<TVITEMW*>(lParam);
		Node* n = Find(s, out->hItem);
		if (!n) return FALSE;
		if (out->mask & TVIF_TEXT) CopyText(n->text, out->pszText, out->cchTextMax);
		if (out->mask & TVIF_PARAM) out->lParam = n->param;
		if (out->mask & TVIF_STATE) out->state = n->state & (out->stateMask ? out->stateMask : ~0u);
		if (out->mask & TVIF_IMAGE) out->iImage = n->image;
		if (out->mask & TVIF_SELECTEDIMAGE) out->iSelectedImage = n->selectedImage;
		if (out->mask & TVIF_CHILDREN) out->cChildren = n->first ? 1 : n->children;
		return TRUE;
	}
	case TVM_SETITEMW: {
		const auto* in = reinterpret_cast<const TVITEMW*>(lParam);
		Node* n = Find(s, in->hItem);
		if (!n) return FALSE;
		ReadItem(n, in);
		MarkDirty(hwnd, s);
		return TRUE;
	}
	case TVM_GETITEMSTATE: {
		Node* n = Find(s, reinterpret_cast<HTREEITEM>(wParam));
		return n ? n->state & static_cast<UINT>(lParam) : 0;
	}
	case TVM_GETNEXTITEM: return reinterpret_cast<LRESULT>(NextItem(hwnd, s, static_cast<UINT>(wParam), Find(s, reinterpret_cast<HTREEITEM>(lParam))));
	case TVM_SELECTITEM: {
		const auto h = reinterpret_cast<HTREEITEM>(lParam);
		Node* n = Find(s, h);
		if (h && !n) return FALSE;
		switch (wParam & ~TVSI_NOSINGLEEXPAND) {
		case TVGN_CARET: return Select(hwnd, s, n, TVC_UNKNOWN);
		case TVGN_DROPHILITE:
			s->hot = n;
			Invalidate(hwnd);
			return TRUE;
		case TVGN_FIRSTVISIBLE:
			if (n) {
				ExpandAncestors(s, n);
				const int index = VisibleIndex(hwnd, s, n);
				if (index >= 0) s->top = index;
				MarkDirty(hwnd, s);
			}
			return TRUE;
		default:
			return FALSE;
		}
	}
	case TVM_EXPAND: {
		Node* n = Find(s, reinterpret_cast<HTREEITEM>(lParam));
		return n ? Expand(hwnd, s, n, static_cast<UINT>(wParam), false) : FALSE;
	}
	case TVM_ENSUREVISIBLE: {
		Node* n = Find(s, reinterpret_cast<HTREEITEM>(lParam));
		if (n) EnsureVisible(hwnd, s, n);
		return n != nullptr;
	}
	case TVM_GETCOUNT: return static_cast<LRESULT>(s->nodes.size());
	case TVM_GETVISIBLECOUNT: return RowsPerPage(hwnd, s);
	case TVM_SORTCHILDREN: {
		const auto h = reinterpret_cast<HTREEITEM>(lParam);
		Node* n = !h || h == TVI_ROOT ? &s->root : Find(s, h);
		if (n) SortChildren(hwnd, s, n, nullptr, 0, wParam != 0);
		return n != nullptr;
	}
	case TVM_SORTCHILDRENCB: {
		const auto* sort = reinterpret_cast<const TVSORTCB*>(lParam);
		Node* n = !sort->hParent || sort->hParent == TVI_ROOT ? &s->root : Find(s, sort->hParent);
		if (n) SortChildren(hwnd, s, n, sort->lpfnCompare, sort->lParam, wParam != 0);
		return n != nullptr;
	}
	case TVM_HITTEST: {
		auto* info = reinterpret_cast<TVHITTESTINFO*>(lParam);
		Node* n = NodeAtY(hwnd, s, info->pt.y);
		info->hItem = ToHandle(n);
		info->flags = n ? HitFlags(hwnd, s, n, info->pt) : TVHT_NOWHERE;
		return reinterpret_cast<LRESULT>(n);
	}
	case TVM_GETITEMRECT: {
		auto* rc = reinterpret_cast<RECT*>(lParam);
		Node* n = Find(s, *reinterpret_cast<HTREEITEM*>(rc));
		const int index = n ? VisibleIndex(hwnd, s, n) : -1;
		if (index < 0) return FALSE;
		RECT client;
		GetClientRect(hwnd, &client);
		const int y = (index - s->top) * s->rowHeight;
		const int left = LabelLeft(hwnd, s, n) - s->offset;
		*rc = wParam ? RECT{left, y, left + n->width + 4, y + s->rowHeight} : RECT{0, y, client.right, y + s->rowHeight};
		return TRUE;
	}
	case TVM_EDITLABELW: return reinterpret_cast<LRESULT>(BeginEdit(hwnd, s, Find(s, reinterpret_cast<HTREEITEM>(lParam))));
	case TVM_GETEDITCONTROL: return reinterpret_cast<LRESULT>(s->edit);
	case TVM_ENDEDITLABELNOW: EndEdit(hwnd, s, wParam == 0); return TRUE;
	case TVM_SETTEXTCOLOR: { const COLORREF c = s->textColor; s->textColor = static_cast<COLORREF>(lParam); Invalidate(hwnd); return c; }
	case TVM_GETTEXTCOLOR: return s->textColor;
	case TVM_SETBKCOLOR: { const COLORREF c = s->back; s->back = static_cast<COLORREF>(lParam); Invalidate(hwnd); return c; }
	case TVM_GETBKCOLOR: return s->back;
	case TVM_SETIMAGELIST: {
		HIMAGELIST previous = s->images;
		if (wParam == TVSIL_NORMAL) s->images = reinterpret_cast<HIMAGELIST>(lParam);
		MarkDirty(hwnd, s);
		return reinterpret_cast<LRESULT>(previous);
	}
	case TVM_GETIMAGELIST: return reinterpret_cast<LRESULT>(s->images);
	case TVM_GETINDENT: return s->indent;
	case TVM_SETINDENT: s->indent = std::max(static_cast<int>(wParam), 8); MarkDirty(hwnd, s); return 0;
	case TVM_GETITEMHEIGHT: return s->rowHeight;
	case TVM_SETITEMHEIGHT: { const int h = s->rowHeight; s->rowHeight = std::max(static_cast<int>(wParam), 1); MarkDirty(hwnd, s); return h; }
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace winport
