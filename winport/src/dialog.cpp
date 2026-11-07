/*
	dialog.cpp — Win32 のダイアログ機構（DialogBox / CreateDialog / DefDlgProc / IsDialogMessage）

	テンプレートは rc2winport.py が Windows と同じ RT_DIALOG 形式で出力する（文字列は 16 ビット）。
	コントロールは dialog_controls.cpp。上流の CDlg* はここを通ってそのまま動く。

	SPDX-License-Identifier: Zlib
*/
#include "user32_internal.h"

#include <algorithm>
#include <cstring>
#include <cwchar>

#include "backend.h"
#include "dialog_internal.h"
#include "winport_internal.h"

using namespace winport;

namespace {

constexpr int kBaseX = 7, kBaseY = 13;  //!< ダイアログ単位（MapDialogRect と同じ）。Hiragino は ＭＳ Ｐゴシックより幅が広いので 6 より大きい

struct DialogState {
	DLGPROC proc = nullptr;
	INT_PTR result = 0;
	bool ended = false;
	HWND lastFocus = nullptr;
	int defId = IDOK;
	std::vector<HWND> order;  //!< 作成順 = タブ順
};

DialogState* StateOf(HWND hDlg)
{
	auto w = GetWindowObject(hDlg);
	return w && w->cls->proc == DefDlgProcW ? static_cast<DialogState*>(w->control) : nullptr;
}

// ---- テンプレートの解析 ----
struct Reader {
	const unsigned char* p;
	template <typename T> T Get() { T v; std::memcpy(&v, p, sizeof(T)); p += sizeof(T); return v; }
	void Align() { p = reinterpret_cast<const unsigned char*>((reinterpret_cast<uintptr_t>(p) + 3) & ~uintptr_t(3)); }
	//! 文字列または 0xFFFF + 序数
	std::wstring Sz(int* ordinal = nullptr)
	{
		std::wstring s;
		uint16_t c = Get<uint16_t>();
		if (c == 0xFFFF) { const uint16_t v = Get<uint16_t>(); if (ordinal) *ordinal = v; return s; }
		for (; c; c = Get<uint16_t>()) s += static_cast<wchar_t>(c);
		return s;
	}
};

const wchar_t* AtomClass(int atom)
{
	switch (atom) {
	case 0x80: return L"BUTTON";
	case 0x81: return L"EDIT";
	case 0x82: return L"STATIC";
	case 0x83: return L"LISTBOX";
	case 0x84: return L"SCROLLBAR";
	case 0x85: return L"COMBOBOX";
	default: return L"STATIC";
	}
}

RECT ScaleRect(int x, int y, int cx, int cy)
{
	return {x * kBaseX / 4, y * kBaseY / 8, (x + cx) * kBaseX / 4, (y + cy) * kBaseY / 8};
}

HWND CreateFromTemplate(HINSTANCE hInstance, const void* tmpl, HWND hWndParent, DLGPROC proc, LPARAM lParam, bool asPage = false)
{
	Reader r{static_cast<const unsigned char*>(tmpl)};
	const bool ex = reinterpret_cast<const uint16_t*>(tmpl)[1] == 0xFFFF;
	DWORD style, exStyle;
	if (ex) { r.Get<uint32_t>(); r.Get<uint32_t>(); exStyle = r.Get<uint32_t>(); style = r.Get<uint32_t>(); }
	else { style = r.Get<uint32_t>(); exStyle = r.Get<uint32_t>(); }
	const int count = r.Get<uint16_t>();
	int x = r.Get<int16_t>(), y = r.Get<int16_t>();
	const int cx = r.Get<int16_t>(), cy = r.Get<int16_t>();
	if (asPage) {
		// プロパティシートのページ。テンプレートは単独のダイアログの形で書かれていても、子として作る（Win32 と同じ）
		style = (style & ~(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE | DS_MODALFRAME | DS_CENTER | DS_CENTERMOUSE)) | WS_CHILD | DS_CONTROL;
		exStyle = (exStyle & ~(WS_EX_CONTEXTHELP | WS_EX_DLGMODALFRAME)) | WS_EX_CONTROLPARENT;
		x = y = 0;
	}
	r.Sz();                             // メニュー
	const std::wstring cls = r.Sz();    // クラス
	const std::wstring title = r.Sz();
	if (style & DS_SETFONT) {
		r.Get<uint16_t>();
		if (ex) { r.Get<uint16_t>(); r.Get<uint8_t>(); r.Get<uint8_t>(); }
		r.Sz();  // フォント名は使わない（DialogFont の説明を参照）
	}

	// 位置: クライアント領域の大きさはテンプレートで決まり、枠はその外側
	const RECT rc = ScaleRect(x, y, cx, cy);
	int width = rc.right - rc.left, height = rc.bottom - rc.top;
	const bool child = (style & WS_CHILD) != 0;
	if (!child && (style & WS_CAPTION) == WS_CAPTION) height += backend::TitleBarHeight();
	POINT origin{rc.left, rc.top};
	HWND owner = hWndParent;
	if (owner && !child) {
		owner = GetAncestor(owner, GA_ROOT);
		ClientToScreen(hWndParent, &origin);
	}
	if (!child && (style & (DS_CENTER | DS_CENTERMOUSE))) {
		RECT area;
		if (owner && IsWindowVisible(owner)) GetWindowRect(owner, &area);
		else { const auto s = backend::ScreenRect(); area = {s.left, s.top, s.right, s.bottom}; }
		origin = {area.left + (area.right - area.left - width) / 2, area.top + (area.bottom - area.top - height) / 2};
	}
	const DWORD createStyle = style & ~(WS_VISIBLE | WS_DISABLED);
	HWND hDlg = CreateWindowExW(exStyle, cls.empty() ? L"#32770" : cls.c_str(), title.c_str(), createStyle, origin.x, origin.y, width, height,
	                            owner, nullptr, hInstance, nullptr);
	if (!hDlg) return nullptr;
	auto s = new DialogState;
	s->proc = proc;
	GetWindowObject(hDlg)->control = s;
	SetWindowLongPtrW(hDlg, DWLP_DLGPROC, reinterpret_cast<LONG_PTR>(proc));
	const HFONT font = DialogFont();
	int defId = 0;
	std::vector<HWND> groupBoxes;
	for (int i = 0; i < count; ++i) {
		r.Align();
		DWORD cStyle, cEx;
		int id;
		if (ex) r.Get<uint32_t>();
		if (ex) { cEx = r.Get<uint32_t>(); cStyle = r.Get<uint32_t>(); }
		else { cStyle = r.Get<uint32_t>(); cEx = r.Get<uint32_t>(); }
		const int ix = r.Get<int16_t>(), iy = r.Get<int16_t>(), icx = r.Get<int16_t>(), icy = r.Get<int16_t>();
		id = ex ? static_cast<int>(r.Get<uint32_t>()) : r.Get<uint16_t>();
		int atom = 0, titleId = -1;
		std::wstring cName = r.Sz(&atom);
		if (cName.empty()) cName = AtomClass(atom);
		const std::wstring cText = r.Sz(&titleId);
		const int extra = r.Get<uint16_t>();
		r.p += extra;
		const RECT ic = ScaleRect(ix, iy, icx, icy);
		HWND h = CreateWindowExW(cEx, cName.c_str(), cText.c_str(), cStyle, ic.left, ic.top, ic.right - ic.left, ic.bottom - ic.top, hDlg,
		                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance, nullptr);
		if (!h) continue;
		s->order.push_back(h);
		SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
		const UINT code = static_cast<UINT>(SendMessageW(h, WM_GETDLGCODE, 0, 0));
		if (code & DLGC_DEFPUSHBUTTON) defId = id;
		if (!_wcsicmp(cName.c_str(), L"BUTTON") && (cStyle & BS_TYPEMASK) == BS_GROUPBOX) groupBoxes.push_back(h);
	}
	if (defId) s->defId = defId;
	// グループボックスは背景を塗るので、Win32 のように中のコントロールを隠さないよう最背面へ
	{
		std::lock_guard lock(UserMutex());
		auto& ch = GetWindowObject(hDlg)->children;
		for (HWND g : groupBoxes) {
			ch.erase(std::remove(ch.begin(), ch.end(), g), ch.end());
			ch.push_back(g);
		}
	}

	HWND first = nullptr;
	for (HWND h : s->order) {
		if ((GetWindowLongW(h, GWL_STYLE) & (WS_TABSTOP | WS_DISABLED | WS_VISIBLE)) == (WS_TABSTOP | WS_VISIBLE)) { first = h; break; }
	}
	if (SendMessageW(hDlg, WM_INITDIALOG, reinterpret_cast<WPARAM>(first), lParam) && first && !s->ended && !asPage) DialogSetFocus(hDlg, first);
	if (!IsWindow(hDlg)) return nullptr;
	if (style & WS_DISABLED) EnableWindow(hDlg, FALSE);
	if ((style & WS_VISIBLE) && !s->ended) ShowWindow(hDlg, child ? SW_SHOWNA : SW_SHOW);
	return hDlg;
}

const void* FindTemplate(HINSTANCE hInstance, LPCWSTR name)
{
	HRSRC res = FindResourceW(hInstance, name, reinterpret_cast<LPCWSTR>(RT_DIALOG));
	if (!res) { ReportUnimplemented("user32", "dialog template not found"); return nullptr; }
	return LockResource(LoadResource(hInstance, res));
}

//! hDlg の直接の子を返す（コンボボックスの中のエディットなら、コンボボックス）
HWND DirectChild(HWND hDlg, HWND h)
{
	while (h && GetAncestor(h, GA_PARENT) != hDlg) h = GetAncestor(h, GA_PARENT);
	return h;
}

bool Usable(HWND h) { return IsWindowVisible(h) && IsWindowEnabled(h); }

//! タブ順。表示中の子ダイアログ（プロパティシートのページ）はその中のコントロールに展開する
void AppendFlat(HWND hDlg, std::vector<HWND>* out)
{
	auto s = StateOf(hDlg);
	if (!s) return;
	for (HWND h : s->order) {
		if (StateOf(h)) {
			if (IsWindowVisible(h)) AppendFlat(h, out);
		} else {
			out->push_back(h);
		}
	}
}

std::vector<HWND> FlatOrder(HWND hDlg)
{
	std::vector<HWND> out;
	AppendFlat(hDlg, &out);
	return out;
}

//! h を含むいちばん内側のダイアログ
HWND InnerDialog(HWND hDlg, HWND h)
{
	for (; h && h != hDlg; h = GetAncestor(h, GA_PARENT)) {
		if (StateOf(h)) return h;
	}
	return hDlg;
}

//! h または h の親のうち、list にあるもの（コンボボックスの中のエディットなら、コンボボックス）
int IndexIn(const std::vector<HWND>& list, HWND hDlg, HWND h)
{
	for (; h && h != hDlg; h = GetAncestor(h, GA_PARENT)) {
		const auto it = std::find(list.begin(), list.end(), h);
		if (it != list.end()) return static_cast<int>(it - list.begin());
	}
	return -1;
}

//! ラベルの "&X" のニーモニックを返す
wchar_t MnemonicOf(const std::wstring& text)
{
	for (size_t i = 0; i + 1 < text.size(); ++i) {
		if (text[i] != L'&') continue;
		if (text[i + 1] == L'&') { ++i; continue; }
		return static_cast<wchar_t>(towupper(text[i + 1]));
	}
	return 0;
}

std::wstring TextOf(HWND h)
{
	wchar_t buf[256] = {};
	GetWindowTextW(h, buf, 256);
	return buf;
}

bool IsStaticLabel(HWND h) { return (SendMessageW(h, WM_GETDLGCODE, 0, 0) & DLGC_STATIC) != 0; }

void ActivateControl(HWND hDlg, HWND h)
{
	const UINT code = static_cast<UINT>(SendMessageW(h, WM_GETDLGCODE, 0, 0));
	DialogSetFocus(hDlg, h);
	if (code & (DLGC_BUTTON | DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) SendMessageW(h, BM_CLICK, 0, 0);
}

bool HandleMnemonic(HWND hDlg, wchar_t key)
{
	key = static_cast<wchar_t>(towupper(key));
	const std::vector<HWND> order = FlatOrder(hDlg);
	for (size_t i = 0; i < order.size(); ++i) {
		HWND h = order[i];
		if (MnemonicOf(TextOf(h)) != key || !IsWindowVisible(h)) continue;
		// ラベルなら、その次のフォーカスを受けられるコントロールへ
		if (IsStaticLabel(h)) {
			for (size_t j = i + 1; j < order.size(); ++j) {
				if ((GetWindowLongW(order[j], GWL_STYLE) & WS_TABSTOP) && Usable(order[j])) { DialogSetFocus(hDlg, order[j]); return true; }
			}
			continue;
		}
		if (IsWindowEnabled(h)) { ActivateControl(hDlg, h); return true; }
	}
	return false;
}

}  // namespace

namespace winport {

HFONT DialogFont()
{
	// テンプレートの「ＭＳ Ｐゴシック 9pt」と同じ 12 ピクセルの mac のフォント
	static HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Hiragino Sans");
	return font;
}

void DialogSetFocus(HWND hDlg, HWND h)
{
	const UINT code = static_cast<UINT>(SendMessageW(h, WM_GETDLGCODE, 0, 0));
	SetFocus(h);
	if (code & DLGC_HASSETSEL) SendMessageW(h, EM_SETSEL, 0, -1);
	(void)hDlg;
}

HWND DialogCreatePage(HINSTANCE hInstance, const void* tmpl, HWND sheet, DLGPROC proc, LPARAM lParam)
{
	HWND page = CreateFromTemplate(hInstance, tmpl, sheet, proc, lParam, true);
	if (auto s = StateOf(sheet); s && page) s->order.insert(s->order.begin() + std::min<size_t>(1, s->order.size()), page);
	return page;
}

void DialogAdoptChildren(HWND hDlg)
{
	auto s = StateOf(hDlg);
	if (!s) return;
	std::vector<HWND> children;
	{
		std::lock_guard lock(UserMutex());
		children = GetWindowObject(hDlg)->children;
	}
	std::reverse(children.begin(), children.end());  // 子のリストは新しい順
	for (HWND h : children) {
		if (std::find(s->order.begin(), s->order.end(), h) == s->order.end()) s->order.push_back(h);
	}
}

std::vector<HWND> DialogGroupOf(HWND control)
{
	std::vector<HWND> group;
	auto s = StateOf(GetAncestor(control, GA_PARENT));
	if (!s) return group;
	const auto& o = s->order;
	const auto it = std::find(o.begin(), o.end(), control);
	if (it == o.end()) return group;
	size_t start = it - o.begin();
	while (start > 0 && !(GetWindowLongW(o[start], GWL_STYLE) & WS_GROUP)) --start;
	for (size_t i = start; i < o.size(); ++i) {
		if (i > start && (GetWindowLongW(o[i], GWL_STYLE) & WS_GROUP)) break;
		group.push_back(o[i]);
	}
	return group;
}

}  // namespace winport

extern "C" {

HWND WINAPI CreateDialogIndirectParamW(HINSTANCE hInstance, LPCDLGTEMPLATEW lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
	return CreateFromTemplate(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam);
}

HWND WINAPI CreateDialogParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
	const void* t = FindTemplate(hInstance, lpTemplateName);
	return t ? CreateFromTemplate(hInstance, t, hWndParent, lpDialogFunc, dwInitParam) : nullptr;
}

INT_PTR WINAPI DialogBoxIndirectParamW(HINSTANCE hInstance, LPCDLGTEMPLATEW hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
	const HWND owner = hWndParent ? GetAncestor(hWndParent, GA_ROOT) : nullptr;
	HWND hDlg = CreateFromTemplate(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);
	if (!hDlg) return -1;
	auto s = StateOf(hDlg);
	const bool ownerEnabled = owner && IsWindowEnabled(owner);
	if (ownerEnabled) EnableWindow(owner, FALSE);
	if (!s->ended) ShowWindow(hDlg, SW_SHOW);
	while (!s->ended && IsWindow(hDlg)) {
		MSG m;
		const int r = GetMessageW(&m, nullptr, 0, 0);
		if (r <= 0) { PostQuitMessage(static_cast<int>(m.wParam)); break; }
		if (!IsDialogMessageW(hDlg, &m)) {
			TranslateMessage(&m);
			DispatchMessageW(&m);
		}
	}
	const INT_PTR result = IsWindow(hDlg) ? s->result : -1;
	if (ownerEnabled) EnableWindow(owner, TRUE);
	if (IsWindow(hDlg)) DestroyWindow(hDlg);
	if (owner && IsWindowVisible(owner)) SetForegroundWindow(owner);
	return result;
}

INT_PTR WINAPI DialogBoxParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
{
	const void* t = FindTemplate(hInstance, lpTemplateName);
	return t ? DialogBoxIndirectParamW(hInstance, static_cast<LPCDLGTEMPLATEW>(t), hWndParent, lpDialogFunc, dwInitParam) : -1;
}

WINBOOL WINAPI EndDialog(HWND hDlg, INT_PTR nResult)
{
	auto s = StateOf(hDlg);
	if (!s) return FALSE;
	s->result = nResult;
	s->ended = true;
	ShowWindow(hDlg, SW_HIDE);
	return TRUE;
}

LRESULT WINAPI DefDlgProcW(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf(hDlg);
	if (Msg == WM_NCDESTROY && s) {
		delete s;
		GetWindowObject(hDlg)->control = nullptr;
		return DefWindowProcW(hDlg, Msg, wParam, lParam);
	}
	if (s && s->proc) {
		SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
		const INT_PTR handled = s->proc(hDlg, Msg, wParam, lParam);
		if (!IsWindow(hDlg)) return handled;
		switch (Msg) {
		case WM_INITDIALOG: case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
		case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC: case WM_COMPAREITEM: case WM_VKEYTOITEM:
		case WM_CHARTOITEM: case WM_QUERYDRAGICON:
			if (handled || Msg == WM_INITDIALOG) return handled;
			break;
		default:
			if (handled) return GetWindowLongPtrW(hDlg, DWLP_MSGRESULT);
			break;
		}
		s = StateOf(hDlg);
	}
	switch (Msg) {
	case WM_CLOSE:
		PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hDlg, IDCANCEL)));
		return 0;
	case WM_ACTIVATE:
		if (!s) break;
		if (LOWORD(wParam) == WA_INACTIVE) {
			const HWND f = GetFocus();
			if (f && IsChild(hDlg, f)) s->lastFocus = f;
		} else {
			HWND f = s->lastFocus && IsWindow(s->lastFocus) ? s->lastFocus : nullptr;
			if (!f) f = GetNextDlgTabItem(hDlg, nullptr, FALSE);
			if (f) SetFocus(f);
		}
		return 0;
	case WM_SETFOCUS:
		if (s && s->lastFocus && IsWindow(s->lastFocus)) SetFocus(s->lastFocus);
		return 0;
	case WM_GETFONT:
		return reinterpret_cast<LRESULT>(DialogFont());
	case DM_GETDEFID:
		return s ? MAKELRESULT(s->defId, DC_HASDEFID) : 0;
	case DM_SETDEFID:
		if (s) s->defId = static_cast<int>(wParam);
		return TRUE;
	case WM_NEXTDLGCTL:
		if (lParam) DialogSetFocus(hDlg, reinterpret_cast<HWND>(wParam));
		else if (HWND n = GetNextDlgTabItem(hDlg, GetFocus(), wParam != 0)) DialogSetFocus(hDlg, n);
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hDlg, Msg, wParam, lParam);
}

