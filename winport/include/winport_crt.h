/*
	winport_crt.h — MSVC CRT 拡張関数（_wcsicmp, wcscpy_s, _itow …）の宣言

	macOS の libc にない MSVC 固有の CRT 関数を宣言する。実装は winport/src/crt.cpp。
	注意: MSVC のワイド版 printf 系は "%s" をワイド文字列として扱う。
	      実装側（_swprintf 等）では MSVC の書式解釈に合わせること。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <time.h>

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
#ifndef STRUNCATE
#define STRUNCATE 80
#endif
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef __iscsym
#define __iscsym(c) (isalnum((unsigned char)(c)) || (c) == '_')
#endif
#ifndef _HEAP_MAXREQ
#define _HEAP_MAXREQ 0xFFFFFFFFFFFFFFE0  /* <malloc.h>: 1 回の確保の上限 */
#endif
#ifndef _MAX_PATH
#define _MAX_PATH   260
#define _MAX_DRIVE  3
#define _MAX_DIR    256
#define _MAX_FNAME  256
#define _MAX_EXT    256
#endif

typedef int errno_t;
/* mingw corecrt.h と同一の型（同じ typedef の重複は C/C++ とも合法） */
struct localeinfo_struct;
typedef struct localeinfo_struct* _locale_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 大文字小文字無視の比較 ---- */
int _memicmp(const void* a, const void* b, size_t n);
int _stricmp(const char* a, const char* b);
int _strnicmp(const char* a, const char* b, size_t n);
int _wcsicmp(const wchar_t* a, const wchar_t* b);
int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t* _wcslwr(wchar_t* s);
wchar_t* _wcsupr(wchar_t* s);
wint_t _towupper_l(wint_t c, _locale_t loc);
wint_t _towlower_l(wint_t c, _locale_t loc);

/* ---- セキュア CRT ---- */
errno_t strcpy_s(char* dst, size_t n, const char* src);
errno_t strncpy_s(char* dst, size_t n, const char* src, size_t count);
errno_t strcat_s(char* dst, size_t n, const char* src);
errno_t wcscpy_s(wchar_t* dst, size_t n, const wchar_t* src);
errno_t wcsncpy_s(wchar_t* dst, size_t n, const wchar_t* src, size_t count);
errno_t wcscat_s(wchar_t* dst, size_t n, const wchar_t* src);
errno_t wcsncat_s(wchar_t* dst, size_t n, const wchar_t* src, size_t count);
errno_t memcpy_s(void* dst, size_t n, const void* src, size_t count);
errno_t wmemcpy_s(wchar_t* dst, size_t n, const wchar_t* src, size_t count);
size_t wcsnlen_s(const wchar_t* s, size_t n);
wchar_t* wcstok_s(wchar_t* s, const wchar_t* delim, wchar_t** ctx);
errno_t _localtime64_s(struct tm* out, const long long* t);

/* ---- 書式化（MSVC 書式: ワイド版の %s はワイド文字列） ---- */
int _swprintf(wchar_t* buf, const wchar_t* fmt, ...);
int _vswprintf(wchar_t* buf, const wchar_t* fmt, va_list ap);
int swprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, ...);
int vswprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, va_list ap);
int _snwprintf_s(wchar_t* buf, size_t n, size_t count, const wchar_t* fmt, ...);
int _vsnwprintf_s(wchar_t* buf, size_t n, size_t count, const wchar_t* fmt, va_list ap);
int _vscwprintf(const wchar_t* fmt, va_list ap);
int _snprintf_s(char* buf, size_t n, size_t count, const char* fmt, ...);
int _vsnprintf_s(char* buf, size_t n, size_t count, const char* fmt, va_list ap);
int vsprintf_s(char* buf, size_t n, const char* fmt, va_list ap);
int _vscprintf(const char* fmt, va_list ap);
int swscanf_s(const wchar_t* buf, const wchar_t* fmt, ...);
int winport_swprintf_ms(wchar_t* buf, size_t n, const wchar_t* fmt, ...);
int winport_vswprintf_ms(wchar_t* buf, size_t n, const wchar_t* fmt, va_list ap);

