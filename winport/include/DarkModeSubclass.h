/*
	winport/include/DarkModeSubclass.h — darkmodelib（Windows 専用）の代替

	sakura_core が呼ぶ DarkMode:: API の宣言だけを提供する。
	mac では NSAppearance に合わせた実装を winport/src に置く予定。
	当面は「ダークモード無効」として振る舞う。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <windows.h>

namespace DarkMode {

enum class DarkModeType : UINT { light = 0, dark = 1, classic = 3 };
enum class ToolTipsType : int { tooltip = 0, toolbar, listview, treeview, tabbar, trackbar, rebar };

void initDarkMode();
void setDarkModeConfig();
void setDarkModeConfigEx(UINT dmType);
void setDefaultColors(bool updateBrushesAndOther);
void setColorizeTitleBarConfig(bool colorize);
bool isEnabled();
bool isExperimentalActive();

COLORREF getTextColor();
COLORREF getBackgroundColor();
COLORREF getCtrlBackgroundColor();
COLORREF getHotBackgroundColor();
COLORREF getDlgBackgroundColor();
COLORREF getViewBackgroundColor();
COLORREF getEdgeColor();
COLORREF getHotEdgeColor();

void setDarkWndSafe(HWND hWnd, bool useWin11Features = true);
void setDarkWndNotifySafeEx(HWND hWnd, bool setSettingChangeSubclass = false, bool useWin11Features = true);
void setDarkTitleBarEx(HWND hWnd, bool useWin11Features = true);
void setDarkTooltips(HWND hWnd, int tooltipType = 0);
void setWindowExStyle(HWND hWnd, bool setStyle, LONG_PTR exStyle);
void setChildCtrlsTheme(HWND hParent);
void setChildCtrlsSubclassAndTheme(HWND hParent);
void setWindowMenuBarSubclass(HWND hWnd);
void setWindowEraseBgSubclass(HWND hWnd);
void setWindowCtlColorSubclass(HWND hWnd);
void setStatusBarCtrlSubclass(HWND hWnd);
void setProgressBarCtrlSubclass(HWND hWnd);

UINT_PTR CALLBACK HookDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

}  // namespace DarkMode
