/*
	tchar.h — externals/bregonig が使う文字列関数（wchar_t は 2 バイト）

	libc の wcs 系は 4 バイト前提なので使えない。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "windows.h"

#ifdef UNICODE
#define _T(x) L##x
#else
#define _T(x) x
#endif

static inline size_t _tcslen(const TCHAR* s)
{
	size_t n = 0;
	while (s[n]) ++n;
	return n;
}

static inline TCHAR* _tcschr(const TCHAR* s, int c)
{
	for (; *s; ++s) {
		if (*s == (TCHAR)c) return (TCHAR*)s;
	}
	return c == 0 ? (TCHAR*)s : NULL;
}

static inline unsigned long _tcstoul(const TCHAR* s, TCHAR** end, int base)
{
	char buf[32];
	size_t n = 0;
	while (n + 1 < sizeof(buf) && s[n] && s[n] < 0x80) {
		buf[n] = (char)s[n];
		++n;
	}
	buf[n] = '\0';
	char* stop = NULL;
	const unsigned long value = strtoul(buf, &stop, base);
	if (end) *end = (TCHAR*)s + (stop - buf);
	return value;
}

static inline long _tcstol(const TCHAR* s, TCHAR** end, int base)
{
	char buf[32];
	size_t n = 0;
	while (n + 1 < sizeof(buf) && s[n] && s[n] < 0x80) {
		buf[n] = (char)s[n];
		++n;
	}
	buf[n] = '\0';
	char* stop = NULL;
	const long value = strtol(buf, &stop, base);
	if (end) *end = (TCHAR*)s + (stop - buf);
	return value;
}

//! 書式は ASCII のみ。%s は 2 バイト文字列ではなく char 文字列として扱う（バージョン文字列でしか使わない）
static inline int _sntprintf(TCHAR* dest, size_t count, const TCHAR* format, ...)
{
	char narrow[256];
	size_t n = 0;
	while (n + 1 < sizeof(narrow) && format[n]) {
		narrow[n] = (char)format[n];
		++n;
	}
	narrow[n] = '\0';
	char out[256];
	va_list args;
	va_start(args, format);
	const int len = vsnprintf(out, sizeof(out), narrow, args);
	va_end(args);
	size_t i = 0;
	for (; i + 1 < count && out[i]; ++i) dest[i] = (TCHAR)(unsigned char)out[i];
	if (count) dest[i] = 0;
	return len;
}

#ifndef UNICODE
static inline char* lstrcpyn(char* dest, const char* src, int count)
{
	int i = 0;
	for (; i + 1 < count && src[i]; ++i) dest[i] = src[i];
	if (count > 0) dest[i] = 0;
	return dest;
}
#endif

//! asc2tcs の L"%hs" 専用
static inline int _snwprintf(TCHAR* dest, size_t count, const TCHAR*, const char* src)
{
	size_t i = 0;
	for (; i + 1 < count && src[i]; ++i) dest[i] = (TCHAR)(unsigned char)src[i];
	if (count) dest[i] = 0;
	return (int)i;
}
