/**
 * win32_types_stub.h
 *
 * 非 Windows 環境で sakura_core の既存ファイルをコンパイルするための
 * Win32 型・マクロ・関数の最小限 stub 定義。
 *
 * これは「コンパイルを通すため」だけの宣言群であり、実装は提供しない。
 * 実際の動作は sakura_core/platform/platform.h（PAL）経由で行うこと。
 *
 * 使い方:
 *   #ifndef _WIN32
 *   #include "platform/win32_types_stub.h"
 *   #endif
 *
 * このファイルは Win32 ビルドでは絶対にインクルードされない。
 */
#pragma once
#ifndef _WIN32

#include <cstdarg>     // va_list / va_start / va_end (vsnprintf 系)
#include <cstddef>
#include <cstdint>
#include <cstdio>      // vsnprintf
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <strings.h>  // strcasecmp / strncasecmp / wcscasecmp / wcsncasecmp
#include <wctype.h>

// ============================================================
// 基本整数型
// ============================================================
using BOOL    = int;
using DWORD   = uint32_t;
using WORD    = uint16_t;
using BYTE    = uint8_t;
using LONG    = int32_t;
using ULONG   = uint32_t;
using LONGLONG  = int64_t;
using ULONGLONG = uint64_t;
using UINT    = unsigned int;
using INT     = int;
using SHORT   = int16_t;
using USHORT  = uint16_t;
using WPARAM  = uintptr_t;
using LPARAM  = intptr_t;
using LRESULT = intptr_t;

// ============================================================
// ハンドル型（不透明ポインタ）
// ============================================================
using HANDLE    = void*;
using HWND      = void*;
using HDC       = void*;
using HFONT     = void*;
using HBRUSH    = void*;
using HPEN      = void*;
using HBITMAP   = void*;
using HICON     = void*;
using HCURSOR   = void*;
using HMENU     = void*;
using HINSTANCE = void*;
using HMODULE   = void*;
using HKEY      = void*;
using HRGN      = void*;
using HACCEL    = void*;
using HGLOBAL   = void*;
using HGDIOBJ   = void*;
using HRSRC     = void*;
using HIMAGELIST = void*;

// ============================================================
// 文字列型
// ============================================================
using CHAR    = char;
using WCHAR   = wchar_t;
using LPWSTR  = wchar_t*;
using LPCWSTR = const wchar_t*;
using LPSTR   = char*;
using LPCSTR  = const char*;
using TCHAR   = wchar_t;
using LPTSTR  = wchar_t*;
using LPCTSTR = const wchar_t*;
using PCWSTR  = const wchar_t*;
using PWSTR   = wchar_t*;
using PCSTR   = const char*;
using PSTR    = char*;
using LPVOID  = void*;
using LPCVOID = const void*;
using LPBYTE  = uint8_t*;

// ============================================================
// 定数・マクロ
// ============================================================
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef _MAX_PATH
#define _MAX_PATH MAX_PATH
#endif

// MSVC safe-string が文字列切り詰め時に返す値 (errno_t)
#ifndef STRUNCATE
#define STRUNCATE 80
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL nullptr
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)(-1))
#endif

// 呼び出し規約マクロ（非Windowsでは無効）
#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef PASCAL
#define PASCAL
#endif

// ============================================================
// 構造体
// ============================================================
struct POINT { LONG x; LONG y; };
struct SIZE  { LONG cx; LONG cy; };
struct RECT  { LONG left; LONG top; LONG right; LONG bottom; };
struct FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; };
struct SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay;
    WORD wHour, wMinute, wSecond, wMilliseconds;
};

// ============================================================
// 色 (GDI 由来だが値型として広く使われるため stub に残す)
// ============================================================
using COLORREF = DWORD;
#ifndef RGB
#define RGB(r,g,b) (static_cast<COLORREF>( \
    (static_cast<DWORD>(static_cast<BYTE>(r))) | \
    (static_cast<DWORD>(static_cast<BYTE>(g)) << 8) | \
    (static_cast<DWORD>(static_cast<BYTE>(b)) << 16)))
#endif

// ============================================================
// 文字列関数（Win32 名 → POSIX 名 のマッピング）
// 実装はインライン関数で提供。
// ============================================================
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline int _wtoi(const wchar_t* s) { return static_cast<int>(wcstol(s, nullptr, 10)); }
inline long _wtol(const wchar_t* s) { return wcstol(s, nullptr, 10); }
inline long long _wtoi64(const wchar_t* s) { return wcstoll(s, nullptr, 10); }

// _memicmp: memory case-insensitive 比較（MSVC 拡張）
inline int _memicmp(const void* a, const void* b, size_t n) {
    auto* pa = static_cast<const unsigned char*>(a);
    auto* pb = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i) {
        int ca = static_cast<int>(std::tolower(pa[i]));
        int cb = static_cast<int>(std::tolower(pb[i]));
        if (ca != cb) return ca - cb;
    }
    return 0;
}

// MSVC safe-string 系（_s 接尾辞）。
// stub では境界チェックを省略してアンセーフ版へ転送する。
// _TRUNCATE は MSVC で「dest バッファに合うだけ書き込む」サイズ指定だが、
// ここではただの sentinel として扱う。
#ifndef _TRUNCATE
#define _TRUNCATE (static_cast<size_t>(-1))
#endif