HWND WINAPI GetNextDlgTabItem(HWND hDlg, HWND hCtl, WINBOOL bPrevious)
{
	const std::vector<HWND> o = FlatOrder(hDlg);
	if (o.empty()) return nullptr;
	const int n = static_cast<int>(o.size());
	int i = hCtl ? IndexIn(o, hDlg, hCtl) : -1;
	if (i < 0) i = bPrevious ? 0 : n - 1;
	for (int k = 1; k <= n; ++k) {
		HWND h = o[((i + (bPrevious ? -k : k)) % n + n) % n];
		if ((GetWindowLongW(h, GWL_STYLE) & WS_TABSTOP) && Usable(h)) return h;
	}
	return nullptr;
}

HWND WINAPI GetNextDlgGroupItem(HWND hDlg, HWND hCtl, WINBOOL bPrevious)
{
	const HWND inner = InnerDialog(hDlg, hCtl);
	const HWND control = DirectChild(inner, hCtl);
	const auto group = DialogGroupOf(control);
	const auto it = std::find(group.begin(), group.end(), control);
	if (it == group.end()) return hCtl;
	const int n = static_cast<int>(group.size());
	const int i = static_cast<int>(it - group.begin());
	for (int k = 1; k <= n; ++k) {
		HWND h = group[((i + (bPrevious ? -k : k)) % n + n) % n];
		if (Usable(h) && !IsStaticLabel(h)) return h;
	}
	return hCtl;
}

