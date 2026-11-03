/*
	mac_main.cpp — 上流 tests1 を mac で走らせるための main

	上流の code-main.cpp は wmain・エディタ起動・Windows ロケール設定を含むため使わない。

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

int main(int argc, char** argv)
{
	testing::InitGoogleMock(&argc, argv);
	return RUN_ALL_TESTS();
}
