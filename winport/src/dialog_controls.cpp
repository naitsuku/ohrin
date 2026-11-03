/*
	dialog_controls.cpp — ダイアログのコントロール（BUTTON / STATIC / EDIT / LISTBOX / COMBOBOX / UpDown）

	上流がサブクラス化し、メッセージで操作するので、NSControl ではなく Win32 のウィンドウプロシージャとして
	実装し GDI で描く（controls.cpp と同じ方針）。ドロップダウンの一覧だけは TrackPopupMenu（NSMenu）を使う。
	枠の線はクライアント領域の中に自分で描くので、WS_BORDER / WS_EX_CLIENTEDGE は作成時に外す。

	SPDX-License-Identifier: Zlib
*/
#include "gdi_internal.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include "dialog_internal.h"
#include "winport_internal.h"

using namespace winport;

namespace {

constexpr COLORREF kAccent = RGB(0, 110, 230);
constexpr COLORREF kBorder = RGB(190, 190, 190);
constexpr COLORREF kDisabledText = RGB(150, 150, 150);

template <typename T>
T* StateOf(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? static_cast<T*>(w->control) : nullptr;
}

template <typename T>
T* Attach(HWND hwnd)
{
	auto s = new T;
	GetWindowObject(hwnd)->control = s;
	return s;
}

template <typename T>
void Detach(HWND hwnd)
{
	delete StateOf<T>(hwnd);
	GetWindowObject(hwnd)->control = nullptr;
}

void Fill(HDC hdc, const RECT& r, COLORREF c)
{
	HBRUSH b = CreateSolidBrush(c);
	FillRect(hdc, &r, b);
	DeleteObject(b);
}

void Frame(HDC hdc, const RECT& r, COLORREF c)
{
	HBRUSH b = CreateSolidBrush(c);
	FrameRect(hdc, &r, b);
	DeleteObject(b);
}

void Rounded(HDC hdc, const RECT& r, int radius, COLORREF fill, COLORREF line)
{
	HBRUSH b = CreateSolidBrush(fill);
	HPEN p = CreatePen(PS_SOLID, 1, line);
	HGDIOBJ ob = SelectObject(hdc, b), op = SelectObject(hdc, p);
	RoundRect(hdc, r.left, r.top, r.right, r.bottom, radius, radius);
	SelectObject(hdc, ob);
	SelectObject(hdc, op);
	DeleteObject(b);
	DeleteObject(p);
}

void Line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF c, int width = 1)
{
	HPEN p = CreatePen(PS_SOLID, width, c);
	HGDIOBJ op = SelectObject(hdc, p);
	MoveToEx(hdc, x1, y1, nullptr);
	LineTo(hdc, x2, y2);
	SelectObject(hdc, op);
	DeleteObject(p);
}

void Invalidate(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void Notify(HWND hwnd, int code)
{
	SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), code), reinterpret_cast<LPARAM>(hwnd));
}

void RemoveBorderStyles(HWND hwnd, bool* hadBorder)
{
	auto w = GetWindowObject(hwnd);
	*hadBorder = (w->style & WS_BORDER) || (w->exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
	w->style &= ~WS_BORDER;
	w->exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
}

//! "下検索(&D)" → "下検索(D)"。mac には下線の表示が無いので & だけを除く（&& は & 1 文字）
std::wstring StripAmp(const std::wstring& s)
{
	std::wstring out;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == L'&') {
			if (i + 1 < s.size() && s[i + 1] == L'&') { out += L'&'; ++i; }
			continue;
		}
		out += s[i];
	}
	return out;
}

SIZE Extent(HDC hdc, const wchar_t* s, size_t n)
{
	SIZE sz{};
	if (n) GetTextExtentPoint32W(hdc, s, static_cast<int>(n), &sz);
	return sz;
}

int LineHeight(HFONT font)
{
	HDC hdc = GetDC(nullptr);
	HGDIOBJ old = SelectObject(hdc, font);
	TEXTMETRICW tm{};
	GetTextMetricsW(hdc, &tm);
	SelectObject(hdc, old);
	ReleaseDC(nullptr, hdc);
	return std::max<int>(tm.tmHeight, 12);
}

//! 文字列を描く（縦は rc の中央）
void Label(HDC hdc, const std::wstring& text, const RECT& rc, COLORREF color, UINT align = DT_LEFT)
{
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, color);
	const SIZE sz = Extent(hdc, text.c_str(), text.size());
	int x = rc.left;
	if (align == DT_CENTER) x = (rc.left + rc.right - sz.cx) / 2;
	else if (align == DT_RIGHT) x = rc.right - sz.cx;
	TEXTMETRICW tm{};
	GetTextMetricsW(hdc, &tm);
	TextOutW(hdc, x, (rc.top + rc.bottom - tm.tmHeight) / 2, text.c_str(), static_cast<int>(text.size()));
}

std::wstring WindowText(HWND hwnd)
{
	auto w = GetWindowObject(hwnd);
	return w ? w->text : L"";
}

HBRUSH CtlColor(HWND hwnd, HDC hdc, UINT msg)
{
	return reinterpret_cast<HBRUSH>(SendMessageW(GetParent(hwnd), msg, reinterpret_cast<WPARAM>(hdc), reinterpret_cast<LPARAM>(hwnd)));
}

// ---- STATIC ----
struct Static {
	HFONT font = nullptr;
	HICON icon = nullptr;
};

LRESULT CALLBACK StaticProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<Static>(hwnd);
	switch (msg) {
	case WM_NCCREATE: {
		bool border;
		RemoveBorderStyles(hwnd, &border);
		Attach<Static>(hwnd);
		break;
	}
	case WM_NCDESTROY: Detach<Static>(hwnd); break;
	case WM_SETFONT: s->font = reinterpret_cast<HFONT>(wParam); if (lParam) Invalidate(hwnd); return 0;
	case WM_GETFONT: return reinterpret_cast<LRESULT>(s->font);
	case WM_GETDLGCODE: return DLGC_STATIC;
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE: Invalidate(hwnd); return 0;
	case WM_SETTEXT: {
		const LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
		Invalidate(hwnd);
		return r;
	}
	case STM_SETICON: case STM_SETIMAGE: {
		const HICON old = s->icon;
		s->icon = reinterpret_cast<HICON>(msg == STM_SETICON ? wParam : lParam);
		Invalidate(hwnd);
		return reinterpret_cast<LRESULT>(old);
	}
	case STM_GETICON: case STM_GETIMAGE: return reinterpret_cast<LRESULT>(s->icon);
	case WM_LBUTTONDOWN:
		if (GetWindowLongW(hwnd, GWL_STYLE) & SS_NOTIFY) Notify(hwnd, STN_CLICKED);
		return 0;
	case WM_NCHITTEST:
		return HTCLIENT;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		SelectObject(hdc, s->font ? s->font : DialogFont());
		SetTextColor(hdc, IsWindowEnabled(hwnd) ? RGB(0, 0, 0) : kDisabledText);
		SetBkMode(hdc, TRANSPARENT);
		HBRUSH bg = CtlColor(hwnd, hdc, WM_CTLCOLORSTATIC);
		if (bg) FillRect(hdc, &rc, bg);
		else Fill(hdc, rc, GetSysColor(COLOR_BTNFACE));
		const DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
		const DWORD type = style & SS_TYPEMASK;
		if (type == SS_ETCHEDHORZ) {
			Line(hdc, rc.left, rc.top, rc.right, rc.top, kBorder);
		} else if (type == SS_ETCHEDFRAME || type == SS_ETCHEDVERT || type == SS_BLACKFRAME || type == SS_GRAYFRAME || type == SS_WHITEFRAME) {
			Frame(hdc, rc, kBorder);
		} else if (type == SS_LEFT || type == SS_CENTER || type == SS_RIGHT || type == SS_LEFTNOWORDWRAP || type == SS_SIMPLE) {
			const std::wstring text = (style & SS_NOPREFIX) ? WindowText(hwnd) : StripAmp(WindowText(hwnd));
			if (!text.empty()) {
				UINT f = DT_WORDBREAK | DT_NOPREFIX;
				if (type == SS_CENTER) f |= DT_CENTER;
				else if (type == SS_RIGHT) f |= DT_RIGHT;
				if (style & SS_CENTERIMAGE) f = (f & ~DT_WORDBREAK) | DT_SINGLELINE | DT_VCENTER;
				DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rc, f);
			}
		}
		EndPaint(hwnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- BUTTON ----
struct Button {
	HFONT font = nullptr;
	int check = BST_UNCHECKED;
	bool pushed = false;    //!< 押し込み表示中
	bool tracking = false;  //!< マウスのボタンを押したまま
	bool focused = false;
};

DWORD ButtonType(HWND hwnd) { return GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK; }
bool IsPush(DWORD t) { return t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON; }
bool IsCheck(DWORD t) { return t == BS_CHECKBOX || t == BS_AUTOCHECKBOX || t == BS_3STATE || t == BS_AUTO3STATE; }
bool IsRadio(DWORD t) { return t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON; }

void ButtonClicked(HWND hwnd, Button* s)
{
	const DWORD t = ButtonType(hwnd);
	if (t == BS_AUTOCHECKBOX) s->check = s->check ? BST_UNCHECKED : BST_CHECKED;
	else if (t == BS_AUTO3STATE) s->check = (s->check + 1) % 3;
	else if (t == BS_AUTORADIOBUTTON) {
		for (HWND h : DialogGroupOf(hwnd)) {
			if (h != hwnd && ButtonType(h) == BS_AUTORADIOBUTTON) SendMessageW(h, BM_SETCHECK, BST_UNCHECKED, 0);
		}
		s->check = BST_CHECKED;
	}
	Invalidate(hwnd);
	Notify(hwnd, BN_CLICKED);
}

void PaintButton(HWND hwnd, Button* s, HDC hdc)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const DWORD t = ButtonType(hwnd);
	const bool enabled = IsWindowEnabled(hwnd);
	const COLORREF textColor = enabled ? RGB(0, 0, 0) : kDisabledText;
	SelectObject(hdc, s->font ? s->font : DialogFont());
	const std::wstring text = StripAmp(WindowText(hwnd));
	const COLORREF face = GetSysColor(COLOR_BTNFACE);
	if (t == BS_OWNERDRAW) {
		DRAWITEMSTRUCT di{ODT_BUTTON, static_cast<UINT>(GetDlgCtrlID(hwnd)), 0, ODA_DRAWENTIRE,
		                  static_cast<UINT>((s->pushed ? ODS_SELECTED : 0) | (s->focused ? ODS_FOCUS : 0) | (enabled ? 0 : ODS_DISABLED)), hwnd, hdc, rc, 0};
		SendMessageW(GetParent(hwnd), WM_DRAWITEM, di.CtlID, reinterpret_cast<LPARAM>(&di));
		return;
	}
	Fill(hdc, rc, face);
	if (t == BS_GROUPBOX) {
		const SIZE sz = Extent(hdc, text.c_str(), text.size());
		const int top = std::max<int>(sz.cy, 12) / 2;
		RECT box{rc.left, rc.top + top, rc.right, rc.bottom};
		Rounded(hdc, box, 6, face, kBorder);
		if (!text.empty()) {
			RECT label{rc.left + 8, rc.top, rc.left + 14 + sz.cx, rc.top + top * 2};
			Fill(hdc, label, face);
			InflateRect(&label, -3, 0);
			Label(hdc, text, label, textColor);
		}
		return;
	}
	if (IsCheck(t) || IsRadio(t)) {
		const int box = 13;
		const int y = (rc.bottom - rc.top - box) / 2;
		RECT b{rc.left + 1, y, rc.left + 1 + box, y + box};
		const bool on = s->check != BST_UNCHECKED;
		const COLORREF fill = on && enabled ? kAccent : (s->pushed ? RGB(225, 225, 225) : RGB(255, 255, 255));
		const COLORREF line = s->focused ? kAccent : (on && enabled ? kAccent : RGB(160, 160, 160));
		if (IsRadio(t)) {
			Rounded(hdc, b, box, fill, line);
			if (on) {
				RECT dot = b;
				InflateRect(&dot, -4, -4);
				Rounded(hdc, dot, 6, enabled ? RGB(255, 255, 255) : RGB(120, 120, 120), enabled ? RGB(255, 255, 255) : RGB(120, 120, 120));
			}
		} else {
			Rounded(hdc, b, 4, fill, line);
			if (s->check == BST_CHECKED) {
				const COLORREF mark = enabled ? RGB(255, 255, 255) : RGB(120, 120, 120);
				Line(hdc, b.left + 3, b.top + 6, b.left + 5, b.top + 9, mark, 2);
				Line(hdc, b.left + 5, b.top + 9, b.left + 10, b.top + 3, mark, 2);
			} else if (s->check == BST_INDETERMINATE) {
				Line(hdc, b.left + 3, b.top + 6, b.left + 10, b.top + 6, RGB(120, 120, 120), 2);
			}
		}
		RECT l{b.right + 5, rc.top, rc.right, rc.bottom};
		Label(hdc, text, l, textColor);
		return;
	}
	const bool def = t == BS_DEFPUSHBUTTON && enabled;
	RECT b = rc;
	InflateRect(&b, -1, -1);
	const COLORREF fill = s->pushed ? (def ? RGB(0, 90, 200) : RGB(215, 215, 215)) : (def ? kAccent : RGB(255, 255, 255));
	Rounded(hdc, b, 8, fill, s->focused ? kAccent : (def ? kAccent : kBorder));
	Label(hdc, text, b, def ? RGB(255, 255, 255) : textColor, DT_CENTER);
}

LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<Button>(hwnd);
	switch (msg) {
	case WM_NCCREATE: {
		bool border;
		RemoveBorderStyles(hwnd, &border);
		Attach<Button>(hwnd);
		break;
	}
	case WM_NCDESTROY: Detach<Button>(hwnd); break;
	case WM_SETFONT: s->font = reinterpret_cast<HFONT>(wParam); if (lParam) Invalidate(hwnd); return 0;
	case WM_GETFONT: return reinterpret_cast<LRESULT>(s->font);
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE: Invalidate(hwnd); return 0;
	case WM_SETTEXT: {
		const LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
		Invalidate(hwnd);
		return r;
	}
	case WM_GETDLGCODE: {
		const DWORD t = ButtonType(hwnd);
		if (t == BS_GROUPBOX) return DLGC_STATIC;
		if (t == BS_DEFPUSHBUTTON) return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
		if (t == BS_PUSHBUTTON) return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
		if (IsRadio(t)) return DLGC_BUTTON | DLGC_RADIOBUTTON;
		return DLGC_BUTTON;
	}
	case WM_SETFOCUS: s->focused = true; Invalidate(hwnd); return 0;
	case WM_KILLFOCUS: s->focused = false; s->pushed = false; Invalidate(hwnd); return 0;
	case BM_GETCHECK: return s->check;
	case BM_SETCHECK: s->check = static_cast<int>(wParam); Invalidate(hwnd); return 0;
	case BM_GETSTATE: return s->check | (s->pushed ? BST_PUSHED : 0) | (s->focused ? BST_FOCUS : 0);
	case BM_SETSTATE: s->pushed = wParam != 0; Invalidate(hwnd); return 0;
	case BM_SETSTYLE: {
		auto w = GetWindowObject(hwnd);
		w->style = (w->style & ~BS_TYPEMASK) | (wParam & BS_TYPEMASK);
		Invalidate(hwnd);
		return 0;
	}
	case BM_CLICK:
		if (IsWindowEnabled(hwnd)) ButtonClicked(hwnd, s);
		return 0;
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
		if (ButtonType(hwnd) == BS_GROUPBOX) return 0;
		SetFocus(hwnd);
		SetCapture(hwnd);
		s->tracking = s->pushed = true;
		Invalidate(hwnd);
		return 0;
	case WM_MOUSEMOVE:
		if (s->tracking) {
			RECT rc;
			GetClientRect(hwnd, &rc);
			const POINT pt{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
			const bool in = PtInRect(&rc, pt);
			if (in != s->pushed) { s->pushed = in; Invalidate(hwnd); }
		}
		return 0;
	case WM_LBUTTONUP:
		if (s->tracking) {
			const bool click = s->pushed;
			s->tracking = s->pushed = false;
			ReleaseCapture();
			Invalidate(hwnd);
			if (click) ButtonClicked(hwnd, s);
		}
		return 0;
	case WM_KEYDOWN:
		if (wParam == VK_SPACE) { s->pushed = true; Invalidate(hwnd); return 0; }
		break;
	case WM_KEYUP:
		if (wParam == VK_SPACE && s->pushed) { s->pushed = false; ButtonClicked(hwnd, s); return 0; }
		break;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		PaintButton(hwnd, s, hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- EDIT ----
struct Edit {
	std::wstring text;
	size_t anchor = 0, caret = 0;  //!< 選択の起点と現在位置（UTF-16 単位）
	HFONT font = nullptr;
	bool border = false;
	bool focused = false;
	bool dragging = false;
	bool modified = false;
	size_t limit = 0x7FFFFFFE;
	std::wstring undoText;
	size_t undoCaret = 0;
	bool canUndo = false;
	wchar_t passwordChar = 0;
	int scrollX = 0;
	int firstLine = 0;
	int lineHeight = 16;
	int margin = 3;
};

bool IsMulti(HWND hwnd) { return (GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) != 0; }

//! 各行の先頭の位置
std::vector<size_t> LineStarts(const Edit* s)
{
	std::vector<size_t> v{0};
	for (size_t i = 0; i < s->text.size(); ++i) {
		if (s->text[i] == L'\n') v.push_back(i + 1);
	}
	return v;
}

size_t LineEnd(const Edit* s, const std::vector<size_t>& starts, size_t line)
{
	size_t e = line + 1 < starts.size() ? starts[line + 1] - 1 : s->text.size();
	if (e > starts[line] && s->text[e - 1] == L'\r') --e;
	return e;
}

size_t LineOfPos(const std::vector<size_t>& starts, size_t pos)
{
	return std::upper_bound(starts.begin(), starts.end(), pos) - starts.begin() - 1;
}

std::wstring Shown(const Edit* s, size_t from, size_t to)
{
	return s->passwordChar ? std::wstring(to - from, s->passwordChar) : s->text.substr(from, to - from);
}

int EditTextX(HWND hwnd, const Edit* s, HDC hdc, size_t from, size_t pos)
{
	(void)hwnd;
	const std::wstring t = Shown(s, from, pos);
	return static_cast<int>(Extent(hdc, t.c_str(), t.size()).cx);
}

struct EditDC {
	HDC hdc;
	HGDIOBJ old;
	explicit EditDC(const Edit* s) : hdc(GetDC(nullptr)), old(SelectObject(hdc, s->font ? s->font : DialogFont())) {}
	~EditDC() { SelectObject(hdc, old); ReleaseDC(nullptr, hdc); }
};

int EditTop(HWND hwnd, const Edit* s)
{
	if (IsMulti(hwnd)) return s->margin;
	RECT rc;
	GetClientRect(hwnd, &rc);
	return (rc.bottom - s->lineHeight) / 2;
}

int VisibleLines(HWND hwnd, const Edit* s)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	return std::max(1, static_cast<int>(rc.bottom - s->margin * 2) / s->lineHeight);
}

void UpdateEditScrollBar(HWND hwnd, const Edit* s)
{
	if (!(GetWindowLongW(hwnd, GWL_STYLE) & WS_VSCROLL)) return;
	SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, static_cast<int>(LineStarts(s).size()) - 1, static_cast<UINT>(VisibleLines(hwnd, s)), s->firstLine};
	SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

void ScrollCaretIntoView(HWND hwnd, Edit* s)
{
	const auto starts = LineStarts(s);
	const size_t line = LineOfPos(starts, s->caret);
	RECT rc;
	GetClientRect(hwnd, &rc);
	EditDC dc(s);
	const int x = EditTextX(hwnd, s, dc.hdc, starts[line], s->caret);
	const int width = rc.right - s->margin * 2;
	if (x < s->scrollX) s->scrollX = std::max(0, x - width / 3);
	else if (x > s->scrollX + width - 2) s->scrollX = x - width + width / 3;
	if (IsMulti(hwnd)) {
		const int visible = VisibleLines(hwnd, s);
		if (static_cast<int>(line) < s->firstLine) s->firstLine = static_cast<int>(line);
		else if (static_cast<int>(line) >= s->firstLine + visible) s->firstLine = static_cast<int>(line) - visible + 1;
		UpdateEditScrollBar(hwnd, s);
	}
}

void UpdateEditCaret(HWND hwnd, Edit* s)
{
	if (!s->focused) return;
	const auto starts = LineStarts(s);
	const size_t line = LineOfPos(starts, s->caret);
	EditDC dc(s);
	const int x = s->margin + EditTextX(hwnd, s, dc.hdc, starts[line], s->caret) - s->scrollX;
	const int y = EditTop(hwnd, s) + (static_cast<int>(line) - s->firstLine) * s->lineHeight;
	SetCaretPos(x, y);
}

void EditChanged(HWND hwnd, Edit* s)
{
	ScrollCaretIntoView(hwnd, s);
	UpdateEditCaret(hwnd, s);
	Invalidate(hwnd);
}

void SelectEdit(HWND hwnd, Edit* s, size_t anchor, size_t caret)
{
	s->anchor = std::min(anchor, s->text.size());
	s->caret = std::min(caret, s->text.size());
	EditChanged(hwnd, s);
}

void SaveUndo(Edit* s)
{
	s->undoText = s->text;
	s->undoCaret = s->caret;
	s->canUndo = true;
}

void NotifyEditChange(HWND hwnd)
{
	Notify(hwnd, EN_UPDATE);
	Notify(hwnd, EN_CHANGE);
}

//! 選択範囲を text に置き換える
void EditReplace(HWND hwnd, Edit* s, std::wstring text, bool undoable = true)
{
	const size_t a = std::min(s->anchor, s->caret), b = std::max(s->anchor, s->caret);
	if (!IsMulti(hwnd)) {
		// 1 行の入力欄では最初の改行までを使う
		const size_t nl = text.find_first_of(L"\r\n");
		if (nl != std::wstring::npos) text.resize(nl);
	}
	const size_t room = s->limit - std::min(s->limit, s->text.size() - (b - a));
	if (text.size() > room) {
		text.resize(room);
		Notify(hwnd, EN_MAXTEXT);
	}
	if (text.empty() && a == b) return;
	if (undoable) SaveUndo(s);
	s->text.replace(a, b - a, text);
	s->anchor = s->caret = a + text.size();
	s->modified = true;
	EditChanged(hwnd, s);
	UpdateEditScrollBar(hwnd, s);
	NotifyEditChange(hwnd);
}

std::wstring EditSelection(const Edit* s)
{
	const size_t a = std::min(s->anchor, s->caret), b = std::max(s->anchor, s->caret);
	return s->text.substr(a, b - a);
}

void EditCopy(HWND hwnd, const Edit* s)
{
	const std::wstring sel = EditSelection(s);
	if (sel.empty() || s->passwordChar || !OpenClipboard(hwnd)) return;
	EmptyClipboard();
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (sel.size() + 1) * sizeof(wchar_t));
	auto p = static_cast<wchar_t*>(GlobalLock(h));
	wmemcpy(p, sel.c_str(), sel.size() + 1);
	GlobalUnlock(h);
	SetClipboardData(CF_UNICODETEXT, h);
	CloseClipboard();
}

void EditPaste(HWND hwnd, Edit* s)
{
	if (!OpenClipboard(hwnd)) return;
	std::wstring text;
	if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
		text = static_cast<const wchar_t*>(GlobalLock(h));
		GlobalUnlock(h);
	}
	CloseClipboard();
	EditReplace(hwnd, s, text);
}