WINBOOL WINAPI IsDialogMessageW(HWND hDlg, LPMSG lpMsg)
{
	if (!hDlg || !lpMsg->hwnd) return FALSE;
	if (lpMsg->hwnd != hDlg && !IsChild(hDlg, lpMsg->hwnd)) return FALSE;
	auto s = StateOf(hDlg);
	if (!s) return FALSE;
	if (lpMsg->message == WM_KEYDOWN) {
		const UINT code = static_cast<UINT>(SendMessageW(lpMsg->hwnd, WM_GETDLGCODE, lpMsg->wParam, reinterpret_cast<LPARAM>(lpMsg)));
		if (!(code & DLGC_WANTMESSAGE) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
			switch (lpMsg->wParam) {
			case VK_TAB:
				if (code & DLGC_WANTTAB) break;
				if (HWND n = GetNextDlgTabItem(hDlg, GetFocus(), (GetKeyState(VK_SHIFT) & 0x8000) != 0)) DialogSetFocus(hDlg, n);
				return TRUE;
			case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
				if (code & DLGC_WANTARROWS) break;
				if (HWND n = GetNextDlgGroupItem(hDlg, GetFocus(), lpMsg->wParam == VK_LEFT || lpMsg->wParam == VK_UP)) {
					DialogSetFocus(hDlg, n);
					if (SendMessageW(n, WM_GETDLGCODE, 0, 0) & DLGC_RADIOBUTTON) SendMessageW(n, BM_CLICK, 0, 0);
				}
				return TRUE;
			case VK_RETURN: {
				if (code & DLGC_WANTALLKEYS) break;
				HWND btn = (code & DLGC_UNDEFPUSHBUTTON) ? lpMsg->hwnd : GetDlgItem(hDlg, s->defId);
				if (btn && !IsWindowEnabled(btn)) return TRUE;
				// ページの中のプッシュボタンなら、シートではなくページへ
				SendMessageW(btn ? GetAncestor(btn, GA_PARENT) : hDlg, WM_COMMAND, MAKEWPARAM(btn ? GetDlgCtrlID(btn) : s->defId, BN_CLICKED), reinterpret_cast<LPARAM>(btn));
				return TRUE;
			}
			case VK_ESCAPE:
				if (code & DLGC_WANTALLKEYS) break;
				SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(hDlg, IDCANCEL)));
				return TRUE;
			default:
				break;
			}
		}
	} else if (lpMsg->message == WM_SYSKEYDOWN && lpMsg->wParam >= '0' && lpMsg->wParam <= 'Z') {
		// mac の Option+英字は別の文字になるので、WM_SYSCHAR ではなく仮想キーで見る
		if (HandleMnemonic(hDlg, static_cast<wchar_t>(lpMsg->wParam))) {
			MSG drop;
			PeekMessageW(&drop, lpMsg->hwnd, WM_SYSCHAR, WM_SYSCHAR, PM_REMOVE);
			return TRUE;
		}
	}
	TranslateMessage(lpMsg);
	DispatchMessageW(lpMsg);
	return TRUE;
}

