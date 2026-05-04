/**
 * vadefs.h shim for non-Windows builds.
 *
 * Win32 では <vadefs.h> が va_list / va_start / va_end / va_arg の型定義を
 * 提供する MSVC 内部ヘッダ。POSIX 環境では <cstdarg> / <stdarg.h> が
 * 同じ機能を提供するので、それで置き換える。
 *
 * 本シムは _WIN32 ビルドでは絶対に拾われない。
 */
#pragma once
#ifdef _WIN32
#error "vadefs.h shim must not be picked up on Win32 builds."
#endif

#include <stdarg.h>
