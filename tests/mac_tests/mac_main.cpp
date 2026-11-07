/*
	mac_main.cpp — 上流 tests1 を mac で走らせるための main

	上流の code-main.cpp は wmain・エディタ起動・Windows ロケール設定を含むため使わない。
	--gtest_filter を指定しなければ、mac では成立しない既知のテストを除外して走らせる。

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

namespace {

// mac では成立しない既知のテスト（理由ごと）。原因を直したら外すこと。
constexpr const char* kKnownUnsupported[] = {
	// 言語 DLL（sakura_lang_en_US.dll）のリソース切替が未実装
	"CSelectLang.test002",

	// Windows の MS ゴシックの画素幅（半角 6px / 全角 11px）を決め打ちしている
	"CharWidthCache.CalcPxWidthByFont3",

	// テストの非 BMP ワイドリテラル（L"\U0001F6B9" 等）が mac では UTF-32 の 1 要素になる（issues.md I-106）
	"CNativeW.GetCharPrev_Bugs_Preview",
	"CNativeW.GetSizeOfChar_IVS",
	"CNativeW.GetSizeOfChar_SurrogatePair",
	"CTextMetrics.GenerateDxArray8",
	"ConvertToUtf32.SurrogatePair",
	"IsVariationSelector.VariationSelectorCheck",
	"UnicodeToHexCases/UnicodeToHexTest.test/65",	// 1 要素の U+1F6B9 を 3 バイト分の領域へ 4 バイト書く（ASan で検出）
	"UnicodeToHexCases/UnicodeToHexTest.test/105",
	"UnicodeToHexCases/UnicodeToHexTest.test/106",
	"UnicodeToHexCases/UnicodeToHexTest.test/107",
	"UnicodeToHexCases/UnicodeToHexTest.test/108",
	"UnicodeToHexCases/UnicodeToHexTest.test/109",
	"UnicodeToHexCases/UnicodeToHexTest.test/110",
	"UnicodeToHexCases/UnicodeToHexTest.test/111",
	"UnicodeToHexCases/UnicodeToHexTest.test/112",
	"UnicodeToHexCases/UnicodeToHexTest.test/113",
	"UnicodeToHexCases/UnicodeToHexTest.test/114",
	"UnicodeToHexCases/UnicodeToHexTest.test/115",
	"UnicodeToHexCases/UnicodeToHexTest.test/116",
	"UnicodeToHexCases/UnicodeToHexTest.test/117",
	"UnicodeToHexCases/UnicodeToHexTest.test/118",
	"UnicodeToHexCases/UnicodeToHexTest.test/119",
	"UnicodeToHexCases/UnicodeToHexTest.test/120",
	"UnicodeToHexCases/UnicodeToHexTest.test/121",
	"UnicodeToHexCases/UnicodeToHexTest.test/122",
	"UnicodeToHexCases/UnicodeToHexTest.test/123",
	"UnicodeToHexCases/UnicodeToHexTest.test/58",
	"UnicodeToHexCases/UnicodeToHexTest.test/59",
	"UnicodeToHexCases/UnicodeToHexTest.test/60",
	"UnicodeToHexCases/UnicodeToHexTest.test/61",
	"UnicodeToHexCases/UnicodeToHexTest.test/62",
	"UnicodeToHexCases/UnicodeToHexTest.test/63",
	"UnicodeToHexCases/UnicodeToHexTest.test/64",
	"UnicodeToHexCases/UnicodeToHexTest.test/66",
	"UnicodeToHexCases/UnicodeToHexTest.test/67",
	"UnicodeToHexCases/UnicodeToHexTest.test/68",
	"WhatKindOfChar.IVS",
	"profile_data.TryParse_WCHAR",
	"to_string.test004",
	"to_wstring.test004",

	// wchar_t = 2 バイト前提（ページサイズ分の wmemset が確保範囲を超える）
	"MYDEVMODETest.StrategyForSegmentationFault",

	// CP_ACP = CP932 を前提にしたテスト（winport は ACP = UTF-8）
	"to_achar.test001",
	"to_string.test101",
	"to_wchar.test001",
};

}  // namespace

int main(int argc, char** argv)
{
	testing::InitGoogleMock(&argc, argv);
	if (GTEST_FLAG_GET(filter) == "*") {
		std::string filter = "*-";
		for (const auto* name : kKnownUnsupported) {
			filter += name;
			filter += ':';
		}
		GTEST_FLAG_SET(filter, filter);
	}
	return RUN_ALL_TESTS();
}
