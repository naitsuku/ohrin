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

#include "backend.h"
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

std::mutex& VmMutex() { static auto* m = new std::mutex; return *m; }
std::map<void*, size_t>& VmSizes() { static auto* m = new std::map<void*, size_t>; return *m; }

}  // namespace

namespace winport { DWORD CurrentProcessId(); }  // process.cpp

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
namespace {
int ToProt(DWORD protect)
{
	switch (protect & 0xFF) {
	case PAGE_NOACCESS: return PROT_NONE;
	case PAGE_READONLY: return PROT_READ;
	case PAGE_EXECUTE: return PROT_EXEC;
	case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
	case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
	default: return PROT_READ | PROT_WRITE;
	}
}
}  // namespace

void WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo)
{
	*lpSystemInfo = {};
	lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
	lpSystemInfo->dwPageSize = static_cast<DWORD>(getpagesize());
	lpSystemInfo->dwAllocationGranularity = static_cast<DWORD>(getpagesize());
	lpSystemInfo->dwNumberOfProcessors = static_cast<DWORD>(sysconf(_SC_NPROCESSORS_ONLN));
	lpSystemInfo->lpMinimumApplicationAddress = reinterpret_cast<LPVOID>(intptr_t{0x10000});
	lpSystemInfo->lpMaximumApplicationAddress = reinterpret_cast<LPVOID>(intptr_t{0x7FFFFFFEFFFF});
}

LPVOID WINAPI VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
	if (lpAddress) {
		// 予約済み領域内のコミット
		if (!(flAllocationType & MEM_COMMIT)) return lpAddress;
		if (mprotect(lpAddress, dwSize, ToProt(flProtect)) != 0) { SetLastError(ERROR_INVALID_ADDRESS); return nullptr; }
		return lpAddress;
	}
	// MEM_RESERVE のみなら触れない領域として確保する
	const int prot = (flAllocationType & MEM_COMMIT) ? ToProt(flProtect) : PROT_NONE;
	void* p = mmap(nullptr, dwSize, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (p == MAP_FAILED) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
	std::lock_guard lock(VmMutex());
	VmSizes()[p] = dwSize;
	return p;
}

WINBOOL WINAPI VirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect)
{
	// 以前の保護属性は追跡していないため READWRITE を返す
	if (lpflOldProtect) *lpflOldProtect = PAGE_READWRITE;
	if (mprotect(lpAddress, dwSize, ToProt(flNewProtect)) != 0) { SetLastError(ERROR_INVALID_ADDRESS); return FALSE; }
	return TRUE;
}

WINBOOL WINAPI VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
	if (!(dwFreeType & MEM_RELEASE)) {
		// MEM_DECOMMIT: 触れない状態に戻す
		return mprotect(lpAddress, dwSize, PROT_NONE) == 0 ? TRUE : FALSE;
	}
	std::lock_guard lock(VmMutex());
	const auto it = VmSizes().find(lpAddress);
	if (it == VmSizes().end()) { SetLastError(ERROR_INVALID_ADDRESS); return FALSE; }
	munmap(it->first, it->second);
	VmSizes().erase(it);
	return TRUE;
}

/* ---- 標準ハンドル（擬似値） ---- */
HANDLE WINAPI GetStdHandle(DWORD nStdHandle)
{
	// GUI アプリには標準ハンドルが無い（上流の Wrap_MessageBox は、標準エラーがあると
	// ダイアログを出さずにそこへ書くので、アプリ動作時は Windows の GUI アプリと同じ null を返す）
	if (winport::backend::Available()) return nullptr;
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

// 仮想プロセス（スレッド）の ID を返す（process.cpp）
DWORD WINAPI GetCurrentProcessId(void) { return winport::CurrentProcessId(); }
DWORD WINAPI GetCurrentThreadId(void)
{
	uint64_t tid = 0;
	pthread_threadid_np(nullptr, &tid);
	return static_cast<DWORD>(tid);
}

}  // extern "C"

