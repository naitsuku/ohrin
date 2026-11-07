/*
	control_common.h — ListView / TreeView などのコモンコントロールで共有する部品

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <string>

namespace winport {

constexpr COLORREF kControlAccent = RGB(0, 110, 230);
constexpr COLORREF kControlBorder = RGB(190, 190, 190);
constexpr COLORREF kControlInactive = RGB(210, 210, 210);
constexpr COLORREF kControlDisabledText = RGB(150, 150, 150);

//! ラベル編集が終わったことを持ち主へ知らせる。wParam は確定なら TRUE
constexpr UINT kEndLabelEdit = WM_USER + 0x7E00;

inline void FillColor(HDC hdc, const RECT& r, COLORREF c)
{
	HBRUSH b = CreateSolidBrush(c);
	FillRect(hdc, &r, b);
	DeleteObject(b);
}

inline void FrameColor(HDC hdc, const RECT& r, COLORREF c)
{
	HBRUSH b = CreateSolidBrush(c);
	FrameRect(hdc, &r, b);
	DeleteObject(b);
}

inline void LineColor(HDC hdc, int x1, int y1, int x2, int y2, COLORREF c, int width = 1)
{
	HPEN p = CreatePen(PS_SOLID, width, c);
	HGDIOBJ old = SelectObject(hdc, p);
	MoveToEx(hdc, x1, y1, nullptr);
	LineTo(hdc, x2, y2);
	SelectObject(hdc, old);
	DeleteObject(p);
}

inline int FontLineHeight(HFONT font)
{
	HDC hdc = GetDC(nullptr);
	HGDIOBJ old = SelectObject(hdc, font);
	TEXTMETRICW tm{};
	GetTextMetricsW(hdc, &tm);
	SelectObject(hdc, old);
	ReleaseDC(nullptr, hdc);
	return std::max<int>(tm.tmHeight, 12);
}

inline int TextWidth(HDC hdc, const std::wstring& text)
{
	SIZE size{};
	if (!text.empty()) GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
	return size.cx;
}

//! WM_NOTIFY を親へ送る。header の hwndFrom / idFrom / code はここで埋める
inline LRESULT NotifyParent(HWND hwnd, NMHDR* header, UINT code)
{
	header->hwndFrom = hwnd;
	header->idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(hwnd));
	header->code = code;
	return SendMessageW(GetParent(hwnd), WM_NOTIFY, header->idFrom, reinterpret_cast<LPARAM>(header));
}

inline void CopyText(const std::wstring& text, LPWSTR dest, int size)
{
	if (!dest || size <= 0) return;
	const size_t n = std::min(text.size(), static_cast<size_t>(size - 1));
	std::copy_n(text.data(), n, dest);
	dest[n] = L'\0';
}

//! ラベル編集用のエディットを作る。Enter / Esc / フォーカス喪失で owner へ kEndLabelEdit を送る
HWND CreateLabelEdit(HWND owner, const RECT& rc, const std::wstring& text, HFONT font);
//! 編集を閉じる。呼び出し元のエディットのメッセージ処理中でも安全なように、破棄は遅らせる
void CloseLabelEdit(HWND owner, HWND edit);

}  // namespace winport