inline int strncpy_s(char* dest, size_t destSize, const char* src, size_t n) {
    if (!dest || !src || destSize == 0) return 22; // EINVAL 相当
    if (n == _TRUNCATE) n = destSize - 1;
    if (n >= destSize) n = destSize - 1;
    std::strncpy(dest, src, n);
    dest[n] = '\0';
    return 0;
}
inline int wcsncpy_s(wchar_t* dest, size_t destSize, const wchar_t* src, size_t n) {
    if (!dest || !src || destSize == 0) return 22;
    if (n == _TRUNCATE) n = destSize - 1;
    if (n >= destSize) n = destSize - 1;
    wcsncpy(dest, src, n);
    dest[n] = L'\0';
    return 0;
}
inline int strcat_s(char* dest, size_t destSize, const char* src) {
    if (!dest || !src || destSize == 0) return 22;
    size_t curLen = std::strlen(dest);
    if (curLen >= destSize) return 34; // ERANGE 相当
    size_t avail = destSize - curLen - 1;
    std::strncat(dest, src, avail);
    return 0;
}
inline int wcscat_s(wchar_t* dest, size_t destSize, const wchar_t* src) {
    if (!dest || !src || destSize == 0) return 22;
    size_t curLen = wcslen(dest);
    if (curLen >= destSize) return 34;
    size_t avail = destSize - curLen - 1;
    wcsncat(dest, src, avail);
    return 0;
}
inline int strcpy_s(char* dest, size_t destSize, const char* src) {
    return strncpy_s(dest, destSize, src, _TRUNCATE);
}
inline int wcscpy_s(wchar_t* dest, size_t destSize, const wchar_t* src) {
    return wcsncpy_s(dest, destSize, src, _TRUNCATE);
}

// errno_t: MSVC 拡張 (Annex K)。POSIX では int で十分。
using errno_t = int;

// _vswprintf: MSVC の古い API。POSIX の vswprintf にマップ。
// 注: MSVC _vswprintf はバッファサイズを取らないが、安全側に倒すため
// vswprintf の N 引数版を呼ぶ stub にしている (誤動作の可能性あり、
// 呼出側でバッファ十分性を確認している前提)。
inline int _vswprintf(wchar_t* buf, const wchar_t* fmt, va_list args) {
    return ::vswprintf(buf, 4096, fmt, args);
}

// MSVC safe-printf 系。POSIX の vsnprintf / vswprintf にマップ。
// 切り詰めが起きると Annex K では STRUNCATE を返す約束だが、ここでは
// 簡易に長さを返す実装にする（呼出側で書き込まれた長さしか見ていない想定）。
inline int _vsnprintf_s(char* buf, size_t bufSize, size_t /*maxCount*/,
                        const char* fmt, va_list args) {
    int n = ::vsnprintf(buf, bufSize, fmt, args);
    return n;
}
inline int _vsnwprintf_s(wchar_t* buf, size_t bufSize, size_t /*maxCount*/,
                         const wchar_t* fmt, va_list args) {
    int n = ::vswprintf(buf, bufSize, fmt, args);
    return n;
}
inline int _snprintf_s(char* buf, size_t bufSize, size_t maxCount,
                       const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = _vsnprintf_s(buf, bufSize, maxCount, fmt, args);
    va_end(args);
    return n;
}
inline int _snwprintf_s(wchar_t* buf, size_t bufSize, size_t maxCount,
                        const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = _vsnwprintf_s(buf, bufSize, maxCount, fmt, args);
    va_end(args);
    return n;
}
inline int vswprintf_s(wchar_t* buf, size_t bufSize,
                       const wchar_t* fmt, va_list args) {
    return ::vswprintf(buf, bufSize, fmt, args);
}

// _T() / TEXT() マクロ — Win32 でワイド/マルチバイトを切り替えるが、
// ここでは常に wchar_t 文字列リテラルとして扱う。
#ifndef _T
#define _T(x) L##x
#endif
#ifndef TEXT
#define TEXT(x) L##x
#endif

// _tcs* 系（TCHAR 用文字列関数）→ wcs* 系
inline size_t _tcslen(const wchar_t* s) { return wcslen(s); }
inline int _tcscmp(const wchar_t* a, const wchar_t* b) { return wcscmp(a, b); }
inline int _tcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _tcsncmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncmp(a, b, n); }
inline int _tcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return wcsncasecmp(a, b, n); }
inline wchar_t* _tcscpy(wchar_t* d, const wchar_t* s) { return wcscpy(d, s); }
inline wchar_t* _tcsncpy(wchar_t* d, const wchar_t* s, size_t n) { return wcsncpy(d, s, n); }
inline wchar_t* _tcscat(wchar_t* d, const wchar_t* s) { return wcscat(d, s); }
inline wchar_t* _tcschr(wchar_t* s, wchar_t c) { return wcschr(s, c); }
inline const wchar_t* _tcschr(const wchar_t* s, wchar_t c) { return wcschr(s, c); }
inline wchar_t* _tcsrchr(wchar_t* s, wchar_t c) { return wcsrchr(s, c); }
inline const wchar_t* _tcsrchr(const wchar_t* s, wchar_t c) { return wcsrchr(s, c); }
inline wchar_t* _tcsstr(wchar_t* h, const wchar_t* n) { return wcsstr(h, n); }
inline const wchar_t* _tcsstr(const wchar_t* h, const wchar_t* n) { return wcsstr(h, n); }

#endif // !_WIN32