size_t PrevPos(const Edit* s, size_t pos)
{
	if (!pos) return 0;
	--pos;
	if (pos && (s->text[pos] & 0xFC00) == 0xDC00 && (s->text[pos - 1] & 0xFC00) == 0xD800) --pos;
	if (pos && s->text[pos] == L'\n' && s->text[pos - 1] == L'\r') --pos;
	return pos;
}

size_t NextPos(const Edit* s, size_t pos)
{
	if (pos >= s->text.size()) return s->text.size();
	if ((s->text[pos] & 0xFC00) == 0xD800 && pos + 1 < s->text.size()) return pos + 2;
	if (s->text[pos] == L'\r' && pos + 1 < s->text.size() && s->text[pos + 1] == L'\n') return pos + 2;
	return pos + 1;
}

bool IsWordChar(wchar_t c) { return iswalnum(c) || c == L'_' || c > 0x7F; }

size_t WordLeft(const Edit* s, size_t pos)
{
	while (pos && !IsWordChar(s->text[pos - 1])) --pos;
	while (pos && IsWordChar(s->text[pos - 1])) --pos;
	return pos;
}

size_t WordRight(const Edit* s, size_t pos)
{
	const size_t n = s->text.size();
	while (pos < n && IsWordChar(s->text[pos])) ++pos;
	while (pos < n && !IsWordChar(s->text[pos])) ++pos;
	return pos;
}

size_t PosFromPoint(HWND hwnd, const Edit* s, int x, int y)
{
	const auto starts = LineStarts(s);
	int line = s->firstLine + (y - EditTop(hwnd, s)) / s->lineHeight;
	line = std::clamp(line, 0, static_cast<int>(starts.size()) - 1);
	const size_t from = starts[line], end = LineEnd(s, starts, line);
	const int target = x - s->margin + s->scrollX;
	EditDC dc(s);
	int prev = 0;
	for (size_t i = from; i < end;) {
		const size_t next = NextPos(s, i);
		const int w = EditTextX(hwnd, s, dc.hdc, from, std::min(next, end));
		if (target < (prev + w) / 2) return i;
		prev = w;
		i = next;
	}
	return end;
}

void MoveCaretKey(HWND hwnd, Edit* s, size_t pos, bool extend)
{
	SelectEdit(hwnd, s, extend ? s->anchor : pos, pos);
}

bool EditKeyDown(HWND hwnd, Edit* s, WPARAM vk)
{
	const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0, ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	const auto starts = LineStarts(s);
	const size_t line = LineOfPos(starts, s->caret);
	const bool hasSel = s->anchor != s->caret;
	switch (vk) {
	case VK_LEFT:
		if (hasSel && !shift && !ctrl) MoveCaretKey(hwnd, s, std::min(s->anchor, s->caret), false);
		else MoveCaretKey(hwnd, s, ctrl ? WordLeft(s, s->caret) : PrevPos(s, s->caret), shift);
		return true;
	case VK_RIGHT:
		if (hasSel && !shift && !ctrl) MoveCaretKey(hwnd, s, std::max(s->anchor, s->caret), false);
		else MoveCaretKey(hwnd, s, ctrl ? WordRight(s, s->caret) : NextPos(s, s->caret), shift);
		return true;
	case VK_HOME: MoveCaretKey(hwnd, s, ctrl ? 0 : starts[line], shift); return true;
	case VK_END: MoveCaretKey(hwnd, s, ctrl ? s->text.size() : LineEnd(s, starts, line), shift); return true;
	case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: {
		if (!IsMulti(hwnd)) return false;
		EditDC dc(s);
		const int x = EditTextX(hwnd, s, dc.hdc, starts[line], s->caret);
		const int step = (vk == VK_UP || vk == VK_DOWN) ? 1 : VisibleLines(hwnd, s);
		const int target = std::clamp(static_cast<int>(line) + ((vk == VK_UP || vk == VK_PRIOR) ? -step : step), 0, static_cast<int>(starts.size()) - 1);
		const int y = EditTop(hwnd, s) + (target - s->firstLine) * s->lineHeight;
		MoveCaretKey(hwnd, s, PosFromPoint(hwnd, s, x + s->margin - s->scrollX, y), shift);
		return true;
	}
	case VK_DELETE:
		if (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY) return true;
		if (!hasSel) s->anchor = ctrl ? WordRight(s, s->caret) : NextPos(s, s->caret);
		EditReplace(hwnd, s, L"");
		return true;
	default:
		return false;
	}
}

void EditChar(HWND hwnd, Edit* s, wchar_t c)
{
	const DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
	const bool readOnly = (style & ES_READONLY) != 0;
	switch (c) {
	case 1: SelectEdit(hwnd, s, 0, s->text.size()); return;        // Ctrl+A
	case 3: EditCopy(hwnd, s); return;                             // Ctrl+C
	case 22: if (!readOnly) EditPaste(hwnd, s); return;            // Ctrl+V
	case 24:                                                       // Ctrl+X
		if (readOnly) return;
		EditCopy(hwnd, s);
		EditReplace(hwnd, s, L"");
		return;
	case 26: if (!readOnly) SendMessageW(hwnd, EM_UNDO, 0, 0); return;  // Ctrl+Z
	case L'\b':
		if (readOnly) return;
		if (s->anchor == s->caret) s->anchor = PrevPos(s, s->caret);
		EditReplace(hwnd, s, L"");
		return;
	case L'\r':
		if (!readOnly && IsMulti(hwnd) && (style & ES_WANTRETURN)) EditReplace(hwnd, s, L"\r\n");
		return;
	default:
		break;
	}
	if (readOnly || c < 0x20 || c == 0x7F) return;
	if ((style & ES_NUMBER) && !(c >= L'0' && c <= L'9')) { MessageBeep(0); return; }
	wchar_t ch = c;
	if (style & ES_UPPERCASE) ch = static_cast<wchar_t>(towupper(ch));
	if (style & ES_LOWERCASE) ch = static_cast<wchar_t>(towlower(ch));
	EditReplace(hwnd, s, std::wstring(1, ch));
}

