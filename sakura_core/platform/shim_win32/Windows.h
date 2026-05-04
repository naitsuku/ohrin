/**
 * Windows.h shim for non-Windows builds.
 *
 * sakura_core/ の多数の .h は #include <Windows.h> を直接書いている。
 * 本シムは include path を介して Win32 ヘッダ参照をハイジャックし、
 * 必要最小限の型・マクロ・関数 stub だけを供給する。
 *
 * 大文字 Windows.h と小文字 windows.h の両方を提供している
 * （macOS は通常 case-insensitive だが、Linux では別ファイル）。
 *
 * このシムは _WIN32 ビルドでは絶対に拾われない（ルート CMakeLists.txt は
 * if(NOT WIN32) でしか include path に追加しない）。
 */
#pragma once
#ifdef _WIN32
#error "Windows.h shim must not be picked up on Win32 builds."
#endif

#include "../win32_types_stub.h"