/* ---- 数値変換 ---- */
int _wtoi(const wchar_t* s);
long _wtol(const wchar_t* s);
wchar_t* _itow(int v, wchar_t* buf, int radix);
wchar_t* _ultow(unsigned long v, wchar_t* buf, int radix);

/* ---- 文字列 ---- */
wchar_t* _wcsdup(const wchar_t* s);
wchar_t* _wcstok(wchar_t* s, const wchar_t* delim);

/* ---- パス・ファイル ---- */
void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext);
errno_t _wsplitpath_s(const wchar_t* path, wchar_t* drive, size_t nd, wchar_t* dir, size_t ndir,
                      wchar_t* fname, size_t nf, wchar_t* ext, size_t ne);
void _wmakepath(wchar_t* path, const wchar_t* drive, const wchar_t* dir, const wchar_t* fname, const wchar_t* ext);
wchar_t* _wfullpath(wchar_t* abs, const wchar_t* rel, size_t n);
FILE* _wfopen(const wchar_t* path, const wchar_t* mode);
int _wunlink(const wchar_t* path);
int _waccess(const wchar_t* path, int mode);
errno_t _waccess_s(const wchar_t* path, int mode);
wchar_t* _wtempnam(const wchar_t* dir, const wchar_t* prefix);
wchar_t* _wgetenv(const wchar_t* name);
int _fileno(FILE* fp);
long _filelength(int fd);

/* ---- ロケール・時刻 ---- */
_locale_t _create_locale(int category, const char* name);
void _free_locale(_locale_t loc);
wchar_t* _wsetlocale(int category, const wchar_t* name);
wchar_t* _wstrdate(wchar_t* buf);
long long _time64(long long* t);
void _tzset(void);

/* ---- スレッド ---- */
uintptr_t _beginthreadex(void* sec, unsigned stack, unsigned (*start)(void*), void* arg,
                         unsigned flags, unsigned* tid);

/* ---- その他 ---- */
unsigned short _byteswap_ushort(unsigned short v);

#ifdef __cplusplus
}  /* extern "C" */

/* MSVC の配列テンプレート版オーバーロード */
template <size_t N> inline errno_t wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscpy_s(dst, N, src); }
template <size_t N> inline errno_t wcsncpy_s(wchar_t (&dst)[N], const wchar_t* src, size_t c) { return wcsncpy_s(dst, N, src, c); }
template <size_t N> inline errno_t wcscat_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscat_s(dst, N, src); }
template <size_t N> inline errno_t wcsncat_s(wchar_t (&dst)[N], const wchar_t* src, size_t c) { return wcsncat_s(dst, N, src, c); }
template <size_t N> inline errno_t strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }
template <size_t N> inline errno_t strncpy_s(char (&dst)[N], const char* src, size_t c) { return strncpy_s(dst, N, src, c); }
template <size_t N> inline errno_t strcat_s(char (&dst)[N], const char* src) { return strcat_s(dst, N, src); }
template <size_t N> inline int swprintf_s(wchar_t (&buf)[N], const wchar_t* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = vswprintf_s(buf, N, fmt, ap); va_end(ap); return r;
}
template <size_t N> inline int _snwprintf_s(wchar_t (&buf)[N], size_t c, const wchar_t* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = _vsnwprintf_s(buf, N, c, fmt, ap); va_end(ap); return r;
}
template <size_t N> inline int _snprintf_s(char (&buf)[N], size_t c, const char* fmt, ...) {
	va_list ap; va_start(ap, fmt); int r = _vsnprintf_s(buf, N, c, fmt, ap); va_end(ap); return r;
}
template <size_t Nd, size_t Ndir, size_t Nf, size_t Ne>
inline errno_t _wsplitpath_s(const wchar_t* p, wchar_t (&d)[Nd], wchar_t (&dir)[Ndir], wchar_t (&f)[Nf], wchar_t (&e)[Ne]) {
	return _wsplitpath_s(p, d, Nd, dir, Ndir, f, Nf, e, Ne);
}
#endif

/* 標準名の swprintf / vswprintf を MSVC 書式（%s = ワイド）の実装へ差し替える。
   標準ヘッダは winport_prelude.h で先に読み込み済みなので、ここ以降のコードにだけ効く。 */
#define swprintf winport_swprintf_ms
#define vswprintf winport_vswprintf_ms
