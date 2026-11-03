/*
	test-winport-gdi.cpp — winport の GDI 実装のテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

namespace {

//! メモリ DC の矩形内に背景色以外の画素がいくつあるか
int CountInk(HDC hdc, int w, int h, COLORREF bg)
{
	int n = 0;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			if (::GetPixel(hdc, x, y) != bg) ++n;
		}
	}
	return n;
}

struct MemDC {
	HDC dc;
	HBITMAP bmp;
	HGDIOBJ oldBmp;
	MemDC(int w, int h)
	{
		HDC screen = ::GetDC(nullptr);
		dc = ::CreateCompatibleDC(screen);
		bmp = ::CreateCompatibleBitmap(screen, w, h);
		::ReleaseDC(nullptr, screen);
		oldBmp = ::SelectObject(dc, bmp);
		RECT rc{0, 0, w, h};
		::FillRect(dc, &rc, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
	}
	~MemDC()
	{
		::SelectObject(dc, oldBmp);
		::DeleteObject(bmp);
		::DeleteDC(dc);
	}
};

}  // namespace

TEST(WinportGdi, ExtTextOutDrawsGlyphs)
{
	MemDC m(120, 30);
	LOGFONTW lf{};
	lf.lfHeight = -16;
	wcscpy_s(lf.lfFaceName, L"ＭＳ ゴシック");
	HFONT font = ::CreateFontIndirectW(&lf);
	HGDIOBJ old = ::SelectObject(m.dc, font);
	::SetTextColor(m.dc, RGB(0, 0, 0));
	::SetBkMode(m.dc, TRANSPARENT);
	ASSERT_TRUE(::ExtTextOutW(m.dc, 2, 2, 0, nullptr, L"Hiあ", 3, nullptr));
	EXPECT_GT(CountInk(m.dc, 120, 30, RGB(255, 255, 255)), 20);
	::SelectObject(m.dc, old);
	::DeleteObject(font);
}

TEST(WinportGdi, ExtTextOutWithDxDrawsGlyphs)
{
	MemDC m(120, 30);
	const int dx[] = {10, 10, 20};
	::SetBkMode(m.dc, TRANSPARENT);
	ASSERT_TRUE(::ExtTextOutW(m.dc, 2, 2, ETO_CLIPPED, nullptr, L"ab漢", 3, dx));
	EXPECT_GT(CountInk(m.dc, 120, 30, RGB(255, 255, 255)), 20);
}

TEST(WinportGdi, TextExtentMatchesFixedPitch)
{
	MemDC m(10, 10);
	LOGFONTW lf{};
	lf.lfHeight = -16;
	wcscpy_s(lf.lfFaceName, L"ＭＳ ゴシック");
	HFONT font = ::CreateFontIndirectW(&lf);
	HGDIOBJ old = ::SelectObject(m.dc, font);
	SIZE one{}, ten{};
	::GetTextExtentPoint32W(m.dc, L"a", 1, &one);
	::GetTextExtentPoint32W(m.dc, L"aaaaaaaaaa", 10, &ten);
	EXPECT_GT(one.cx, 0);
	EXPECT_EQ(one.cx * 10, ten.cx);
	::SelectObject(m.dc, old);
	::DeleteObject(font);
}
