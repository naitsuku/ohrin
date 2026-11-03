/*
	darkmode.cpp — darkmode.lib 代替（winport/include/DarkModeSubclass.h の実装）

	当面は「ダークモード無効」として振る舞う。色の取得はシステムカラー相当を返す。
	TODO(winport): NSApp.effectiveAppearance を見てダークモードに追従する。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <DarkModeSubclass.h>

namespace DarkMode {

void initDarkMode() {}
void setDarkModeConfig() {}
void setDarkModeConfigEx(UINT) {}
void setDefaultColors(bool) {}
void setColorizeTitleBarConfig(bool) {}
bool isEnabled() { return false; }
bool isExperimentalActive() { return false; }

COLORREF getTextColor() { return RGB(0, 0, 0); }
COLORREF getBackgroundColor() { return RGB(255, 255, 255); }
COLORREF getCtrlBackgroundColor() { return RGB(255, 255, 255); }
COLORREF getHotBackgroundColor() { return RGB(229, 243, 255); }
COLORREF getDlgBackgroundColor() { return RGB(240, 240, 240); }
COLORREF getViewBackgroundColor() { return RGB(255, 255, 255); }
COLORREF getEdgeColor() { return RGB(160, 160, 160); }
COLORREF getHotEdgeColor() { return RGB(0, 120, 215); }

void setDarkWndSafe(HWND, bool) {}
void setDarkWndNotifySafeEx(HWND, bool, bool) {}
void setDarkTitleBarEx(HWND, bool) {}
void setDarkTooltips(HWND, int) {}
void setWindowExStyle(HWND, bool, LONG_PTR) {}
void setChildCtrlsTheme(HWND) {}
void setChildCtrlsSubclassAndTheme(HWND) {}
void setWindowMenuBarSubclass(HWND) {}
void setWindowEraseBgSubclass(HWND) {}
void setWindowCtlColorSubclass(HWND) {}
void setStatusBarCtrlSubclass(HWND) {}
void setProgressBarCtrlSubclass(HWND) {}

UINT_PTR CALLBACK HookDlgProc(HWND, UINT, WPARAM, LPARAM) { return 0; }

}  // namespace DarkMode
