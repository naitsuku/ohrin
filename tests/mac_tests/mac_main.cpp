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
