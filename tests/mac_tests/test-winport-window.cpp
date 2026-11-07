/*
	test-winport-window.cpp — winport のウィンドウ管理のテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

namespace {

int g_sizeCount = 0;
int g_moveCount = 0;

LRESULT CALLBACK CountingProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_SIZE) ++g_sizeCount;
	if (msg == WM_MOVE) ++g_moveCount;
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

// CTabWnd::LayoutTab は WM_SIZE の処理の中で同じ大きさの SetWindowPos を呼ぶ。送ると無限再帰になる
TEST(WinportWindow, SetWindowPosSendsSizeOnlyWhenChanged)
{
	WNDCLASSW wc{};
	wc.lpfnWndProc = CountingProc;
	wc.lpszClassName = L"winport_test_setwindowpos";
	ASSERT_NE(0, ::RegisterClassW(&wc));
	HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 10, 20, 100, 50, nullptr, nullptr, nullptr, nullptr);
	ASSERT_NE(nullptr, hwnd);

	g_sizeCount = g_moveCount = 0;
	::SetWindowPos(hwnd, nullptr, 0, 0, 100, 50, SWP_NOMOVE | SWP_NOZORDER);
	EXPECT_EQ(0, g_sizeCount);
	::SetWindowPos(hwnd, nullptr, 10, 20, 100, 50, SWP_NOZORDER);
	EXPECT_EQ(0, g_sizeCount);
	EXPECT_EQ(0, g_moveCount);

	::SetWindowPos(hwnd, nullptr, 0, 0, 120, 50, SWP_NOMOVE | SWP_NOZORDER);
	EXPECT_EQ(1, g_sizeCount);
	EXPECT_EQ(0, g_moveCount);
	::SetWindowPos(hwnd, nullptr, 30, 20, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	EXPECT_EQ(1, g_sizeCount);
	EXPECT_EQ(1, g_moveCount);

	::DestroyWindow(hwnd);
}

// nTrackPos は設定できず、ドラッグ中以外は現在位置を返す。上流の印刷プレビューは SIF_ALL で nTrackPos=100 を渡し、
// 読み戻した値を現在位置として使うので、保存すると表示が 100 ピクセルずれる
TEST(WinportWindow, ScrollTrackPosFollowsPosition)
{
	HWND bar = ::CreateWindowExW(0, L"SCROLLBAR", L"", WS_POPUP, 0, 0, 20, 100, nullptr, nullptr, nullptr, nullptr);
	ASSERT_NE(nullptr, bar);
	SCROLLINFO set{sizeof(set), SIF_ALL, 0, 500, 50, 30, 100};
	::SetScrollInfo(bar, SB_CTL, &set, FALSE);
	SCROLLINFO get{sizeof(get), SIF_POS | SIF_TRACKPOS};
	::GetScrollInfo(bar, SB_CTL, &get);
	EXPECT_EQ(30, get.nPos);
	EXPECT_EQ(30, get.nTrackPos);
	::DestroyWindow(bar);
}
