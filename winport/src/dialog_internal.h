/*
	dialog_internal.h — dialog.cpp と dialog_controls.cpp の間の受け渡し

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <windows.h>

#include <vector>

namespace winport {

//! コントロールの既定フォント
HFONT DialogFont();
//! フォーカスを移す。エディットなら全選択する（Win32 のダイアログと同じ）
void DialogSetFocus(HWND hDlg, HWND h);
//! control と同じグループ（WS_GROUP で区切られた範囲）のコントロールを作成順に返す
std::vector<HWND> DialogGroupOf(HWND control);
//! BUTTON / STATIC / EDIT / LISTBOX / COMBOBOX / msctls_updown32 のプロシージャ。対象外は nullptr
WNDPROC DialogControlProc(const wchar_t* name);

}  // namespace winport
