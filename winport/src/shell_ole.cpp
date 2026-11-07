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

#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#include "user32_internal.h"

#include "winport_internal.h"

extern "C" {

HRESULT WINAPI OleInitialize(LPVOID) { return S_OK; }
void WINAPI OleUninitialize(void) {}
// TODO(winport): NSDraggingDestination と連携する（Week 6）
HRESULT WINAPI RegisterDragDrop(HWND, LPDROPTARGET) { return S_OK; }
HRESULT WINAPI RevokeDragDrop(HWND) { return S_OK; }
void WINAPI DragAcceptFiles(HWND hWnd, WINBOOL fAccept)
{
	if (auto w = winport::GetWindowObject(hWnd)) w->acceptFiles = fAccept != FALSE;
}

//! Windows と同じ DROPFILES（ヘッダーの後に \0 区切りのパス、空文字列で終わる）
UINT WINAPI DragQueryFileW(HDROP hDrop, UINT iFile, LPWSTR lpszFile, UINT cch)
{
	auto df = static_cast<const DROPFILES*>(GlobalLock(hDrop));
	auto p = reinterpret_cast<const wchar_t*>(reinterpret_cast<const char*>(df) + df->pFiles);
	UINT count = 0, result = 0;
	for (; *p; p += wcslen(p) + 1, ++count) {
		if (count != iFile) continue;
		result = static_cast<UINT>(wcslen(p));
		if (lpszFile && cch) {
			wcsncpy_s(lpszFile, cch, p, _TRUNCATE);
			result = static_cast<UINT>(wcslen(lpszFile));
		}
		break;
	}
	GlobalUnlock(hDrop);
	return iFile == 0xFFFFFFFF ? count : result;
}

WINBOOL WINAPI DragQueryPoint(HDROP hDrop, POINT* ppt)
{
	auto df = static_cast<const DROPFILES*>(GlobalLock(hDrop));
	*ppt = df->pt;
	const bool client = !df->fNC;
	GlobalUnlock(hDrop);
	return client;
}

void WINAPI DragFinish(HDROP hDrop) { GlobalFree(hDrop); }

WINBOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX*) { return TRUE; }
WINBOOL WINAPI Shell_NotifyIconW(DWORD, PNOTIFYICONDATAW) { return TRUE; }
void WINAPI SHAddToRecentDocs(UINT, LPCVOID) {}

WINBOOL WINAPI SetDllDirectoryW(LPCWSTR) { return TRUE; }
WINBOOL WINAPI SetSearchPathMode(DWORD) { return TRUE; }
WINBOOL WINAPI SetProcessShutdownParameters(DWORD, DWORD) { return TRUE; }

}  // extern "C"

namespace winport {

HDROP CreateDropHandle(const std::vector<std::wstring>& files, POINT pt)
{
	size_t chars = 1;
	for (const auto& f : files) chars += f.size() + 1;
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
	auto df = static_cast<DROPFILES*>(GlobalLock(h));
	df->pFiles = sizeof(DROPFILES);
	df->pt = pt;
	df->fWide = TRUE;
	auto p = reinterpret_cast<wchar_t*>(df + 1);
	for (const auto& f : files) {
		wmemcpy(p, f.c_str(), f.size() + 1);
		p += f.size() + 1;
	}
	GlobalUnlock(h);
	return static_cast<HDROP>(h);
}

}  // namespace winport

/* ---- ユーザーのフォルダー ---- */
void WINAPI CoTaskMemFree(LPVOID pv) { std::free(pv); }

//! 設定の保存先を決めるのに使う（CControlProcess::GetPrivateIniFileName）
HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD, HANDLE, PWSTR* ppszPath)
{
	*ppszPath = nullptr;
	const char* home = std::getenv("HOME");
	if (!home) return E_FAIL;
	std::string path = home;
	if (IsEqualGUID(rfid, FOLDERID_RoamingAppData)) path += "/Library/Application Support";
	else if (IsEqualGUID(rfid, FOLDERID_Documents)) path += "/Documents";
	else if (!IsEqualGUID(rfid, FOLDERID_Profile)) return E_INVALIDARG;
	const std::wstring wide = winport::Utf8ToWide(path);
	auto* buffer = static_cast<PWSTR>(std::malloc((wide.size() + 1) * sizeof(WCHAR)));
	if (!buffer) return E_OUTOFMEMORY;
	std::wmemcpy(buffer, wide.c_str(), wide.size() + 1);
	*ppszPath = buffer;
	return S_OK;
}
