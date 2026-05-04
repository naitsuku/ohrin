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

#include <cstddef>
#include <cstdint>
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
