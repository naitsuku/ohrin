/*
	winport_prelude.h — 全 TU に -include で強制挿入するプレリュード

	mingw-w64 の Windows ヘッダは _WIN32 / _WIN64 を前提とするが、
	libc++ や macOS SDK は _WIN32 を見ると Windows 用コードパスを選んでしまう。
	そこで標準ヘッダを先に読み込み、その後で Windows 用マクロを定義する。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#ifndef SAKURA_WINPORT
#define SAKURA_WINPORT 1
#endif

/* ---- 1. 標準ヘッダを先に読み込む（_WIN32 未定義の状態で） ---- */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

#ifdef __cplusplus
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#if __cplusplus >= 202002L
#include <concepts>
#include <format>
#include <memory_resource>
#include <ranges>
#include <source_location>
#include <span>
#endif
#endif

/* ---- 2. mingw CRT が型を再定義しないようにガードを立てる ---- */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#endif
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#endif
#ifndef _INTPTR_T_DEFINED
#define _INTPTR_T_DEFINED
#endif
#ifndef _UINTPTR_T_DEFINED
#define _UINTPTR_T_DEFINED
#endif
#ifndef _PTRDIFF_T_DEFINED
#define _PTRDIFF_T_DEFINED
#endif
#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
#endif
#ifndef _WCTYPE_T_DEFINED
#define _WCTYPE_T_DEFINED
#endif
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
#endif
#ifndef _VA_LIST_DEFINED
#define _VA_LIST_DEFINED
#endif
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
#endif
#ifndef _ERRCODE_DEFINED
#define _ERRCODE_DEFINED
#endif
#ifndef _RSIZE_T_DEFINED
#define _RSIZE_T_DEFINED
#endif
typedef size_t rsize_t;
typedef int errno_t;

/* ---- 3. Windows 用マクロ ---- */
#define _WIN32 1
#define _WIN64 1
/* 上流は MinGW ビルドに対応しており、__MINGW32__ 分岐（TComPtr 等）が GCC + mingw ヘッダ向けに書かれている */
#define __MINGW32__ 1
#define __MINGW64__ 1
#define WIN32 1
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define NTDDI_VERSION 0x0A000000
#define UNICODE 1
#define _UNICODE 1
#define NOMINMAX 1
#define _WINSOCKAPI_ 1  /* winsock.h は POSIX ソケットと衝突する */

/* ---- 4. MSVC CRT 拡張 ---- */
#include "winport_crt.h"
