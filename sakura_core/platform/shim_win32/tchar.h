/**
 * tchar.h shim for non-Windows builds.
 *
 * Win32 の <tchar.h> は TCHAR / _T() / _tcs* 系を提供する。
 * 非 Windows では win32_types_stub.h が同等の宣言を持っているため、
 * 単に転送するだけで十分。
 *
 * 本シムは _WIN32 ビルドでは絶対に拾われない。
 */
#pragma once
#ifdef _WIN32
#error "tchar.h shim must not be picked up on Win32 builds."
#endif

#include "../win32_types_stub.h"