/* ---- ダイアログ項目の操作 ---- */
LRESULT WINAPI SendDlgItemMessageW(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HWND h = GetDlgItem(hDlg, nIDDlgItem);
	return h ? SendMessageW(h, Msg, wParam, lParam) : 0;
}

UINT WINAPI GetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPWSTR lpString, int cchMax)
{
	HWND h = GetDlgItem(hDlg, nIDDlgItem);
	if (!h) { if (cchMax > 0) lpString[0] = 0; return 0; }
	return static_cast<UINT>(GetWindowTextW(h, lpString, cchMax));
}

WINBOOL WINAPI SetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPCWSTR lpString)
{
	HWND h = GetDlgItem(hDlg, nIDDlgItem);
	return h ? SetWindowTextW(h, lpString) : FALSE;
}

UINT WINAPI GetDlgItemInt(HWND hDlg, int nIDDlgItem, WINBOOL* lpTranslated, WINBOOL bSigned)
{
	wchar_t buf[64] = {};
	GetDlgItemTextW(hDlg, nIDDlgItem, buf, 64);
	wchar_t* end = nullptr;
	const long v = bSigned ? wcstol(buf, &end, 10) : static_cast<long>(wcstoul(buf, &end, 10));
	if (lpTranslated) *lpTranslated = end != buf;
	return end != buf ? static_cast<UINT>(v) : 0;
}