void PaintEdit(HWND hwnd, Edit* s, HDC hdc)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const bool enabled = IsWindowEnabled(hwnd);
	const bool readOnly = (GetWindowLongW(hwnd, GWL_STYLE) & ES_READONLY) != 0;
	SelectObject(hdc, s->font ? s->font : DialogFont());
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, enabled ? RGB(0, 0, 0) : kDisabledText);
	HBRUSH custom = CtlColor(hwnd, hdc, (readOnly || !enabled) ? WM_CTLCOLORSTATIC : WM_CTLCOLOREDIT);
	if (custom) FillRect(hdc, &rc, custom);
	else Fill(hdc, rc, (readOnly || !enabled) ? GetSysColor(COLOR_BTNFACE) : RGB(255, 255, 255));
	const auto starts = LineStarts(s);
	const size_t a = std::min(s->anchor, s->caret), b = std::max(s->anchor, s->caret);
	const int top = EditTop(hwnd, s);
	const DWORD align = GetWindowLongW(hwnd, GWL_STYLE) & (ES_RIGHT | ES_CENTER);
	for (int line = s->firstLine; line < static_cast<int>(starts.size()); ++line) {
		const int y = top + (line - s->firstLine) * s->lineHeight;
		if (y >= rc.bottom) break;
		const size_t from = starts[line], to = LineEnd(s, starts, line);
		const std::wstring t = Shown(s, from, to);
		int x = s->margin - s->scrollX;
		if (align && !IsMulti(hwnd)) {
			const int w = static_cast<int>(Extent(hdc, t.c_str(), t.size()).cx);
			if (align & ES_RIGHT) x = rc.right - s->margin - w;
			else x = (rc.right - w) / 2;
		}
		TextOutW(hdc, x, y, t.c_str(), static_cast<int>(t.size()));
		const size_t sa = std::clamp(a, from, to), sb = std::clamp(b, from, to);
		if (sa < sb && (s->focused || (GetWindowLongW(hwnd, GWL_STYLE) & ES_NOHIDESEL))) {
			const int x1 = x + EditTextX(hwnd, s, hdc, from, sa), x2 = x + EditTextX(hwnd, s, hdc, from, sb);
			RECT sel{x1, y, x2, y + s->lineHeight};
			Fill(hdc, sel, RGB(179, 215, 255));
			const std::wstring part = Shown(s, sa, sb);
			TextOutW(hdc, x1, y, part.c_str(), static_cast<int>(part.size()));
		}
	}
	if (s->border) Frame(hdc, rc, s->focused ? kAccent : kBorder);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<Edit>(hwnd);
	switch (msg) {
	case WM_NCCREATE: {
		s = Attach<Edit>(hwnd);
		RemoveBorderStyles(hwnd, &s->border);
		auto cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
		if (cs->lpszName) s->text = cs->lpszName;
		s->lineHeight = LineHeight(DialogFont());
		if (cs->style & ES_PASSWORD) s->passwordChar = L'\x2022';
		break;
	}
	case WM_NCDESTROY: Detach<Edit>(hwnd); break;
	case WM_SETFONT:
		s->font = reinterpret_cast<HFONT>(wParam);
		s->lineHeight = LineHeight(s->font ? s->font : DialogFont());
		if (lParam) Invalidate(hwnd);
		return 0;
	case WM_GETFONT: return reinterpret_cast<LRESULT>(s->font);
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE: Invalidate(hwnd); return 0;
	case WM_SIZE: UpdateEditScrollBar(hwnd, s); Invalidate(hwnd); break;
	case WM_GETDLGCODE: {
		LRESULT code = DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS;
		if (IsMulti(hwnd)) code |= DLGC_WANTALLKEYS;
		// Esc は複数行でもダイアログへ渡す
		if (lParam && reinterpret_cast<const MSG*>(lParam)->wParam == VK_ESCAPE) code &= ~DLGC_WANTALLKEYS;
		if (lParam && reinterpret_cast<const MSG*>(lParam)->wParam == VK_TAB) code &= ~DLGC_WANTALLKEYS;
		return code;
	}
	case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_IBEAM))); return TRUE;
	case WM_SETFOCUS:
		s->focused = true;
		CreateCaret(hwnd, nullptr, 1, s->lineHeight);
		UpdateEditCaret(hwnd, s);
		ShowCaret(hwnd);
		Invalidate(hwnd);
		Notify(hwnd, EN_SETFOCUS);
		return 0;
	case WM_KILLFOCUS:
		s->focused = false;
		HideCaret(hwnd);
		DestroyCaret();
		Invalidate(hwnd);
		Notify(hwnd, EN_KILLFOCUS);
		return 0;
	case WM_SETTEXT:
		s->text = lParam ? reinterpret_cast<LPCWSTR>(lParam) : L"";
		GetWindowObject(hwnd)->text = s->text;
		s->anchor = s->caret = 0;
		s->scrollX = 0;
		s->firstLine = 0;
		s->modified = false;
		s->canUndo = false;
		UpdateEditScrollBar(hwnd, s);
		UpdateEditCaret(hwnd, s);
		Invalidate(hwnd);
		NotifyEditChange(hwnd);
		return TRUE;
	case WM_GETTEXT: {
		if (!wParam) return 0;
		const size_t n = std::min<size_t>(wParam - 1, s->text.size());
		wmemcpy(reinterpret_cast<LPWSTR>(lParam), s->text.c_str(), n);
		reinterpret_cast<LPWSTR>(lParam)[n] = 0;
		return static_cast<LRESULT>(n);
	}
	case WM_GETTEXTLENGTH: return static_cast<LRESULT>(s->text.size());
	case WM_KEYDOWN: {
		const HWND parent = GetParent(hwnd);
		auto pw = GetWindowObject(parent);
		if (pw && !_wcsicmp(pw->cls->name.c_str(), L"COMBOBOX") && (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_F4)) {
			return SendMessageW(parent, msg, wParam, lParam);
		}
		if (EditKeyDown(hwnd, s, wParam)) return 0;
		break;
	}
	case WM_SYSKEYDOWN: {
		const HWND parent = GetParent(hwnd);
		auto pw = GetWindowObject(parent);
		if (pw && !_wcsicmp(pw->cls->name.c_str(), L"COMBOBOX") && wParam == VK_DOWN) return SendMessageW(parent, msg, wParam, lParam);
		break;
	}
	case WM_CHAR: case WM_IME_CHAR: EditChar(hwnd, s, static_cast<wchar_t>(wParam)); return 0;
	case WM_LBUTTONDOWN: {
		SetFocus(hwnd);
		SetCapture(hwnd);
		s->dragging = true;
		const size_t pos = PosFromPoint(hwnd, s, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
		SelectEdit(hwnd, s, (GetKeyState(VK_SHIFT) & 0x8000) ? s->anchor : pos, pos);
		return 0;
	}
	case WM_LBUTTONDBLCLK: {
		size_t pos = PosFromPoint(hwnd, s, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
		size_t a = pos, b = pos;
		while (a && IsWordChar(s->text[a - 1])) --a;
		while (b < s->text.size() && IsWordChar(s->text[b])) ++b;
		SelectEdit(hwnd, s, a, b);
		return 0;
	}
	case WM_MOUSEMOVE:
		if (s->dragging) SelectEdit(hwnd, s, s->anchor, PosFromPoint(hwnd, s, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))));
		return 0;
	case WM_LBUTTONUP:
		if (s->dragging) { s->dragging = false; ReleaseCapture(); }
		return 0;
	case WM_MOUSEWHEEL:
		if (IsMulti(hwnd)) {
			const int lines = static_cast<int>(LineStarts(s).size());
			s->firstLine = std::clamp(s->firstLine + (static_cast<short>(HIWORD(wParam)) > 0 ? -3 : 3), 0, std::max(0, lines - VisibleLines(hwnd, s)));
			UpdateEditScrollBar(hwnd, s);
			UpdateEditCaret(hwnd, s);
			Invalidate(hwnd);
			return 0;
		}
		break;
	case WM_VSCROLL: {
		const int lines = static_cast<int>(LineStarts(s).size());
		int first = s->firstLine;
		switch (LOWORD(wParam)) {
		case SB_LINEUP: --first; break;
		case SB_LINEDOWN: ++first; break;
		case SB_PAGEUP: first -= VisibleLines(hwnd, s); break;
		case SB_PAGEDOWN: first += VisibleLines(hwnd, s); break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: first = static_cast<short>(HIWORD(wParam)); break;
		default: break;
		}
		s->firstLine = std::clamp(first, 0, std::max(0, lines - VisibleLines(hwnd, s)));
		UpdateEditScrollBar(hwnd, s);
		UpdateEditCaret(hwnd, s);
		Invalidate(hwnd);
		return 0;
	}
	case WM_COPY: EditCopy(hwnd, s); return 0;
	case WM_CUT: EditCopy(hwnd, s); EditReplace(hwnd, s, L""); return 0;
	case WM_PASTE: EditPaste(hwnd, s); return 0;
	case WM_CLEAR: EditReplace(hwnd, s, L""); return 0;
	case WM_UNDO: case EM_UNDO:
		if (!s->canUndo) return FALSE;
		std::swap(s->text, s->undoText);
		s->anchor = s->caret = std::min(s->undoCaret, s->text.size());
		EditChanged(hwnd, s);
		NotifyEditChange(hwnd);
		return TRUE;
	case EM_CANUNDO: return s->canUndo;
	case EM_EMPTYUNDOBUFFER: s->canUndo = false; return 0;
	case EM_GETSEL: {
		const DWORD a = static_cast<DWORD>(std::min(s->anchor, s->caret)), b = static_cast<DWORD>(std::max(s->anchor, s->caret));
		if (wParam) *reinterpret_cast<DWORD*>(wParam) = a;
		if (lParam) *reinterpret_cast<DWORD*>(lParam) = b;
		return MAKELRESULT(a, b);
	}
	case EM_SETSEL: {
		const auto start = static_cast<long>(static_cast<int>(wParam)), end = static_cast<long>(static_cast<int>(lParam));
		if (start < 0) { SelectEdit(hwnd, s, s->caret, s->caret); return 0; }
		SelectEdit(hwnd, s, static_cast<size_t>(start), end < 0 ? s->text.size() : static_cast<size_t>(end));
		return 0;
	}
	case EM_REPLACESEL: EditReplace(hwnd, s, reinterpret_cast<LPCWSTR>(lParam), wParam != 0); return 0;
	case EM_SETLIMITTEXT: s->limit = wParam ? wParam : 0x7FFFFFFE; return 0;
	case EM_GETLIMITTEXT: return static_cast<LRESULT>(s->limit);
	case EM_SETREADONLY: {
		auto w = GetWindowObject(hwnd);
		if (wParam) w->style |= ES_READONLY;
		else w->style &= ~ES_READONLY;
		Invalidate(hwnd);
		return TRUE;
	}
	case EM_GETMODIFY: return s->modified;
	case EM_SETMODIFY: s->modified = wParam != 0; return 0;
	case EM_SETPASSWORDCHAR: s->passwordChar = static_cast<wchar_t>(wParam); Invalidate(hwnd); return 0;
	case EM_GETLINECOUNT: return static_cast<LRESULT>(LineStarts(s).size());
	case EM_GETFIRSTVISIBLELINE: return s->firstLine;
	case EM_LINEINDEX: {
		const auto starts = LineStarts(s);
		const size_t line = static_cast<int>(wParam) < 0 ? LineOfPos(starts, s->caret) : wParam;
		return line < starts.size() ? static_cast<LRESULT>(starts[line]) : -1;
	}
	case EM_LINEFROMCHAR: return static_cast<LRESULT>(LineOfPos(LineStarts(s), static_cast<int>(wParam) < 0 ? s->caret : wParam));
	case EM_LINELENGTH: {
		const auto starts = LineStarts(s);
		const size_t line = LineOfPos(starts, static_cast<int>(wParam) < 0 ? s->caret : wParam);
		return static_cast<LRESULT>(LineEnd(s, starts, line) - starts[line]);
	}
	case EM_GETLINE: {
		const auto starts = LineStarts(s);
		if (wParam >= starts.size()) return 0;
		auto buf = reinterpret_cast<LPWSTR>(lParam);
		const size_t room = static_cast<WORD>(buf[0]);
		const size_t n = std::min(room, LineEnd(s, starts, wParam) - starts[wParam]);
		wmemcpy(buf, s->text.c_str() + starts[wParam], n);
		return static_cast<LRESULT>(n);
	}
	case EM_LINESCROLL: {
		const int lines = static_cast<int>(LineStarts(s).size());
		s->firstLine = std::clamp(s->firstLine + static_cast<int>(lParam), 0, std::max(0, lines - 1));
		UpdateEditScrollBar(hwnd, s);
		Invalidate(hwnd);
		return TRUE;
	}
	case EM_SCROLLCARET: ScrollCaretIntoView(hwnd, s); UpdateEditCaret(hwnd, s); Invalidate(hwnd); return TRUE;
	case EM_SETMARGINS: return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		PaintEdit(hwnd, s, hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- LISTBOX ----
struct ListItem {
	std::wstring text;
	LPARAM data = 0;
	bool selected = false;
};

struct ListBox {
	std::vector<ListItem> items;
	int cur = -1;     //!< 単一選択の選択項目。複数選択ではフォーカス項目
	int anchor = -1;
	int top = 0;
	int itemHeight = 17;
	HFONT font = nullptr;
	bool border = false;
	bool focused = false;
	bool dragging = false;
};

bool ListMulti(HWND hwnd) { return (GetWindowLongW(hwnd, GWL_STYLE) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0; }
bool ListOwnerDraw(HWND hwnd) { return (GetWindowLongW(hwnd, GWL_STYLE) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0; }
bool ListHasStrings(HWND hwnd) { return !ListOwnerDraw(hwnd) || (GetWindowLongW(hwnd, GWL_STYLE) & LBS_HASSTRINGS); }

int ListVisible(HWND hwnd, const ListBox* s)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	return std::max(1, static_cast<int>(rc.bottom) / s->itemHeight);
}

void UpdateListScroll(HWND hwnd, ListBox* s)
{
	const int visible = ListVisible(hwnd, s);
	s->top = std::clamp(s->top, 0, std::max(0, static_cast<int>(s->items.size()) - visible));
	if (GetWindowLongW(hwnd, GWL_STYLE) & WS_VSCROLL) {
		SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, static_cast<int>(s->items.size()) - 1), static_cast<UINT>(visible), s->top};
		SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
	}
	Invalidate(hwnd);
}

