/*
	winport/include/mbstring.h — MSVC <mbstring.h> の代替（sakura_core が使う関数のみ）

	mingw-w64 版は FILE の定義が macOS と衝突するため使わない。
	JIS ⇔ Shift_JIS 変換は winport/src/crt.cpp で実装する（ロケール非依存の算術変換）。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include "winport_crt.h"

#ifdef __cplusplus
extern "C" {
#endif
unsigned int _mbcjistojms(unsigned int c);
unsigned int _mbcjmstojis(unsigned int c);
unsigned int _mbcjistojms_l(unsigned int c, _locale_t loc);
unsigned int _mbcjmstojis_l(unsigned int c, _locale_t loc);
#ifdef __cplusplus
}
#endif