WINBOOL WINAPI SetDlgItemInt(HWND hDlg, int nIDDlgItem, UINT uValue, WINBOOL bSigned)
{
	const std::string a = bSigned ? std::to_string(static_cast<int>(uValue)) : std::to_string(uValue);
	return SetDlgItemTextW(hDlg, nIDDlgItem, std::wstring(a.begin(), a.end()).c_str());
}

WINBOOL WINAPI CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck)
{
	return SendDlgItemMessageW(hDlg, nIDButton, BM_SETCHECK, uCheck, 0), TRUE;
}

UINT WINAPI IsDlgButtonChecked(HWND hDlg, int nIDButton)
{
	return static_cast<UINT>(SendDlgItemMessageW(hDlg, nIDButton, BM_GETCHECK, 0, 0));
}

WINBOOL WINAPI CheckRadioButton(HWND hDlg, int nIDFirstButton, int nIDLastButton, int nIDCheckButton)
{
	for (int id = nIDFirstButton; id <= nIDLastButton; ++id) CheckDlgButton(hDlg, id, id == nIDCheckButton ? BST_CHECKED : BST_UNCHECKED);
	return TRUE;
}

LONG WINAPI GetDialogBaseUnits(void) { return MAKELONG(kBaseX, kBaseY); }

}  // extern "C"