/* ---- 日付・時刻の書式（GetDateFormat / GetTimeFormat）----

	上流は日付挿入・時刻挿入、印刷のヘッダー／フッター、$d $t の展開で使う。戻り値を見ずにバッファを
	使うので、失敗時も空文字列を入れておく。ロケールは日本語とそれ以外（en-US）の 2 種類だけ持つ。
*/
namespace {

bool IsJapanese(LCID locale) { return PRIMARYLANGID(LANGIDFROMLCID(locale)) == LANG_JAPANESE; }

//! 0 = 日曜。SYSTEMTIME の wDayOfWeek は信用せず年月日から求める（Win32 と同じ）
int DayOfWeek(int y, int m, int d)
{
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	if (m < 3) --y;
	return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

std::wstring Number(int value, int width)
{
	std::wstring s = std::to_wstring(value);
	if (static_cast<int>(s.size()) < width) s.insert(0, width - s.size(), L'0');
	return s;
}

std::wstring DatePart(wchar_t c, int n, const SYSTEMTIME& st, bool ja)
{
	static const wchar_t* const kDayJa[] = {L"日", L"月", L"火", L"水", L"木", L"金", L"土"};
	static const wchar_t* const kDayEn[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
	static const wchar_t* const kMonthEn[] = {L"January", L"February", L"March", L"April", L"May", L"June", L"July",
		L"August", L"September", L"October", L"November", L"December"};
	switch (c) {
	case L'd': {
		if (n <= 2) return Number(st.wDay, n);
		const int w = DayOfWeek(st.wYear, st.wMonth, st.wDay);
		if (ja) return std::wstring(kDayJa[w]) + (n == 3 ? L"" : L"曜日");
		const std::wstring name = kDayEn[w];
		return n == 3 ? name.substr(0, 3) : name;
	}
	case L'M': {
		if (n <= 2) return Number(st.wMonth, n);
		if (ja) return Number(st.wMonth, 1) + (n == 3 ? L"" : L"月");
		const std::wstring name = kMonthEn[st.wMonth - 1];
		return n == 3 ? name.substr(0, 3) : name;
	}
	case L'y':
		return n >= 4 ? Number(st.wYear, 4) : Number(st.wYear % 100, n >= 2 ? 2 : 1);
	default:	// g, gg: 時代。西暦のみ
		return ja ? L"西暦" : L"A.D.";
	}
}

std::wstring TimePart(wchar_t c, int n, const SYSTEMTIME& st, bool ja)
{
	const int width = n >= 2 ? 2 : 1;
	switch (c) {
	case L'h': return Number(st.wHour % 12 ? st.wHour % 12 : 12, width);
	case L'H': return Number(st.wHour, width);
	case L'm': return Number(st.wMinute, width);
	case L's': return Number(st.wSecond, width);
	default: {	// t, tt: 午前／午後
		const std::wstring marker = st.wHour < 12 ? (ja ? L"午前" : L"AM") : (ja ? L"午後" : L"PM");
		return n == 1 ? marker.substr(0, 1) : marker;
	}
	}
}

//! 書式を展開する。' で囲んだ範囲はそのまま出力し、' 2 個は ' 1 文字になる
template <class PartFn>
std::wstring ExpandPicture(const wchar_t* format, const wchar_t* specifiers, PartFn part)
{
	std::wstring out;
	bool quoted = false;
	for (const wchar_t* p = format; *p; ) {
		if (*p == L'\'') {
			if (p[1] == L'\'') { out += L'\''; p += 2; continue; }
			quoted = !quoted;
			++p;
		} else if (!quoted && std::wcschr(specifiers, *p)) {
			const wchar_t c = *p;
			int n = 0;
			while (*p == c) { ++n; ++p; }
			out += part(c, n);
		} else {
			out += *p++;
		}
	}
	return out;
}

int FinishFormat(const std::wstring& text, LPWSTR dest, int cch)
{
	const int need = static_cast<int>(text.size()) + 1;
	if (cch == 0) return need;
	if (cch < need) {
		dest[0] = L'\0';
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	std::wmemcpy(dest, text.c_str(), need);
	return need;
}

int FailFormat(LPWSTR dest, int cch)
{
	if (dest && cch > 0) dest[0] = L'\0';
	SetLastError(ERROR_INVALID_PARAMETER);
	return 0;
}

}  // namespace

int WINAPI GetDateFormatW(LCID Locale, DWORD dwFlags, const SYSTEMTIME* lpDate, LPCWSTR lpFormat, LPWSTR lpDateStr, int cchDate)
{
	SYSTEMTIME now;
	if (!lpDate) { GetLocalTime(&now); lpDate = &now; }
	if (cchDate < 0 || (cchDate > 0 && !lpDateStr) || lpDate->wMonth < 1 || lpDate->wMonth > 12 || lpDate->wDay < 1 || lpDate->wDay > 31) {
		return FailFormat(lpDateStr, cchDate);
	}
	const bool ja = IsJapanese(Locale);
	if (!lpFormat) {
		if (dwFlags & DATE_LONGDATE) lpFormat = ja ? L"yyyy'年'M'月'd'日'" : L"dddd, MMMM d, yyyy";
		else lpFormat = ja ? L"yyyy/MM/dd" : L"M/d/yyyy";
	}
	return FinishFormat(ExpandPicture(lpFormat, L"dMyg", [&](wchar_t c, int n) { return DatePart(c, n, *lpDate, ja); }), lpDateStr, cchDate);
}

int WINAPI GetTimeFormatW(LCID Locale, DWORD, const SYSTEMTIME* lpTime, LPCWSTR lpFormat, LPWSTR lpTimeStr, int cchTime)
{
	SYSTEMTIME now;
	if (!lpTime) { GetLocalTime(&now); lpTime = &now; }
	if (cchTime < 0 || (cchTime > 0 && !lpTimeStr) || lpTime->wHour > 23 || lpTime->wMinute > 59 || lpTime->wSecond > 59) {
		return FailFormat(lpTimeStr, cchTime);
	}
	const bool ja = IsJapanese(Locale);
	if (!lpFormat) lpFormat = ja ? L"H:mm:ss" : L"h:mm:ss tt";
	return FinishFormat(ExpandPicture(lpFormat, L"hHmst", [&](wchar_t c, int n) { return TimePart(c, n, *lpTime, ja); }), lpTimeStr, cchTime);
}

/* ---- advapi32: レジストリ ----

	mac にレジストリは無いので「キーが無い」と答える。スタブの 0 は ERROR_SUCCESS になり、上流が
	未設定のバッファを成功として読んでしまう。
*/
LSTATUS WINAPI RegOpenKeyExW(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY phkResult)
{
	if (phkResult) *phkResult = nullptr;
	return ERROR_FILE_NOT_FOUND;
}
LSTATUS WINAPI RegCreateKeyExW(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY phkResult, LPDWORD)
{
	if (phkResult) *phkResult = nullptr;
	return ERROR_ACCESS_DENIED;
}
LSTATUS WINAPI RegCloseKey(HKEY) { return ERROR_INVALID_HANDLE; }
LSTATUS WINAPI RegQueryValueExW(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LSTATUS WINAPI RegSetValueExW(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD) { return ERROR_ACCESS_DENIED; }
LSTATUS WINAPI RegDeleteKeyW(HKEY, LPCWSTR) { return ERROR_FILE_NOT_FOUND; }
LSTATUS WINAPI RegDeleteValueW(HKEY, LPCWSTR) { return ERROR_FILE_NOT_FOUND; }
LSTATUS WINAPI RegEnumKeyExW(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME) { return ERROR_NO_MORE_ITEMS; }
LSTATUS WINAPI RegEnumValueW(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_NO_MORE_ITEMS; }