void ListEnsureVisible(HWND hwnd, ListBox* s, int i)
{
	if (i < 0) return;
	const int visible = ListVisible(hwnd, s);
	if (i < s->top) s->top = i;
	else if (i >= s->top + visible) s->top = i - visible + 1;
	UpdateListScroll(hwnd, s);
}

void ListSelect(HWND hwnd, ListBox* s, int i)
{
	if (!ListMulti(hwnd)) for (auto& it : s->items) it.selected = false;
	if (i >= 0 && i < static_cast<int>(s->items.size())) s->items[i].selected = true;
	s->cur = i;
	ListEnsureVisible(hwnd, s, i);
	Invalidate(hwnd);
}

int ListHit(HWND hwnd, const ListBox* s, int y)
{
	(void)hwnd;
	const int i = s->top + y / s->itemHeight;
	return y >= 0 && i < static_cast<int>(s->items.size()) ? i : -1;
}

int ListFind(const ListBox* s, int after, const wchar_t* text, bool exact)
{
	const int n = static_cast<int>(s->items.size());
	const size_t len = wcslen(text);
	for (int k = 1; k <= n; ++k) {
		const int i = (after + k + n) % n;
		const auto& t = s->items[i].text;
		if (exact ? !_wcsicmp(t.c_str(), text) : !_wcsnicmp(t.c_str(), text, len)) return i;
	}
	return LB_ERR;
}

void PaintList(HWND hwnd, ListBox* s, HDC hdc)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	SelectObject(hdc, s->font ? s->font : DialogFont());
	Fill(hdc, rc, RGB(255, 255, 255));
	const bool enabled = IsWindowEnabled(hwnd);
	for (int i = s->top; i < static_cast<int>(s->items.size()); ++i) {
		const int y = (i - s->top) * s->itemHeight;
		if (y >= rc.bottom) break;
		RECT r{rc.left, y, rc.right, y + s->itemHeight};
		const auto& it = s->items[i];
		if (ListOwnerDraw(hwnd)) {
			DRAWITEMSTRUCT di{ODT_LISTBOX, static_cast<UINT>(GetDlgCtrlID(hwnd)), static_cast<UINT>(i), ODA_DRAWENTIRE,
			                  static_cast<UINT>((it.selected ? ODS_SELECTED : 0) | (s->focused && i == s->cur ? ODS_FOCUS : 0)), hwnd, hdc, r, static_cast<ULONG_PTR>(it.data)};
			SendMessageW(GetParent(hwnd), WM_DRAWITEM, di.CtlID, reinterpret_cast<LPARAM>(&di));
			continue;
		}
		if (it.selected) Fill(hdc, r, s->focused ? kAccent : RGB(210, 210, 210));
		RECT t = r;
		t.left += 4;
		Label(hdc, it.text, t, !enabled ? kDisabledText : (it.selected && s->focused ? RGB(255, 255, 255) : RGB(0, 0, 0)));
	}
	if (s->border) Frame(hdc, rc, s->focused ? kAccent : kBorder);
}

