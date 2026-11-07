/*
	test-winport-controls.cpp — ListView / TreeView / トラックバー / ホットキー / プロパティシートのテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <commctrl.h>
#include <prsht.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct Log {
	std::vector<UINT> codes;
	std::vector<NMLISTVIEW> changes;
	LRESULT selChanging = 0;
	int deleted = 0;
	void Clear() { codes.clear(); changes.clear(); selChanging = 0; deleted = 0; }
	int Count(UINT code) const { return static_cast<int>(std::count(codes.begin(), codes.end(), code)); }
} g_log;

LRESULT CALLBACK ParentProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_NOTIFY) {
		auto* header = reinterpret_cast<NMHDR*>(lParam);
		if (header->code == NM_CUSTOMDRAW) return CDRF_DODEFAULT;
		g_log.codes.push_back(header->code);
		if (header->code == LVN_ITEMCHANGED) g_log.changes.push_back(*reinterpret_cast<NMLISTVIEW*>(lParam));
		if (header->code == TVN_SELCHANGINGW) return g_log.selChanging;
		if (header->code == TVN_DELETEITEMW) ++g_log.deleted;
		return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateParent()
{
	static bool registered = false;
	if (!registered) {
		WNDCLASSW wc{};
		wc.lpfnWndProc = ParentProc;
		wc.lpszClassName = L"winport_test_controls";
		::RegisterClassW(&wc);
		registered = true;
	}
	return ::CreateWindowExW(0, L"winport_test_controls", L"", WS_POPUP, 0, 0, 400, 300, nullptr, nullptr, nullptr, nullptr);
}

HWND CreateChild(HWND parent, const wchar_t* cls, DWORD style)
{
	return ::CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 300, 200, parent, reinterpret_cast<HMENU>(100), nullptr, nullptr);
}

int AddItem(HWND list, int at, const wchar_t* text, LPARAM param = 0)
{
	LVITEMW item{};
	item.mask = LVIF_TEXT | LVIF_PARAM;
	item.iItem = at;
	item.pszText = const_cast<LPWSTR>(text);
	item.lParam = param;
	return ListView_InsertItem(list, &item);
}

std::wstring ItemText(HWND list, int index, int sub = 0)
{
	wchar_t buf[64] = {};
	ListView_GetItemText(list, index, sub, buf, 64);
	return buf;
}

void AddColumn(HWND list, int at, const wchar_t* text, int width)
{
	LVCOLUMNW column{};
	column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
	column.fmt = LVCFMT_LEFT;
	column.cx = width;
	column.pszText = const_cast<LPWSTR>(text);
	ListView_InsertColumn(list, at, &column);
}

int CALLBACK ByParamDescending(LPARAM a, LPARAM b, LPARAM) { return static_cast<int>(b - a); }

HTREEITEM AddNode(HWND tree, HTREEITEM parent, HTREEITEM after, const wchar_t* text, LPARAM param = 0)
{
	TVINSERTSTRUCTW in{};
	in.hParent = parent;
	in.hInsertAfter = after;
	in.item.mask = TVIF_TEXT | TVIF_PARAM;
	in.item.pszText = const_cast<LPWSTR>(text);
	in.item.lParam = param;
	return TreeView_InsertItem(tree, &in);
}

std::wstring NodeText(HWND tree, HTREEITEM h)
{
	wchar_t buf[64] = {};
	TVITEMW item{};
	item.mask = TVIF_TEXT | TVIF_HANDLE;
	item.hItem = h;
	item.pszText = buf;
	item.cchTextMax = 64;
	return TreeView_GetItem(tree, &item) ? buf : L"<invalid>";
}

}  // namespace

TEST(WinportListView, ItemsAndSubItems)
{
	HWND parent = CreateParent();
	HWND list = CreateChild(parent, L"SysListView32", LVS_REPORT);
	AddColumn(list, 0, L"名前", 80);
	AddColumn(list, 1, L"値", 60);
	EXPECT_EQ(0, AddItem(list, 0, L"b"));
	EXPECT_EQ(0, AddItem(list, 0, L"a", 7));
	EXPECT_EQ(2, AddItem(list, 99, L"c"));
	ListView_SetItemText(list, 1, 1, const_cast<LPWSTR>(L"sub"));
	EXPECT_EQ(3, ListView_GetItemCount(list));
	EXPECT_EQ(L"a", ItemText(list, 0));
	EXPECT_EQ(L"b", ItemText(list, 1));
	EXPECT_EQ(L"sub", ItemText(list, 1, 1));
	EXPECT_EQ(L"", ItemText(list, 2, 1));

	LVITEMW get{};
	get.mask = LVIF_PARAM;
	get.iItem = 0;
	EXPECT_TRUE(ListView_GetItem(list, &get));
	EXPECT_EQ(7, get.lParam);
	get.iItem = 3;
	EXPECT_FALSE(ListView_GetItem(list, &get));

	EXPECT_EQ(80, ListView_GetColumnWidth(list, 0));
	ListView_SetColumnWidth(list, 1, 33);
	EXPECT_EQ(33, ListView_GetColumnWidth(list, 1));
	ListView_DeleteColumn(list, 0);
	EXPECT_EQ(L"sub", ItemText(list, 1, 0));	// 残った列が先頭になる

	EXPECT_TRUE(ListView_DeleteItem(list, 1));
	EXPECT_EQ(2, ListView_GetItemCount(list));
	EXPECT_TRUE(ListView_DeleteAllItems(list));
	EXPECT_EQ(0, ListView_GetItemCount(list));
	::DestroyWindow(parent);
}

TEST(WinportListView, SingleSelectionMovesAndNotifies)
{
	HWND parent = CreateParent();
	HWND list = CreateChild(parent, L"SysListView32", LVS_REPORT | LVS_SINGLESEL);
	for (int i = 0; i < 3; ++i) AddItem(list, i, L"x");
	g_log.Clear();
	ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	ListView_SetItemState(list, 2, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	EXPECT_EQ(2, ListView_GetNextItem(list, -1, LVNI_ALL | LVNI_SELECTED));
	EXPECT_EQ(-1, ListView_GetNextItem(list, 2, LVNI_ALL | LVNI_SELECTED));
	EXPECT_EQ(2, ListView_GetNextItem(list, -1, LVNI_FOCUSED));
	EXPECT_EQ(1u, ListView_GetSelectedCount(list));
	EXPECT_EQ(0u, ListView_GetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED));
	ASSERT_FALSE(g_log.changes.empty());
	EXPECT_EQ(static_cast<UINT>(LVIF_STATE), g_log.changes.front().uChanged);

	// 同じ状態を設定しても通知しない
	g_log.Clear();
	ListView_SetItemState(list, 2, LVIS_SELECTED, LVIS_SELECTED);
	EXPECT_EQ(0, g_log.Count(LVN_ITEMCHANGED));

	// 前の項目を消すと選択は項目に付いていく
	ListView_DeleteItem(list, 0);
	EXPECT_EQ(1, ListView_GetNextItem(list, -1, LVNI_SELECTED));
	EXPECT_EQ(1, ListView_GetNextItem(list, -1, LVNI_FOCUSED));
	::DestroyWindow(parent);
}

TEST(WinportListView, CheckBoxes)
{
	HWND parent = CreateParent();
	HWND list = CreateChild(parent, L"SysListView32", LVS_REPORT);
	AddColumn(list, 0, L"", 100);
	AddItem(list, 0, L"before");
	ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
	AddItem(list, 1, L"after");
	EXPECT_EQ(static_cast<DWORD>(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT), ListView_GetExtendedListViewStyle(list));
	EXPECT_FALSE(ListView_GetCheckState(list, 0));
	EXPECT_FALSE(ListView_GetCheckState(list, 1));
	ListView_SetCheckState(list, 1, TRUE);
	EXPECT_TRUE(ListView_GetCheckState(list, 1));
	::SendMessageW(list, WM_SETFOCUS, 0, 0);
	ListView_SetItemState(list, 1, LVIS_FOCUSED, LVIS_FOCUSED);
	::SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
	EXPECT_FALSE(ListView_GetCheckState(list, 1));
	::DestroyWindow(parent);
}

TEST(WinportListView, SortedInsertAndSortItems)
{
	HWND parent = CreateParent();
	HWND list = CreateChild(parent, L"SysListView32", LVS_LIST | LVS_SORTASCENDING);
	AddItem(list, 0, L"pear");
	AddItem(list, 0, L"Apple");
	AddItem(list, 0, L"zebra");
	AddItem(list, 0, L"mango");
	EXPECT_EQ(L"Apple", ItemText(list, 0));
	EXPECT_EQ(L"mango", ItemText(list, 1));
	EXPECT_EQ(L"pear", ItemText(list, 2));
	EXPECT_EQ(L"zebra", ItemText(list, 3));
	::DestroyWindow(parent);

	parent = CreateParent();
	list = CreateChild(parent, L"SysListView32", LVS_REPORT);
	AddItem(list, 0, L"one", 1);
	AddItem(list, 1, L"three", 3);
	AddItem(list, 2, L"two", 2);
	ListView_SetItemState(list, 2, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
	ListView_SortItems(list, ByParamDescending, 0);
	EXPECT_EQ(L"three", ItemText(list, 0));
	EXPECT_EQ(L"two", ItemText(list, 1));
	EXPECT_EQ(L"one", ItemText(list, 2));
	EXPECT_EQ(1, ListView_GetNextItem(list, -1, LVNI_FOCUSED));
	::DestroyWindow(parent);
}

TEST(WinportListView, ClickSelectsAndNotifies)
{
	HWND parent = CreateParent();
	HWND list = CreateChild(parent, L"SysListView32", LVS_REPORT);
	AddColumn(list, 0, L"列", 200);
	ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
	for (int i = 0; i < 5; ++i) AddItem(list, i, L"x");
	RECT rc{};
	rc.left = LVIR_BOUNDS;
	ASSERT_TRUE(ListView_GetItemRect(list, 2, &rc, LVIR_BOUNDS));
	const LPARAM at = MAKELPARAM(20, (rc.top + rc.bottom) / 2);
	g_log.Clear();
	::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, at);
	::SendMessageW(list, WM_LBUTTONUP, 0, at);
	EXPECT_EQ(2, ListView_GetNextItem(list, -1, LVNI_SELECTED));
	EXPECT_EQ(1, g_log.Count(NM_CLICK));

	g_log.Clear();
	::SendMessageW(list, WM_LBUTTONDBLCLK, MK_LBUTTON, at);
	::SendMessageW(list, WM_LBUTTONUP, 0, at);
	EXPECT_EQ(1, g_log.Count(NM_DBLCLK));

	// 何もないところのクリックは選択を外す
	::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 190));
	::SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(20, 190));
	EXPECT_EQ(0u, ListView_GetSelectedCount(list));

	g_log.Clear();
	::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 5));	// ヘッダー
	::SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(20, 5));
	EXPECT_EQ(1, g_log.Count(LVN_COLUMNCLICK));
	::DestroyWindow(parent);
}

TEST(WinportListView, TopIndexFollowsEnsureVisible)
{
	HWND parent = CreateParent();
	HWND list = CreateChild(parent, L"SysListView32", LVS_REPORT);
	AddColumn(list, 0, L"列", 100);
	for (int i = 0; i < 200; ++i) AddItem(list, i, L"x");
	EXPECT_EQ(0, ListView_GetTopIndex(list));
	ListView_EnsureVisible(list, 150, FALSE);
	const int top = ListView_GetTopIndex(list);
	EXPECT_LE(top, 150);
	EXPECT_GT(top + ListView_GetCountPerPage(list), 150);
	ListView_EnsureVisible(list, 3, FALSE);
	EXPECT_EQ(3, ListView_GetTopIndex(list));
	::DestroyWindow(parent);
}

TEST(WinportListView, LabelEditCommitsThroughParent)
{
	HWND parent = CreateParent();
	HWND list = CreateChild(parent, L"SysListView32", LVS_REPORT | LVS_EDITLABELS);
	AddColumn(list, 0, L"列", 100);
	AddItem(list, 0, L"old");
	::SetActiveWindow(parent);	// 非アクティブなら SetFocus でアクティブ化が起き、編集が終わってしまう
	g_log.Clear();
	HWND edit = ListView_EditLabel(list, 0);
	ASSERT_NE(nullptr, edit);
	EXPECT_EQ(edit, ListView_GetEditControl(list));
	EXPECT_EQ(1, g_log.Count(LVN_BEGINLABELEDITW));
	::SetWindowTextW(edit, L"new");
	::SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
	EXPECT_EQ(1, g_log.Count(LVN_ENDLABELEDITW));
	EXPECT_EQ(nullptr, ListView_GetEditControl(list));
	EXPECT_EQ(L"old", ItemText(list, 0));		// 親が TRUE を返さなければ変えない
	::DestroyWindow(parent);
}

TEST(WinportTreeView, StructureAndNavigation)
{
	HWND parent = CreateParent();
	HWND tree = CreateChild(parent, L"SysTreeView32", TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT);
	HTREEITEM b = AddNode(tree, TVI_ROOT, TVI_LAST, L"b");
	HTREEITEM a = AddNode(tree, TVI_ROOT, TVI_FIRST, L"a");
	HTREEITEM c = AddNode(tree, TVI_ROOT, b, L"c");
	HTREEITEM child2 = AddNode(tree, b, TVI_LAST, L"b2");
	HTREEITEM child1 = AddNode(tree, b, TVI_FIRST, L"b1");
	EXPECT_EQ(a, TreeView_GetRoot(tree));
	EXPECT_EQ(b, TreeView_GetNextSibling(tree, a));
	EXPECT_EQ(c, TreeView_GetNextSibling(tree, b));
	EXPECT_EQ(nullptr, TreeView_GetNextSibling(tree, c));
	EXPECT_EQ(b, TreeView_GetPrevSibling(tree, c));
	EXPECT_EQ(child1, TreeView_GetChild(tree, b));
	EXPECT_EQ(child2, TreeView_GetNextSibling(tree, child1));
	EXPECT_EQ(b, TreeView_GetParent(tree, child2));
	EXPECT_EQ(nullptr, TreeView_GetParent(tree, a));
	EXPECT_EQ(5u, TreeView_GetCount(tree));
	EXPECT_EQ(L"b1", NodeText(tree, child1));

	TVITEMW set{};
	set.mask = TVIF_TEXT | TVIF_HANDLE;
	set.hItem = child1;
	set.pszText = const_cast<LPWSTR>(L"renamed");
	EXPECT_TRUE(TreeView_SetItem(tree, &set));
	EXPECT_EQ(L"renamed", NodeText(tree, child1));

	// 閉じている子は表示順に現れない
	EXPECT_EQ(c, TreeView_GetNextVisible(tree, b));
	TreeView_Expand(tree, b, TVE_EXPAND);
	EXPECT_EQ(child1, TreeView_GetNextVisible(tree, b));
	::DestroyWindow(parent);
}

TEST(WinportTreeView, SelectionCanBeVetoed)
{
	HWND parent = CreateParent();
	HWND tree = CreateChild(parent, L"SysTreeView32", 0);
	HTREEITEM a = AddNode(tree, TVI_ROOT, TVI_LAST, L"a");
	HTREEITEM b = AddNode(tree, TVI_ROOT, TVI_LAST, L"b");
	g_log.Clear();
	EXPECT_TRUE(TreeView_SelectItem(tree, a));
	EXPECT_EQ(a, TreeView_GetSelection(tree));
	EXPECT_EQ(1, g_log.Count(TVN_SELCHANGEDW));
	g_log.Clear();
	g_log.selChanging = TRUE;
	EXPECT_FALSE(TreeView_SelectItem(tree, b));
	EXPECT_EQ(a, TreeView_GetSelection(tree));
	EXPECT_EQ(0, g_log.Count(TVN_SELCHANGEDW));
	g_log.Clear();
	::DestroyWindow(parent);
}

TEST(WinportTreeView, DeleteMovesSelectionAndInvalidatesHandle)
{
	HWND parent = CreateParent();
	HWND tree = CreateChild(parent, L"SysTreeView32", 0);
	HTREEITEM a = AddNode(tree, TVI_ROOT, TVI_LAST, L"a");
	HTREEITEM b = AddNode(tree, TVI_ROOT, TVI_LAST, L"b");
	HTREEITEM sub = AddNode(tree, a, TVI_LAST, L"a1");
	TreeView_SelectItem(tree, sub);
	EXPECT_EQ(a, TreeView_GetParent(tree, sub));
	g_log.Clear();
	EXPECT_TRUE(TreeView_DeleteItem(tree, a));
	EXPECT_EQ(2, g_log.deleted);	// 子も通知される
	EXPECT_EQ(b, TreeView_GetSelection(tree));
	EXPECT_EQ(L"<invalid>", NodeText(tree, a));
	EXPECT_TRUE(TreeView_DeleteAllItems(tree));
	EXPECT_EQ(nullptr, TreeView_GetRoot(tree));
	EXPECT_EQ(nullptr, TreeView_GetSelection(tree));
	g_log.Clear();
	::DestroyWindow(parent);
}

TEST(WinportTreeView, Sort)
{
	HWND parent = CreateParent();
	HWND tree = CreateChild(parent, L"SysTreeView32", 0);
	AddNode(tree, TVI_ROOT, TVI_LAST, L"two");
	HTREEITEM last = AddNode(tree, TVI_ROOT, TVI_LAST, L"one");
	EXPECT_TRUE(TreeView_SortChildren(tree, TVI_ROOT, FALSE));
	EXPECT_EQ(last, TreeView_GetRoot(tree));
	HTREEITEM sorted = AddNode(tree, TVI_ROOT, TVI_SORT, L"p");
	EXPECT_EQ(sorted, TreeView_GetNextSibling(tree, last));
	::DestroyWindow(parent);
}

TEST(WinportTreeView, ChildrenFlagShowsExpandableEmptyNode)
{
	HWND parent = CreateParent();
	HWND tree = CreateChild(parent, L"SysTreeView32", TVS_HASBUTTONS);
	TVINSERTSTRUCTW in{};
	in.hParent = TVI_ROOT;
	in.hInsertAfter = TVI_LAST;
	in.item.mask = TVIF_TEXT | TVIF_CHILDREN;
	in.item.pszText = const_cast<LPWSTR>(L"folder");
	in.item.cChildren = 1;
	HTREEITEM h = TreeView_InsertItem(tree, &in);
	g_log.Clear();
	// 初めて開くときだけ TVM_EXPAND でも通知する。上流のファイルツリーはここで子を足す
	TreeView_Expand(tree, h, TVE_EXPAND);
	EXPECT_EQ(1, g_log.Count(TVN_ITEMEXPANDINGW));
	TreeView_Expand(tree, h, TVE_COLLAPSE);
	TreeView_Expand(tree, h, TVE_EXPAND);
	EXPECT_EQ(1, g_log.Count(TVN_ITEMEXPANDINGW));
	::DestroyWindow(parent);
}

TEST(WinportSmallControls, TrackBar)
{
	HWND parent = CreateParent();
	HWND track = CreateChild(parent, L"msctls_trackbar32", 0);
	::SendMessageW(track, TBM_SETRANGE, TRUE, MAKELPARAM(10, 90));
	::SendMessageW(track, TBM_SETPOS, TRUE, 50);
	EXPECT_EQ(50, ::SendMessageW(track, TBM_GETPOS, 0, 0));
	::SendMessageW(track, TBM_SETPOS, TRUE, 500);
	EXPECT_EQ(90, ::SendMessageW(track, TBM_GETPOS, 0, 0));
	::SendMessageW(track, WM_KEYDOWN, VK_LEFT, 0);
	EXPECT_EQ(89, ::SendMessageW(track, TBM_GETPOS, 0, 0));
	::DestroyWindow(parent);
}

TEST(WinportSmallControls, HotKey)
{
	HWND parent = CreateParent();
	HWND hot = CreateChild(parent, L"msctls_hotkey32", 0);
	const WPARAM key = MAKEWORD('Z', HOTKEYF_ALT | HOTKEYF_CONTROL);
	::SendMessageW(hot, HKM_SETHOTKEY, key, 0);
	EXPECT_EQ(static_cast<LRESULT>(key), ::SendMessageW(hot, HKM_GETHOTKEY, 0, 0));
	::DestroyWindow(parent);
}

namespace {

//! 子を持たない最小のダイアログテンプレート（文字列は 16 ビット）
std::vector<uint16_t> PageTemplate()
{
	std::vector<uint16_t> t(12, 0);
	const DWORD style = WS_POPUP | WS_CAPTION;	// 実物と同じく単独のダイアログの形。シートが子にする
	std::memcpy(t.data(), &style, sizeof(style));
	t[7] = 100;		// cx
	t[8] = 80;		// cy
	return t;
}

struct PageLog {
	std::vector<std::string> events;
	int rejectKill = 0;	//!< この回数だけ PSN_KILLACTIVE を拒否する
	int pressButton = PSBTN_OK;
	HWND sheet = nullptr;
	bool pageIsChild = false;
} g_pages;

INT_PTR CALLBACK PageProc(HWND hDlg, UINT msg, WPARAM, LPARAM lParam)
{
	const std::string name = "p" + std::to_string(::GetWindowLongPtrW(hDlg, GWLP_ID));
	if (msg == WM_INITDIALOG) {
		g_pages.sheet = ::GetParent(hDlg);
		g_pages.pageIsChild = (::GetWindowLongW(hDlg, GWL_STYLE) & WS_CHILD) != 0;
		const auto* page = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
		g_pages.events.push_back("init:" + std::to_string(page->lParam));
		::SetWindowLongPtrW(hDlg, DWLP_USER, page->lParam);
		return TRUE;
	}
	if (msg != WM_NOTIFY) return FALSE;
	const LONG_PTR id = ::GetWindowLongPtrW(hDlg, DWLP_USER);
	switch (reinterpret_cast<const NMHDR*>(lParam)->code) {
	case PSN_SETACTIVE:
		g_pages.events.push_back("active:" + std::to_string(id));
		if (id == 1) ::PostMessageW(g_pages.sheet, PSM_PRESSBUTTON, g_pages.pressButton, 0);
		return TRUE;
	case PSN_KILLACTIVE:
		g_pages.events.push_back("kill:" + std::to_string(id));
		if (g_pages.rejectKill > 0) {
			--g_pages.rejectKill;
			::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, TRUE);
			::PostMessageW(g_pages.sheet, PSM_PRESSBUTTON, PSBTN_OK, 0);
		}
		return TRUE;
	case PSN_APPLY:
		g_pages.events.push_back("apply:" + std::to_string(id));
		return TRUE;
	case PSN_RESET:
		g_pages.events.push_back("reset:" + std::to_string(id));
		return TRUE;
	default:
		return FALSE;
	}
}

INT_PTR RunSheet(UINT startPage)
{
	static const std::vector<uint16_t> tmpl = PageTemplate();
	PROPSHEETPAGEW pages[2]{};
	for (int i = 0; i < 2; ++i) {
		pages[i].dwSize = sizeof(PROPSHEETPAGEW);
		pages[i].dwFlags = PSP_DLGINDIRECT | PSP_USETITLE;
		pages[i].pResource = reinterpret_cast<LPCDLGTEMPLATEW>(tmpl.data());
		pages[i].pfnDlgProc = PageProc;
		pages[i].pszTitle = i ? L"二" : L"一";
		pages[i].lParam = i;
	}
	PROPSHEETHEADERW header{};
	header.dwSize = sizeof(header);
	header.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
	header.pszCaption = L"シート";
	header.nPages = 2;
	header.nStartPage = startPage;
	header.ppsp = pages;
	return ::PropertySheetW(&header);
}

}  // namespace

TEST(WinportPropertySheet, OkAppliesOnlyCreatedPages)
{
	g_pages = PageLog{};
	EXPECT_EQ(1, RunSheet(1));
	EXPECT_TRUE(g_pages.pageIsChild);
	// ページ 0 は開いていないので作られず、通知も届かない
	const std::vector<std::string> expected{"init:1", "active:1", "kill:1", "apply:1"};
	EXPECT_EQ(expected, g_pages.events);
}

TEST(WinportPropertySheet, CancelResetsAndReturnsZero)
{
	g_pages = PageLog{};
	g_pages.pressButton = PSBTN_CANCEL;
	EXPECT_EQ(0, RunSheet(1));
	const std::vector<std::string> expected{"init:1", "active:1", "reset:1"};
	EXPECT_EQ(expected, g_pages.events);
}

TEST(WinportPropertySheet, RejectedKillActiveKeepsSheetOpen)
{
	g_pages = PageLog{};
	g_pages.rejectKill = 1;
	EXPECT_EQ(1, RunSheet(1));
	const std::vector<std::string> expected{"init:1", "active:1", "kill:1", "kill:1", "apply:1"};
	EXPECT_EQ(expected, g_pages.events);
}
