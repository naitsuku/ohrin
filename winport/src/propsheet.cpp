/*
	propsheet.cpp — プロパティシート（PropertySheetW）

	上流の共通設定・タイプ別設定はページのダイアログプロシージャとテンプレートをそのまま渡してくるので、
	Win32 と同じ手順で動かす。ページは初めて開くときに作り、切り替えでは PSN_KILLACTIVE / PSN_SETACTIVE を送る。
	OK は現在のページの PSN_KILLACTIVE のあと、作成済みの全ページへ PSN_APPLY を送る。
	シート自体は空のダイアログテンプレートから作り、タブとボタンを WM_INITDIALOG で並べる。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <commctrl.h>
#include <prsht.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "dialog_internal.h"
#include "user32_internal.h"
#include "winport_internal.h"

namespace {

constexpr int kTabControlId = 0x3020;	// Win32 のプロパティシートと同じ ID
constexpr int kApplyId = 0x3021;
constexpr int kMargin = 8;
constexpr int kButtonWidth = 84;
constexpr int kButtonHeight = 24;

struct Sheet {
	PROPSHEETHEADERW header{};
	std::vector<PROPSHEETPAGEW> pages;
	std::vector<HWND> windows;
	HWND tab = nullptr;
	RECT pageRect{};
	int current = -1;
	INT_PTR result = 0;
};

Sheet* SheetOf(HWND hDlg) { return reinterpret_cast<Sheet*>(GetWindowLongPtrW(hDlg, DWLP_USER)); }

const void* PageTemplate(const PROPSHEETPAGEW& page)
{
	if (page.dwFlags & PSP_DLGINDIRECT) return page.pResource;
	HRSRC res = FindResourceW(page.hInstance, page.pszTemplate, reinterpret_cast<LPCWSTR>(RT_DIALOG));
	return res ? LockResource(LoadResource(page.hInstance, res)) : nullptr;
}

//! テンプレートの大きさ（ダイアログ単位）
SIZE TemplateSize(const void* tmpl)
{
	const auto words = static_cast<const uint16_t*>(tmpl);
	const bool ex = words[1] == 0xFFFF;
	const auto cx = reinterpret_cast<const unsigned char*>(tmpl) + (ex ? 22 : 14);	// x, y の次
	int16_t size[2];
	std::memcpy(size, cx, sizeof(size));
	return {size[0], size[1]};
}

std::wstring PageTitle(const PROPSHEETPAGEW& page)
{
	if ((page.dwFlags & PSP_USETITLE) && page.pszTitle) {
		if (!IS_INTRESOURCE(page.pszTitle)) return page.pszTitle;
		wchar_t buf[256] = {};
		LoadStringW(page.hInstance, static_cast<UINT>(reinterpret_cast<uintptr_t>(page.pszTitle)), buf, 256);
		return buf;
	}
	return L"";
}

//! ページへ通知し、ページの返した値（DWLP_MSGRESULT）を返す
LRESULT NotifyPage(HWND hDlg, HWND page, UINT code)
{
	PSHNOTIFY n{};
	n.hdr.hwndFrom = hDlg;
	n.hdr.idFrom = 0;
	n.hdr.code = code;
	return SendMessageW(page, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&n));
}

HWND EnsurePage(HWND hDlg, Sheet* sheet, int index)
{
	if (sheet->windows[index]) return sheet->windows[index];
	PROPSHEETPAGEW& page = sheet->pages[index];
	const void* tmpl = PageTemplate(page);
	if (!tmpl) return nullptr;
	if ((page.dwFlags & PSP_USECALLBACK) && page.pfnCallback) page.pfnCallback(nullptr, PSPCB_CREATE, &page);
	HWND h = winport::DialogCreatePage(page.hInstance, tmpl, hDlg, page.pfnDlgProc, reinterpret_cast<LPARAM>(&page));
	sheet->windows[index] = h;
	if (h) {
		const RECT& rc = sheet->pageRect;
		SetWindowPos(h, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
	}
	return h;
}

//! 現在のページから離れてよいか
bool LeaveCurrent(HWND hDlg, Sheet* sheet)
{
	if (sheet->current < 0 || !sheet->windows[sheet->current]) return true;
	return NotifyPage(hDlg, sheet->windows[sheet->current], PSN_KILLACTIVE) == 0;
}

void ShowPage(HWND hDlg, Sheet* sheet, int index)
{
	HWND page = EnsurePage(hDlg, sheet, index);
	if (!page) return;
	const int previous = sheet->current;
	sheet->current = index;
	NotifyPage(hDlg, page, PSN_SETACTIVE);
	SendMessageW(sheet->tab, TCM_SETCURSEL, index, 0);
	if (previous >= 0 && previous != index && sheet->windows[previous]) ShowWindow(sheet->windows[previous], SW_HIDE);
	SetWindowPos(page, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	ShowWindow(page, SW_SHOWNA);
}

//! OK / 適用。ページが拒否したら false
bool Apply(HWND hDlg, Sheet* sheet)
{
	if (!LeaveCurrent(hDlg, sheet)) return false;
	for (size_t i = 0; i < sheet->windows.size(); ++i) {
		if (!sheet->windows[i]) continue;
		if (NotifyPage(hDlg, sheet->windows[i], PSN_APPLY) == PSNRET_INVALID_NOCHANGEPAGE) {
			ShowPage(hDlg, sheet, static_cast<int>(i));
			return false;
		}
	}
	sheet->result = 1;
	return true;
}

void Cancel(HWND hDlg, Sheet* sheet)
{
	if (sheet->current >= 0 && sheet->windows[sheet->current] && NotifyPage(hDlg, sheet->windows[sheet->current], PSN_QUERYCANCEL)) return;
	for (HWND page : sheet->windows) {
		if (page) NotifyPage(hDlg, page, PSN_RESET);
	}
	EndDialog(hDlg, sheet->result);
}

HWND AddButton(HWND hDlg, int id, const wchar_t* text, DWORD style)
{
	HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | style, 0, 0, kButtonWidth, kButtonHeight, hDlg,
	                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
	SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(winport::DialogFont()), FALSE);
	return h;
}

//! タブの段数が決まるまでシートの高さは決まらないので、全項目を入れてから並べる
void Arrange(HWND hDlg, Sheet* sheet)
{
	SIZE page{};
	for (const PROPSHEETPAGEW& p : sheet->pages) {
		const void* tmpl = PageTemplate(p);
		if (!tmpl) continue;
		const SIZE size = TemplateSize(tmpl);
		RECT rc{0, 0, size.cx, size.cy};
		MapDialogRect(hDlg, &rc);
		page.cx = std::max(page.cx, rc.right);
		page.cy = std::max(page.cy, rc.bottom);
	}
	const int tabWidth = page.cx + 8;
	SetWindowPos(sheet->tab, nullptr, kMargin, kMargin, tabWidth, page.cy, SWP_NOZORDER | SWP_NOACTIVATE);
	RECT inner{0, 0, tabWidth, page.cy};
	SendMessageW(sheet->tab, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&inner));
	const int header = inner.top;
	const int tabHeight = header + page.cy + 6;
	SetWindowPos(sheet->tab, nullptr, 0, 0, tabWidth, tabHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	sheet->pageRect = {kMargin + 4, kMargin + header + 2, kMargin + 4 + page.cx, kMargin + header + 2 + page.cy};

	const int clientWidth = tabWidth + kMargin * 2;
	const int buttonTop = kMargin + tabHeight + kMargin;
	const int clientHeight = buttonTop + kButtonHeight + kMargin;
	int x = clientWidth - kMargin - kButtonWidth;
	for (int id : {static_cast<int>(IDHELP), kApplyId, static_cast<int>(IDCANCEL), static_cast<int>(IDOK)}) {
		HWND h = GetDlgItem(hDlg, id);
		if (!h || !(GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE)) continue;
		SetWindowPos(h, nullptr, x, buttonTop, kButtonWidth, kButtonHeight, SWP_NOZORDER | SWP_NOACTIVATE);
		x -= kButtonWidth + 6;
	}

	RECT window, client;
	GetWindowRect(hDlg, &window);
	GetClientRect(hDlg, &client);
	const int width = clientWidth + (window.right - window.left) - client.right;
	const int height = clientHeight + (window.bottom - window.top) - client.bottom;
	RECT area;
	HWND owner = GetWindow(hDlg, GW_OWNER);
	if (owner && IsWindowVisible(owner)) GetWindowRect(owner, &area);
	else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
	SetWindowPos(hDlg, nullptr, std::max<int>(area.left, area.left + (area.right - area.left - width) / 2),
	             std::max<int>(area.top, area.top + (area.bottom - area.top - height) / 2), width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Init(HWND hDlg, Sheet* sheet)
{
	SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(sheet));
	sheet->tab = CreateWindowExW(0, L"SysTabControl32", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_MULTILINE, 0, 0, 100, 100, hDlg,
	                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabControlId)), nullptr, nullptr);
	SendMessageW(sheet->tab, WM_SETFONT, reinterpret_cast<WPARAM>(winport::DialogFont()), FALSE);
	for (size_t i = 0; i < sheet->pages.size(); ++i) {
		std::wstring title = PageTitle(sheet->pages[i]);
		TCITEMW item{};
		item.mask = TCIF_TEXT;
		item.pszText = title.data();
		SendMessageW(sheet->tab, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
	}
	AddButton(hDlg, IDOK, L"OK", WS_VISIBLE | BS_DEFPUSHBUTTON);
	AddButton(hDlg, IDCANCEL, L"キャンセル", WS_VISIBLE | BS_PUSHBUTTON);
	AddButton(hDlg, kApplyId, L"適用", ((sheet->header.dwFlags & PSH_NOAPPLYNOW) ? 0 : WS_VISIBLE) | BS_PUSHBUTTON);
	bool help = (sheet->header.dwFlags & PSH_HASHELP) != 0;
	for (const PROPSHEETPAGEW& p : sheet->pages) help = help || (p.dwFlags & PSP_HASHELP);
	AddButton(hDlg, IDHELP, L"ヘルプ", (help ? WS_VISIBLE : 0) | BS_PUSHBUTTON);
	Arrange(hDlg, sheet);
	winport::DialogAdoptChildren(hDlg);
	if ((sheet->header.dwFlags & PSH_USECALLBACK) && sheet->header.pfnCallback) {
		sheet->header.pfnCallback(hDlg, PSCB_INITIALIZED, 0);
		winport::DialogAdoptChildren(hDlg);		// 上流はここで「設定フォルダー」ボタンを足す
	}
	const UINT start = std::min<UINT>(sheet->header.nStartPage, static_cast<UINT>(sheet->pages.size()) - 1);
	ShowPage(hDlg, sheet, static_cast<int>(start));
	if (HWND first = GetNextDlgTabItem(sheet->windows[start], nullptr, FALSE)) winport::DialogSetFocus(hDlg, first);
}

INT_PTR Result(HWND hDlg, LRESULT value)
{
	SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, value);
	return TRUE;
}

INT_PTR CALLBACK SheetProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INITDIALOG) {
		Init(hDlg, reinterpret_cast<Sheet*>(lParam));
		return FALSE;
	}
	Sheet* sheet = SheetOf(hDlg);
	if (!sheet) return FALSE;
	switch (msg) {
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			if (Apply(hDlg, sheet)) EndDialog(hDlg, sheet->result);
			return TRUE;
		case IDCANCEL:
			Cancel(hDlg, sheet);
			return TRUE;
		case kApplyId:
			Apply(hDlg, sheet);
			return TRUE;
		case IDHELP:
			if (sheet->current >= 0) NotifyPage(hDlg, sheet->windows[sheet->current], PSN_HELP);
			return TRUE;
		default:
			return FALSE;
		}
	case WM_NOTIFY: {
		const auto* header = reinterpret_cast<const NMHDR*>(lParam);
		if (header->hwndFrom != sheet->tab) return FALSE;
		if (header->code == TCN_SELCHANGING) return Result(hDlg, !LeaveCurrent(hDlg, sheet));
		if (header->code == TCN_SELCHANGE) {
			ShowPage(hDlg, sheet, static_cast<int>(SendMessageW(sheet->tab, TCM_GETCURSEL, 0, 0)));
			return TRUE;
		}
		return FALSE;
	}
	case PSM_GETTABCONTROL:
		return Result(hDlg, reinterpret_cast<LRESULT>(sheet->tab));
	case PSM_GETCURRENTPAGEHWND:
		return Result(hDlg, sheet->current >= 0 ? reinterpret_cast<LRESULT>(sheet->windows[sheet->current]) : 0);
	case PSM_SETCURSEL: {
		const int index = static_cast<int>(wParam);
		if (index < 0 || index >= static_cast<int>(sheet->pages.size()) || !LeaveCurrent(hDlg, sheet)) return Result(hDlg, FALSE);
		ShowPage(hDlg, sheet, index);
		return Result(hDlg, TRUE);
	}
	case PSM_SETTITLEW:
		if (!IS_INTRESOURCE(lParam)) SetWindowTextW(hDlg, reinterpret_cast<LPCWSTR>(lParam));
		return Result(hDlg, 0);
	case PSM_PRESSBUTTON: {
		static const int ids[] = {0, 0, 0, IDOK, kApplyId, IDCANCEL, IDHELP};	// PSBTN_BACK / NEXT / FINISH はウィザード用
		if (wParam < std::size(ids) && ids[wParam]) PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(ids[wParam], BN_CLICKED), 0);
		return Result(hDlg, 0);
	}
	case PSM_CHANGED:
	case PSM_UNCHANGED:
	case PSM_CANCELTOCLOSE:
		return Result(hDlg, 0);
	default:
		return FALSE;
	}
}

//! 空のダイアログテンプレート（文字列は 16 ビット）
std::vector<uint16_t> EmptyTemplate(const std::wstring& title)
{
	std::vector<uint16_t> t(13, 0);
	const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
	std::memcpy(t.data(), &style, sizeof(style));
	t[9] = 0;	// メニューなし（t[4] は項目数 0、t[5..8] は x, y, cx, cy）
	t[10] = 0;	// クラス既定
	t.resize(11);
	for (wchar_t c : title) t.push_back(static_cast<uint16_t>(c));
	t.push_back(0);
	t.resize((t.size() + 1) & ~size_t(1));
	return t;
}

}  // namespace

extern "C" INT_PTR WINAPI PropertySheetW(LPCPROPSHEETHEADERW header)
{
	if (!header || !(header->dwFlags & PSH_PROPSHEETPAGE) || !header->nPages) {
		winport::ReportUnimplemented("comctl32", "PropertySheetW without PSH_PROPSHEETPAGE");
		return -1;
	}
	Sheet sheet;
	std::memcpy(&sheet.header, header, std::min<size_t>(header->dwSize, sizeof(sheet.header)));
	const auto* cursor = reinterpret_cast<const unsigned char*>(header->ppsp);
	for (UINT i = 0; i < header->nPages; ++i) {
		const auto* page = reinterpret_cast<const PROPSHEETPAGEW*>(cursor);
		PROPSHEETPAGEW copy{};
		std::memcpy(&copy, page, std::min<size_t>(page->dwSize, sizeof(copy)));
		sheet.pages.push_back(copy);
		cursor += page->dwSize;
	}
	sheet.windows.assign(sheet.pages.size(), nullptr);
	std::wstring title = header->pszCaption && !IS_INTRESOURCE(header->pszCaption) ? header->pszCaption : L"";
	const std::vector<uint16_t> tmpl = EmptyTemplate(title);
	const INT_PTR result = DialogBoxIndirectParamW(header->hInstance, reinterpret_cast<LPCDLGTEMPLATEW>(tmpl.data()), header->hwndParent, SheetProc,
	                                               reinterpret_cast<LPARAM>(&sheet));
	return result < 0 ? -1 : sheet.result;
}