LRESULT ListInsert(HWND hwnd, ListBox* s, int at, LPARAM lParam)
{
	ListItem it;
	if (ListHasStrings(hwnd)) it.text = reinterpret_cast<LPCWSTR>(lParam);
	else it.data = lParam;
	if (at < 0 || at > static_cast<int>(s->items.size())) at = static_cast<int>(s->items.size());
	s->items.insert(s->items.begin() + at, std::move(it));
	if (s->cur >= at) ++s->cur;
	UpdateListScroll(hwnd, s);
	return at;
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<ListBox>(hwnd);
	const int count = s ? static_cast<int>(s->items.size()) : 0;
	const int idx = static_cast<int>(wParam);
	switch (msg) {
	case WM_NCCREATE:
		s = Attach<ListBox>(hwnd);
		RemoveBorderStyles(hwnd, &s->border);
		s->itemHeight = LineHeight(DialogFont()) + 3;
		break;
	case WM_CREATE:
		if (ListOwnerDraw(hwnd)) {
			MEASUREITEMSTRUCT mi{ODT_LISTBOX, static_cast<UINT>(GetDlgCtrlID(hwnd)), 0, 0, static_cast<UINT>(s->itemHeight), 0};
			SendMessageW(GetParent(hwnd), WM_MEASUREITEM, mi.CtlID, reinterpret_cast<LPARAM>(&mi));
			if (mi.itemHeight) s->itemHeight = static_cast<int>(mi.itemHeight);
		}
		break;
	case WM_NCDESTROY: Detach<ListBox>(hwnd); break;
	case WM_SETFONT:
		s->font = reinterpret_cast<HFONT>(wParam);
		if (!ListOwnerDraw(hwnd)) s->itemHeight = LineHeight(s->font ? s->font : DialogFont()) + 3;
		UpdateListScroll(hwnd, s);
		return 0;
	case WM_GETFONT: return reinterpret_cast<LRESULT>(s->font);
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE: Invalidate(hwnd); return 0;
	case WM_SIZE: UpdateListScroll(hwnd, s); break;
	case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
	case WM_SETFOCUS:
		s->focused = true;
		Invalidate(hwnd);
		if (GetWindowLongW(hwnd, GWL_STYLE) & LBS_NOTIFY) Notify(hwnd, LBN_SETFOCUS);
		return 0;
	case WM_KILLFOCUS:
		s->focused = false;
		Invalidate(hwnd);
		if (GetWindowLongW(hwnd, GWL_STYLE) & LBS_NOTIFY) Notify(hwnd, LBN_KILLFOCUS);
		return 0;
	case LB_GETCOUNT: return count;
	case LB_ADDSTRING: {
		int at = count;
		if ((GetWindowLongW(hwnd, GWL_STYLE) & LBS_SORT) && ListHasStrings(hwnd)) {
			const auto text = reinterpret_cast<LPCWSTR>(lParam);
			at = static_cast<int>(std::find_if(s->items.begin(), s->items.end(), [&](const ListItem& i) { return _wcsicmp(i.text.c_str(), text) > 0; }) - s->items.begin());
		}
		return ListInsert(hwnd, s, at, lParam);
	}
	case LB_INSERTSTRING: return ListInsert(hwnd, s, idx, lParam);
	case LB_DELETESTRING:
		if (idx < 0 || idx >= count) return LB_ERR;
		s->items.erase(s->items.begin() + idx);
		if (s->cur == idx) s->cur = -1;
		else if (s->cur > idx) --s->cur;
		UpdateListScroll(hwnd, s);
		return count - 1;
	case LB_RESETCONTENT:
		s->items.clear();
		s->cur = -1;
		s->top = 0;
		UpdateListScroll(hwnd, s);
		return 0;
	case LB_GETCURSEL:
		if (ListMulti(hwnd)) return s->cur;
		return s->cur >= 0 ? s->cur : LB_ERR;
	case LB_SETCURSEL:
		ListSelect(hwnd, s, idx < count ? idx : -1);
		return idx < count ? idx : LB_ERR;
	case LB_GETTEXT:
		if (idx < 0 || idx >= count) return LB_ERR;
		if (!ListHasStrings(hwnd)) { *reinterpret_cast<LPARAM*>(lParam) = s->items[idx].data; return sizeof(LPARAM); }
		wcscpy(reinterpret_cast<LPWSTR>(lParam), s->items[idx].text.c_str());
		return static_cast<LRESULT>(s->items[idx].text.size());
	case LB_GETTEXTLEN:
		return idx >= 0 && idx < count ? static_cast<LRESULT>(s->items[idx].text.size()) : LB_ERR;
	case LB_GETITEMDATA: return idx >= 0 && idx < count ? s->items[idx].data : LB_ERR;
	case LB_SETITEMDATA:
		if (idx < 0 || idx >= count) return LB_ERR;
		s->items[idx].data = lParam;
		return 0;
	case LB_FINDSTRING: case LB_FINDSTRINGEXACT:
		return count ? ListFind(s, idx, reinterpret_cast<LPCWSTR>(lParam), msg == LB_FINDSTRINGEXACT) : LB_ERR;
	case LB_SELECTSTRING: {
		const int i = count ? ListFind(s, idx, reinterpret_cast<LPCWSTR>(lParam), false) : LB_ERR;
		if (i != LB_ERR) ListSelect(hwnd, s, i);
		return i;
	}
	case LB_GETTOPINDEX: return s->top;
	case LB_SETTOPINDEX: s->top = idx; UpdateListScroll(hwnd, s); return 0;
	case LB_GETITEMHEIGHT: return s->itemHeight;
	case LB_SETITEMHEIGHT: s->itemHeight = std::max(1, static_cast<int>(LOWORD(lParam))); UpdateListScroll(hwnd, s); return 0;
	case LB_GETSEL: return idx >= 0 && idx < count ? s->items[idx].selected : LB_ERR;
	case LB_SETSEL: {
		const int i = static_cast<int>(lParam);
		for (int k = 0; k < count; ++k) if (i < 0 || k == i) s->items[k].selected = wParam != 0;
		Invalidate(hwnd);
		return 0;
	}
	case LB_GETSELCOUNT: return std::count_if(s->items.begin(), s->items.end(), [](const ListItem& i) { return i.selected; });
	case LB_GETSELITEMS: {
		int n = 0;
		for (int i = 0; i < count && n < idx; ++i) if (s->items[i].selected) reinterpret_cast<int*>(lParam)[n++] = i;
		return n;
	}
	case LB_GETCARETINDEX: return s->cur;
	case LB_SETCARETINDEX: s->cur = idx; ListEnsureVisible(hwnd, s, idx); return 0;
	case LB_GETHORIZONTALEXTENT: return 0;
	case LB_SETHORIZONTALEXTENT: return 0;
	case LB_GETITEMRECT: {
		auto r = reinterpret_cast<RECT*>(lParam);
		GetClientRect(hwnd, r);
		r->top = (idx - s->top) * s->itemHeight;
		r->bottom = r->top + s->itemHeight;
		return 1;
	}
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
		SetFocus(hwnd);
		const int i = ListHit(hwnd, s, static_cast<short>(HIWORD(lParam)));
		if (i < 0) return 0;
		const DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
		const bool changed = !s->items[i].selected || ListMulti(hwnd) || s->cur != i;
		if (ListMulti(hwnd)) {
			if ((style & LBS_EXTENDEDSEL) && (wParam & MK_SHIFT) && s->anchor >= 0) {
				for (int k = 0; k < count; ++k) s->items[k].selected = k >= std::min(s->anchor, i) && k <= std::max(s->anchor, i);
			} else if ((style & LBS_MULTIPLESEL) || (wParam & MK_CONTROL)) {
				s->items[i].selected = !s->items[i].selected;
				s->anchor = i;
			} else {
				for (auto& it : s->items) it.selected = false;
				s->items[i].selected = true;
				s->anchor = i;
			}
			s->cur = i;
			Invalidate(hwnd);
		} else {
			ListSelect(hwnd, s, i);
			SetCapture(hwnd);
			s->dragging = true;
		}
		if (style & LBS_NOTIFY) {
			if (changed) Notify(hwnd, LBN_SELCHANGE);
			if (msg == WM_LBUTTONDBLCLK) Notify(hwnd, LBN_DBLCLK);
		}
		return 0;
	}
	case WM_MOUSEMOVE:
		if (s->dragging) {
			const int i = ListHit(hwnd, s, static_cast<short>(HIWORD(lParam)));
			if (i >= 0 && i != s->cur) {
				ListSelect(hwnd, s, i);
				if (GetWindowLongW(hwnd, GWL_STYLE) & LBS_NOTIFY) Notify(hwnd, LBN_SELCHANGE);
			}
		}
		return 0;
	case WM_LBUTTONUP:
		if (s->dragging) { s->dragging = false; ReleaseCapture(); }
		return 0;
	case WM_KEYDOWN: {
		int i = s->cur;
		const int page = ListVisible(hwnd, s);
		switch (wParam) {
		case VK_UP: --i; break;
		case VK_DOWN: ++i; break;
		case VK_PRIOR: i -= page; break;
		case VK_NEXT: i += page; break;
		case VK_HOME: i = 0; break;
		case VK_END: i = count - 1; break;
		default: return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
		if (!count) return 0;
		i = std::clamp(i, 0, count - 1);
		if (ListMulti(hwnd)) for (auto& it : s->items) it.selected = false;
		ListSelect(hwnd, s, i);
		if (GetWindowLongW(hwnd, GWL_STYLE) & LBS_NOTIFY) Notify(hwnd, LBN_SELCHANGE);
		return 0;
	}
	case WM_MOUSEWHEEL:
		s->top += static_cast<short>(HIWORD(wParam)) > 0 ? -3 : 3;
		UpdateListScroll(hwnd, s);
		return 0;
	case WM_VSCROLL:
		switch (LOWORD(wParam)) {
		case SB_LINEUP: --s->top; break;
		case SB_LINEDOWN: ++s->top; break;
		case SB_PAGEUP: s->top -= ListVisible(hwnd, s); break;
		case SB_PAGEDOWN: s->top += ListVisible(hwnd, s); break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: s->top = static_cast<short>(HIWORD(wParam)); break;
		default: break;
		}
		UpdateListScroll(hwnd, s);
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		PaintList(hwnd, s, hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- COMBOBOX ----
constexpr int kComboHeight = 22;
constexpr int kComboArrow = 18;

struct ComboItem {
	std::wstring text;
	LPARAM data = 0;
};

struct Combo {
	std::vector<ComboItem> items;
	int sel = -1;
	HWND edit = nullptr;   //!< CBS_DROPDOWN のみ
	HWND list = nullptr;   //!< GetComboBoxInfo 用の非表示ウィンドウ。一覧の表示には使わない
	HFONT font = nullptr;
	bool focused = false;
	bool dropped = false;
	bool settingText = false;
};

bool ComboHasEdit(HWND hwnd) { return (GetWindowLongW(hwnd, GWL_STYLE) & 3) != CBS_DROPDOWNLIST; }

void ComboShowSelection(HWND hwnd, Combo* s)
{
	if (s->edit) {
		s->settingText = true;
		SetWindowTextW(s->edit, s->sel >= 0 ? s->items[s->sel].text.c_str() : L"");
		s->settingText = false;
		if (GetFocus() == s->edit) SendMessageW(s->edit, EM_SETSEL, 0, -1);
	}
	Invalidate(hwnd);
}

void ComboChoose(HWND hwnd, Combo* s, int i)
{
	s->sel = i;
	ComboShowSelection(hwnd, s);
	Notify(hwnd, CBN_SELENDOK);
	Notify(hwnd, CBN_SELCHANGE);
}

void ComboDrop(HWND hwnd, Combo* s)
{
	if (s->dropped) return;
	s->dropped = true;
	Notify(hwnd, CBN_DROPDOWN);
	if (s->items.empty()) { s->dropped = false; Notify(hwnd, CBN_CLOSEUP); return; }
	HMENU menu = CreatePopupMenu();
	for (size_t i = 0; i < s->items.size(); ++i) {
		std::wstring label;
		for (wchar_t c : s->items[i].text) {
			if (c == L'&') label += L'&';
			label += c == L'\t' ? L' ' : c;
		}
		AppendMenuW(menu, MF_STRING | (static_cast<int>(i) == s->sel ? MF_CHECKED : 0), i + 1, label.c_str());
	}
	RECT rc;
	GetWindowRect(hwnd, &rc);
	const int id = static_cast<int>(TrackPopupMenu(menu, TPM_RETURNCMD, rc.left, rc.bottom, 0, hwnd, nullptr));
	DestroyMenu(menu);
	s->dropped = false;
	if (id > 0) ComboChoose(hwnd, s, id - 1);
	else Notify(hwnd, CBN_SELENDCANCEL);
	Notify(hwnd, CBN_CLOSEUP);
}

void ComboLayout(HWND hwnd, Combo* s)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	if (rc.bottom > kComboHeight + 2) {
		// テンプレートの高さは一覧を含むので、Win32 と同じく閉じた状態の高さにする
		SetWindowPos(hwnd, nullptr, 0, 0, rc.right, kComboHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		return;
	}
	if (s->edit) MoveWindow(s->edit, 2, 2, rc.right - kComboArrow - 3, rc.bottom - 4, TRUE);
}

void PaintCombo(HWND hwnd, Combo* s, HDC hdc)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const bool enabled = IsWindowEnabled(hwnd);
	SelectObject(hdc, s->font ? s->font : DialogFont());
	Fill(hdc, rc, GetSysColor(COLOR_BTNFACE));
	RECT b = rc;
	InflateRect(&b, 0, 0);
	Rounded(hdc, b, 6, RGB(255, 255, 255), s->focused ? kAccent : kBorder);
	const DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
	if (!s->edit && s->sel >= 0) {
		RECT t{rc.left + 6, rc.top, rc.right - kComboArrow, rc.bottom};
		if (style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) {
			DRAWITEMSTRUCT di{ODT_COMBOBOX, static_cast<UINT>(GetDlgCtrlID(hwnd)), static_cast<UINT>(s->sel), ODA_DRAWENTIRE, 0, hwnd, hdc, t, static_cast<ULONG_PTR>(s->items[s->sel].data)};
			SendMessageW(GetParent(hwnd), WM_DRAWITEM, di.CtlID, reinterpret_cast<LPARAM>(&di));
		} else {
			Label(hdc, s->items[s->sel].text, t, enabled ? RGB(0, 0, 0) : kDisabledText);
		}
	}
	const int cx = rc.right - kComboArrow / 2 - 1, cy = (rc.top + rc.bottom) / 2;
	const COLORREF arrow = enabled ? RGB(90, 90, 90) : kDisabledText;
	Line(hdc, cx - 4, cy - 2, cx, cy + 2, arrow, 2);
	Line(hdc, cx, cy + 2, cx + 4, cy - 2, arrow, 2);
}

int ComboFind(const Combo* s, int after, const wchar_t* text, bool exact)
{
	const int n = static_cast<int>(s->items.size());
	const size_t len = wcslen(text);
	for (int k = 1; k <= n; ++k) {
		const int i = (after + k + n) % n;
		const auto& t = s->items[i].text;
		if (exact ? !_wcsicmp(t.c_str(), text) : !_wcsnicmp(t.c_str(), text, len)) return i;
	}
	return CB_ERR;
}

LRESULT CALLBACK ComboProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<Combo>(hwnd);
	const int count = s ? static_cast<int>(s->items.size()) : 0;
	const int idx = static_cast<int>(wParam);
	switch (msg) {
	case WM_NCCREATE: {
		bool border;
		RemoveBorderStyles(hwnd, &border);
		Attach<Combo>(hwnd);
		break;
	}
	case WM_CREATE:
		if (ComboHasEdit(hwnd)) {
			s->edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NOHIDESEL, 2, 2, 10, 10, hwnd,
			                          reinterpret_cast<HMENU>(1001), nullptr, nullptr);
		}
		s->list = CreateWindowExW(0, L"COMBOLBOX", L"", WS_CHILD, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1000), nullptr, nullptr);
		ComboLayout(hwnd, s);
		break;
	case WM_NCDESTROY: Detach<Combo>(hwnd); break;
	case WM_SIZE: ComboLayout(hwnd, s); break;
	case WM_SETFONT:
		s->font = reinterpret_cast<HFONT>(wParam);
		if (s->edit) SendMessageW(s->edit, msg, wParam, lParam);
		return 0;
	case WM_GETFONT: return reinterpret_cast<LRESULT>(s->font);
	case WM_ERASEBKGND: return 1;
	case WM_ENABLE:
		if (s->edit) EnableWindow(s->edit, wParam != 0);
		Invalidate(hwnd);
		return 0;
	case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
	case WM_SETFOCUS:
		if (s->edit) {
			SetFocus(s->edit);
			SendMessageW(s->edit, EM_SETSEL, 0, -1);  // Win32 と同じく、コンボボックスに入るときは全選択
			return 0;
		}
		s->focused = true;
		Invalidate(hwnd);
		Notify(hwnd, CBN_SETFOCUS);
		return 0;
	case WM_KILLFOCUS:
		s->focused = false;
		Invalidate(hwnd);
		if (!s->edit) Notify(hwnd, CBN_KILLFOCUS);
		return 0;
	case WM_COMMAND:
		if (reinterpret_cast<HWND>(lParam) == s->edit && s->edit) {
			switch (HIWORD(wParam)) {
			case EN_SETFOCUS: s->focused = true; Invalidate(hwnd); Notify(hwnd, CBN_SETFOCUS); break;
			case EN_KILLFOCUS: s->focused = false; Invalidate(hwnd); Notify(hwnd, CBN_KILLFOCUS); break;
			case EN_UPDATE: if (!s->settingText) Notify(hwnd, CBN_EDITUPDATE); break;
			case EN_CHANGE: if (!s->settingText) { s->sel = -1; Notify(hwnd, CBN_EDITCHANGE); } break;
			default: break;
			}
			return 0;
		}
		break;
	case WM_SETTEXT: case WM_GETTEXT: case WM_GETTEXTLENGTH:
		if (s->edit) return SendMessageW(s->edit, msg, wParam, lParam);
		if (msg == WM_GETTEXTLENGTH) return s->sel >= 0 ? static_cast<LRESULT>(s->items[s->sel].text.size()) : 0;
		if (msg == WM_GETTEXT) {
			if (!wParam) return 0;
			const std::wstring t = s->sel >= 0 ? s->items[s->sel].text : L"";
			const size_t n = std::min<size_t>(wParam - 1, t.size());
			wmemcpy(reinterpret_cast<LPWSTR>(lParam), t.c_str(), n);
			reinterpret_cast<LPWSTR>(lParam)[n] = 0;
			return static_cast<LRESULT>(n);
		}
		return TRUE;
	case CB_GETCOUNT: return count;
	case CB_ADDSTRING: case CB_INSERTSTRING: {
		int at = msg == CB_ADDSTRING || idx < 0 || idx > count ? count : idx;
		const bool ownerNoStr = (GetWindowLongW(hwnd, GWL_STYLE) & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) && !(GetWindowLongW(hwnd, GWL_STYLE) & CBS_HASSTRINGS);
		ComboItem it;
		if (ownerNoStr) it.data = lParam;
		else it.text = reinterpret_cast<LPCWSTR>(lParam);
		if (msg == CB_ADDSTRING && (GetWindowLongW(hwnd, GWL_STYLE) & CBS_SORT)) {
			at = static_cast<int>(std::find_if(s->items.begin(), s->items.end(), [&](const ComboItem& i) { return _wcsicmp(i.text.c_str(), it.text.c_str()) > 0; }) - s->items.begin());
		}
		s->items.insert(s->items.begin() + at, std::move(it));
		if (s->sel >= at) ++s->sel;
		return at;
	}
	case CB_DELETESTRING:
		if (idx < 0 || idx >= count) return CB_ERR;
		s->items.erase(s->items.begin() + idx);
		if (s->sel == idx) s->sel = -1;
		else if (s->sel > idx) --s->sel;
		Invalidate(hwnd);
		return count - 1;
	case CB_RESETCONTENT:
		s->items.clear();
		s->sel = -1;
		if (s->edit) { s->settingText = true; SetWindowTextW(s->edit, L""); s->settingText = false; }
		Invalidate(hwnd);
		return CB_OKAY;
	case CB_GETCURSEL: return s->sel;
	case CB_SETCURSEL:
		s->sel = idx >= 0 && idx < count ? idx : -1;
		ComboShowSelection(hwnd, s);
		return s->sel >= 0 ? s->sel : CB_ERR;
	case CB_GETLBTEXT:
		if (idx < 0 || idx >= count) return CB_ERR;
		wcscpy(reinterpret_cast<LPWSTR>(lParam), s->items[idx].text.c_str());
		return static_cast<LRESULT>(s->items[idx].text.size());
	case CB_GETLBTEXTLEN: return idx >= 0 && idx < count ? static_cast<LRESULT>(s->items[idx].text.size()) : CB_ERR;
	case CB_GETITEMDATA: return idx >= 0 && idx < count ? s->items[idx].data : CB_ERR;
	case CB_SETITEMDATA:
		if (idx < 0 || idx >= count) return CB_ERR;
		s->items[idx].data = lParam;
		return CB_OKAY;
	case CB_FINDSTRING: case CB_FINDSTRINGEXACT:
		return count ? ComboFind(s, idx, reinterpret_cast<LPCWSTR>(lParam), msg == CB_FINDSTRINGEXACT) : CB_ERR;
	case CB_SELECTSTRING: {
		const int i = count ? ComboFind(s, idx, reinterpret_cast<LPCWSTR>(lParam), false) : CB_ERR;
		if (i != CB_ERR) { s->sel = i; ComboShowSelection(hwnd, s); }
		return i;
	}
	case CB_LIMITTEXT:
		if (s->edit) SendMessageW(s->edit, EM_SETLIMITTEXT, wParam, 0);
		return TRUE;
	case CB_GETEDITSEL:
		return s->edit ? SendMessageW(s->edit, EM_GETSEL, wParam, lParam) : CB_ERR;
	case CB_SETEDITSEL:
		if (!s->edit) return CB_ERR;
		SendMessageW(s->edit, EM_SETSEL, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
		return TRUE;
	case CB_SHOWDROPDOWN:
		if (wParam) ComboDrop(hwnd, s);
		return TRUE;
	case CB_GETDROPPEDSTATE: return s->dropped;
	case CB_GETDROPPEDCONTROLRECT: GetWindowRect(hwnd, reinterpret_cast<RECT*>(lParam)); return CB_OKAY;
	case CB_SETDROPPEDWIDTH: case CB_GETDROPPEDWIDTH: case CB_SETEXTENDEDUI: case CB_SETITEMHEIGHT: case CB_GETITEMHEIGHT:
	case CB_SETHORIZONTALEXTENT: case CB_GETEXTENDEDUI: case CB_SETMINVISIBLE:
		return msg == CB_GETITEMHEIGHT ? kComboHeight - 4 : CB_OKAY;
	case CB_GETCOMBOBOXINFO: {
		auto info = reinterpret_cast<COMBOBOXINFO*>(lParam);
		GetClientRect(hwnd, &info->rcItem);
		info->rcItem.right -= kComboArrow;
		GetClientRect(hwnd, &info->rcButton);
		info->rcButton.left = info->rcButton.right - kComboArrow;
		info->stateButton = 0;
		info->hwndCombo = hwnd;
		info->hwndItem = s->edit;
		info->hwndList = s->list;
		return TRUE;
	}
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
		SetFocus(hwnd);
		ComboDrop(hwnd, s);
		return 0;
	case WM_KEYDOWN:
		if (wParam == VK_F4) { ComboDrop(hwnd, s); return 0; }
		if ((wParam == VK_UP || wParam == VK_DOWN) && count) {
			ComboChoose(hwnd, s, std::clamp(s->sel + (wParam == VK_UP ? -1 : 1), 0, count - 1));
			return 0;
		}
		break;
	case WM_SYSKEYDOWN:
		if (wParam == VK_DOWN) { ComboDrop(hwnd, s); return 0; }
		break;
	case WM_CHAR:
		if (!s->edit && count && wParam >= 0x20) {
			const wchar_t key[2] = {static_cast<wchar_t>(wParam), 0};
			const int i = ComboFind(s, s->sel, key, false);
			if (i != CB_ERR) ComboChoose(hwnd, s, i);
			return 0;
		}
		break;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		PaintCombo(hwnd, s, hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- UpDown ----
struct UpDown {
	HWND buddy = nullptr;
	int low = 100, high = 0;  //!< Win32 の既定は 100〜0（上で減る）
	int pos = 0;
	int pressed = 0;          //!< +1 上、-1 下
};

int ReadBuddy(HWND hwnd, UpDown* s)
{
	if (s->buddy && (GetWindowLongW(hwnd, GWL_STYLE) & UDS_SETBUDDYINT)) {
		wchar_t buf[32] = {};
		if (GetWindowTextW(s->buddy, buf, 32) > 0) s->pos = static_cast<int>(wcstol(buf, nullptr, 10));
	}
	return s->pos;
}

void WriteBuddy(HWND hwnd, UpDown* s)
{
	if (!s->buddy || !(GetWindowLongW(hwnd, GWL_STYLE) & UDS_SETBUDDYINT)) return;
	const std::string a = std::to_string(s->pos);
	SetWindowTextW(s->buddy, std::wstring(a.begin(), a.end()).c_str());
}

void SetBuddy(HWND hwnd, UpDown* s, HWND buddy)
{
	s->buddy = buddy;
	if (!buddy || !(GetWindowLongW(hwnd, GWL_STYLE) & (UDS_ALIGNRIGHT | UDS_ALIGNLEFT))) return;
	RECT b;
	GetWindowRect(buddy, &b);
	MapWindowPoints(nullptr, GetParent(hwnd), reinterpret_cast<POINT*>(&b), 2);
	constexpr int width = 15;
	MoveWindow(buddy, b.left, b.top, b.right - b.left - width, b.bottom - b.top, TRUE);
	MoveWindow(hwnd, b.right - width, b.top, width, b.bottom - b.top, TRUE);
}

void UpDownStep(HWND hwnd, UpDown* s, int dir)
{
	ReadBuddy(hwnd, s);
	const int a = std::min(s->low, s->high), b = std::max(s->low, s->high);
	const int delta = s->low > s->high ? -dir : dir;
	NMUPDOWN nm{};
	nm.hdr = {hwnd, static_cast<UINT_PTR>(GetDlgCtrlID(hwnd)), static_cast<UINT>(UDN_DELTAPOS)};
	nm.iPos = s->pos;
	nm.iDelta = delta;
	if (SendMessageW(GetParent(hwnd), WM_NOTIFY, nm.hdr.idFrom, reinterpret_cast<LPARAM>(&nm))) return;
	int next = s->pos + nm.iDelta;
	if (GetWindowLongW(hwnd, GWL_STYLE) & UDS_WRAP) { if (next > b) next = a; else if (next < a) next = b; }
	s->pos = std::clamp(next, a, b);
	WriteBuddy(hwnd, s);
	SendMessageW(GetParent(hwnd), (GetWindowLongW(hwnd, GWL_STYLE) & UDS_HORZ) ? WM_HSCROLL : WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, s->pos), reinterpret_cast<LPARAM>(hwnd));
}

LRESULT CALLBACK UpDownProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto s = StateOf<UpDown>(hwnd);
	switch (msg) {
	case WM_NCCREATE: Attach<UpDown>(hwnd); break;
	case WM_CREATE:
		if (GetWindowLongW(hwnd, GWL_STYLE) & UDS_AUTOBUDDY) {
			// 直前に作られた兄弟（子のリストは新しい順）
			std::lock_guard lock(UserMutex());
			const auto& ch = GetWindowObject(GetParent(hwnd))->children;
			const auto it = std::find(ch.begin(), ch.end(), hwnd);
			if (it != ch.end() && it + 1 != ch.end()) SetBuddy(hwnd, s, *(it + 1));
		}
		break;
	case WM_NCDESTROY: Detach<UpDown>(hwnd); break;
	case WM_ERASEBKGND: return 1;
	case WM_GETDLGCODE: return DLGC_STATIC;
	case UDM_SETBUDDY: { const HWND old = s->buddy; SetBuddy(hwnd, s, reinterpret_cast<HWND>(wParam)); return reinterpret_cast<LRESULT>(old); }
	case UDM_GETBUDDY: return reinterpret_cast<LRESULT>(s->buddy);
	case UDM_SETRANGE: s->low = static_cast<short>(HIWORD(lParam)); s->high = static_cast<short>(LOWORD(lParam)); return 0;
	case UDM_SETRANGE32: s->low = static_cast<int>(wParam); s->high = static_cast<int>(lParam); return 0;
	case UDM_GETRANGE: return MAKELONG(s->high, s->low);
	case UDM_GETRANGE32:
		if (wParam) *reinterpret_cast<int*>(wParam) = s->low;
		if (lParam) *reinterpret_cast<int*>(lParam) = s->high;
		return 0;
	case UDM_SETPOS: { const int old = s->pos; s->pos = static_cast<short>(LOWORD(lParam)); WriteBuddy(hwnd, s); return old; }
	case UDM_SETPOS32: { const int old = s->pos; s->pos = static_cast<int>(lParam); WriteBuddy(hwnd, s); return old; }
	case UDM_GETPOS: return ReadBuddy(hwnd, s) & 0xFFFF;
	case UDM_GETPOS32: return ReadBuddy(hwnd, s);
	case UDM_SETACCEL: case UDM_SETBASE: return 0;
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
		RECT rc;
		GetClientRect(hwnd, &rc);
		s->pressed = static_cast<short>(HIWORD(lParam)) < rc.bottom / 2 ? 1 : -1;
		SetCapture(hwnd);
		UpDownStep(hwnd, s, s->pressed);
		SetTimer(hwnd, 1, 400, nullptr);
		Invalidate(hwnd);
		return 0;
	}
	case WM_TIMER:
		if (s->pressed) {
			SetTimer(hwnd, 1, 80, nullptr);
			UpDownStep(hwnd, s, s->pressed);
		}
		return 0;
	case WM_LBUTTONUP:
		if (s->pressed) {
			s->pressed = 0;
			KillTimer(hwnd, 1);
			ReleaseCapture();
			Invalidate(hwnd);
		}
		return 0;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		Fill(hdc, rc, GetSysColor(COLOR_BTNFACE));
		Rounded(hdc, rc, 4, RGB(255, 255, 255), kBorder);
		const int mid = (rc.top + rc.bottom) / 2, cx = rc.right / 2;
		if (s->pressed) {
			RECT half{rc.left + 1, s->pressed > 0 ? rc.top + 1 : mid, rc.right - 1, s->pressed > 0 ? mid : rc.bottom - 1};
			Fill(hdc, half, RGB(215, 215, 215));
		}
		Line(hdc, rc.left, mid, rc.right, mid, kBorder);
		const COLORREF arrow = IsWindowEnabled(hwnd) ? RGB(70, 70, 70) : kDisabledText;
		Line(hdc, cx - 3, mid - 3, cx, mid - 6, arrow);
		Line(hdc, cx, mid - 6, cx + 3, mid - 3, arrow);
		Line(hdc, cx - 3, mid + 3, cx, mid + 6, arrow);
		Line(hdc, cx, mid + 6, cx + 3, mid + 3, arrow);
		EndPaint(hwnd, &ps);
		return 0;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

namespace winport {

WNDPROC DialogControlProc(const wchar_t* name)
{
	if (!_wcsicmp(name, L"#32770")) return DefDlgProcW;
	if (!_wcsicmp(name, L"BUTTON")) return ButtonProc;
	if (!_wcsicmp(name, L"STATIC")) return StaticProc;
	if (!_wcsicmp(name, L"EDIT")) return EditProc;
	if (!_wcsicmp(name, L"LISTBOX")) return ListProc;
	if (!_wcsicmp(name, L"COMBOBOX")) return ComboProc;
	if (!_wcsicmp(name, L"msctls_updown32")) return UpDownProc;
	return nullptr;
}

}  // namespace winport

extern "C" {

WINBOOL WINAPI GetComboBoxInfo(HWND hwndCombo, PCOMBOBOXINFO pcbi)
{
	return SendMessageW(hwndCombo, CB_GETCOMBOBOXINFO, 0, reinterpret_cast<LPARAM>(pcbi)) != 0;
}

}  // extern "C"
