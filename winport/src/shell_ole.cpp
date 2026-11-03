/*
	shell_ole.cpp — ole32 / shell32 / comctl32 などの「呼ばれても何もしなくてよい」関数

	OLE のドラッグ＆ドロップやタスクトレイは未実装。上流は失敗するとメッセージボックスを
	出す箇所があるので、機能が無いだけの関数は成功を返す。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

extern "C" {

HRESULT WINAPI OleInitialize(LPVOID) { return S_OK; }
void WINAPI OleUninitialize(void) {}
// TODO(winport): NSDraggingDestination と連携する（Week 6）
HRESULT WINAPI RegisterDragDrop(HWND, LPDROPTARGET) { return S_OK; }
HRESULT WINAPI RevokeDragDrop(HWND) { return S_OK; }
void WINAPI DragAcceptFiles(HWND, WINBOOL) {}

WINBOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX*) { return TRUE; }
WINBOOL WINAPI Shell_NotifyIconW(DWORD, PNOTIFYICONDATAW) { return TRUE; }
void WINAPI SHAddToRecentDocs(UINT, LPCVOID) {}

WINBOOL WINAPI SetDllDirectoryW(LPCWSTR) { return TRUE; }
WINBOOL WINAPI SetSearchPathMode(DWORD) { return TRUE; }
WINBOOL WINAPI SetProcessShutdownParameters(DWORD, DWORD) { return TRUE; }

}  // extern "C"
