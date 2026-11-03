/*
	kernel32.cpp — kernel32 の非 GUI 基本関数

	エラーコード、モジュール・カレントディレクトリ、UI 言語・ロケール、仮想メモリ、DLL 読み込み（未対応）。
	パスは UTF-16(wchar_t) ⇔ UTF-8 で変換して POSIX に渡す。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>

#include <mach-o/dyld.h>
#include <sys/mman.h>
#include <unistd.h>

#include <climits>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>

#include "winport_internal.h"

namespace {

thread_local DWORD t_lastError = ERROR_SUCCESS;
thread_local LANGID t_uiLanguage = MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);

//! std::wstring を Win32 流の (バッファ, 長さ) に書き出す。足りなければ必要サイズ（終端込み）を返す
DWORD CopyOut(const std::wstring& s, LPWSTR buf, DWORD n)
{
	if (!buf || n <= s.size()) return static_cast<DWORD>(s.size() + 1);
	std::wmemcpy(buf, s.c_str(), s.size() + 1);
	return static_cast<DWORD>(s.size());
}

std::mutex g_vmMutex;
std::map<void*, size_t> g_vmSizes;

}  // namespace

extern "C" {

/* ---- エラーコード ---- */
DWORD WINAPI GetLastError(void) { return t_lastError; }
void WINAPI SetLastError(DWORD dwErrCode) { t_lastError = dwErrCode; }

/* ---- モジュール ---- */
DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
	if (hModule) {
		// メイン実行ファイル以外のモジュールは持たない
		SetLastError(ERROR_MOD_NOT_FOUND);
		return 0;
	}
	char path[PATH_MAX];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size) != 0) return 0;
	char real[PATH_MAX];
	const std::wstring w = winport::Utf8ToWide(realpath(path, real) ? real : path);
	if (nSize == 0) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
	const DWORD n = w.size() < nSize ? static_cast<DWORD>(w.size()) : nSize - 1;
	std::wmemcpy(lpFilename, w.c_str(), n);
	lpFilename[n] = 0;
	if (n < w.size()) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return nSize; }
	return n;
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName)
{
	// メインモジュールは非 null の擬似ハンドルで表す（リソース検索では無視される）
	static char s_main;
	return lpModuleName ? nullptr : reinterpret_cast<HMODULE>(&s_main);
}

HMODULE WINAPI LoadLibraryW(LPCWSTR) { SetLastError(ERROR_MOD_NOT_FOUND); return nullptr; }
HMODULE WINAPI LoadLibraryExW(LPCWSTR, HANDLE, DWORD) { SetLastError(ERROR_MOD_NOT_FOUND); return nullptr; }
WINBOOL WINAPI FreeLibrary(HMODULE) { return TRUE; }
FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR) { SetLastError(ERROR_PROC_NOT_FOUND); return nullptr; }

/* ---- カレントディレクトリ ---- */
DWORD WINAPI GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
	char buf[PATH_MAX];
	if (!getcwd(buf, sizeof(buf))) return 0;
	return CopyOut(winport::Utf8ToWide(buf), lpBuffer, nBufferLength);
}

WINBOOL WINAPI SetCurrentDirectoryW(LPCWSTR lpPathName)
{
	if (chdir(winport::WideToUtf8(lpPathName ? lpPathName : L"").c_str()) == 0) return TRUE;
	SetLastError(ERROR_PATH_NOT_FOUND);
	return FALSE;
}

/* ---- UI 言語・ロケール（日本語版 Windows として振る舞う） ---- */
LANGID WINAPI GetThreadUILanguage(void) { return t_uiLanguage; }
LANGID WINAPI SetThreadUILanguage(LANGID LangId) { t_uiLanguage = LangId; return LangId; }
LANGID WINAPI GetUserDefaultUILanguage(void) { return MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN); }
LANGID WINAPI GetSystemDefaultUILanguage(void) { return MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN); }
LANGID WINAPI GetUserDefaultLangID(void) { return MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN); }
LANGID WINAPI GetSystemDefaultLangID(void) { return MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN); }
LCID WINAPI GetUserDefaultLCID(void) { return MAKELCID(GetUserDefaultLangID(), SORT_DEFAULT); }
LCID WINAPI GetSystemDefaultLCID(void) { return GetUserDefaultLCID(); }
LCID WINAPI GetThreadLocale(void) { return GetUserDefaultLCID(); }

