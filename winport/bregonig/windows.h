/*
	windows.h — externals/bregonig をビルドするための最小の代替ヘッダ

	bregonig は wchar_t が 2 バイトであることを前提にしているので、このライブラリだけ -fshort-wchar で
	コンパイルする。mingw のヘッダは使わない（winport_prelude.h が 4 バイト前提）。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef int BOOL;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef intptr_t INT_PTR;
typedef void* HINSTANCE;
typedef void* LPVOID;
#ifdef UNICODE
typedef wchar_t TCHAR;
typedef wchar_t TBYTE;
#else
typedef char TCHAR;
typedef unsigned char TBYTE;
#endif
typedef const TCHAR* LPCTSTR;

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

#define TRUE 1
#define FALSE 0
#define WINAPI
#define __stdcall
#define __cdecl

// デバッグ出力（dbgtrace.h）は使わない
#define wvsprintf(buf, msg, ap) ((void)(buf))
#define OutputDebugString(s) ((void)(s))

// mem_vc6.h の代わり。new は失敗すると bad_alloc を投げる
#define set_new_throw_bad_alloc() ((void)0)

#include "tchar.h"
