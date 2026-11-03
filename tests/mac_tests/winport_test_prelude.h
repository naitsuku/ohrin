/*
	winport_test_prelude.h — テスト用プレリュード

	GoogleTest は _WIN32 を見ると Windows 用の実装を選ぶため、
	winport_prelude.h が _WIN32 を定義する前に gtest / gmock を読み込んでおく。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "winport_prelude.h"