int WINAPI LCIDToLocaleName(LCID Locale, LPWSTR lpName, int cchName, DWORD)
{
	const wchar_t* name = PRIMARYLANGID(LANGIDFROMLCID(Locale)) == LANG_JAPANESE ? L"ja-JP" : L"en-US";
	const int len = static_cast<int>(std::wcslen(name)) + 1;
	if (cchName == 0) return len;
	if (cchName < len) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
	std::wmemcpy(lpName, name, len);
	return len;
}

int WINAPI GetLocaleInfoEx(LPCWSTR, LCTYPE LCType, LPWSTR lpLCData, int cchData)
{
	const LCTYPE type = LCType & ~(LOCALE_RETURN_NUMBER | LOCALE_NOUSEROVERRIDE);
	if (LCType & LOCALE_RETURN_NUMBER) {
		DWORD value;
		switch (type) {
		case LOCALE_IDEFAULTANSICODEPAGE: value = GetACP(); break;
		case LOCALE_IDEFAULTCODEPAGE: value = GetOEMCP(); break;
		default:
			winport::ReportUnimplemented("kernel32", "GetLocaleInfoEx(number type)");
			return 0;
		}
		// Windows は「DWORD = 2 文字」として 2 を返す。上流は戻り値 2 を前提に比較しているので合わせる
		// （WCHAR が 4 バイトの winport では、2 文字分のバッファに DWORD は収まる）
		if (cchData == 0) return 2;
		std::memcpy(lpLCData, &value, sizeof(DWORD));
		return 2;
	}
	winport::ReportUnimplemented("kernel32", "GetLocaleInfoEx(string type)");
	return 0;
}

/* ---- 仮想メモリ ---- */
LPVOID WINAPI VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD)
{
	if (lpAddress || !(flAllocationType & (MEM_RESERVE | MEM_COMMIT))) {
		winport::ReportUnimplemented("kernel32", "VirtualAlloc(address / commit only)");
		return nullptr;
	}
	void* p = mmap(nullptr, dwSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (p == MAP_FAILED) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
	std::lock_guard lock(g_vmMutex);
	g_vmSizes[p] = dwSize;
	return p;
}

WINBOOL WINAPI VirtualFree(LPVOID lpAddress, SIZE_T, DWORD dwFreeType)
{
	if (!(dwFreeType & MEM_RELEASE)) return TRUE;  // MEM_DECOMMIT は何もしない
	std::lock_guard lock(g_vmMutex);
	const auto it = g_vmSizes.find(lpAddress);
	if (it == g_vmSizes.end()) { SetLastError(ERROR_INVALID_ADDRESS); return FALSE; }
	munmap(it->first, it->second);
	g_vmSizes.erase(it);
	return TRUE;
}

/* ---- 標準ハンドル（擬似値） ---- */
HANDLE WINAPI GetStdHandle(DWORD nStdHandle)
{
	switch (nStdHandle) {
	case STD_INPUT_HANDLE: return reinterpret_cast<HANDLE>(intptr_t{0x10});
	case STD_OUTPUT_HANDLE: return reinterpret_cast<HANDLE>(intptr_t{0x11});
	case STD_ERROR_HANDLE: return reinterpret_cast<HANDLE>(intptr_t{0x12});
	default: return INVALID_HANDLE_VALUE;
	}
}

/* ---- 時刻 ---- */
DWORD WINAPI GetTickCount(void)
{
	return static_cast<DWORD>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000000);
}

ULONGLONG WINAPI GetTickCount64(void)
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000000;
}

WINBOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)
{
	lpPerformanceCount->QuadPart = static_cast<LONGLONG>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
	return TRUE;
}

WINBOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency)
{
	lpFrequency->QuadPart = 1000000000LL;
	return TRUE;
}

DWORD WINAPI GetCurrentProcessId(void) { return static_cast<DWORD>(getpid()); }
DWORD WINAPI GetCurrentThreadId(void)
{
	uint64_t tid = 0;
	pthread_threadid_np(nullptr, &tid);
	return static_cast<DWORD>(tid);
}

/* ---- user32: メッセージボックス（GUI 実装までは stderr に出して IDOK） ---- */
int WINAPI MessageBoxExW(HWND, LPCWSTR lpText, LPCWSTR lpCaption, UINT, WORD)
{
	std::fprintf(stderr, "[MessageBox] %s: %s\n", winport::WideToUtf8(lpCaption ? lpCaption : L"").c_str(),
	             winport::WideToUtf8(lpText ? lpText : L"").c_str());
	return IDOK;
}

int WINAPI MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
	return MessageBoxExW(hWnd, lpText, lpCaption, uType, 0);
}

}  // extern "C"
