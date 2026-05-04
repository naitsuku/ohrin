// tests/wx_tests/test_funccode.cpp
//
// Phase 12 で追加。tools/porting/gen_funccode.py が CMake から起動され、
// build/generated/sakura_core/Funccode_enum.h を出力していることを確認する
// スモークテスト。upstream Funccode_x.hsrc の主要 ID と値の互換性を検証。
//
// このテストが通ること = 非 Windows ビルドで Funccode_enum.h 経由の
// 依存チェーン (func/CKeyBind.h など) を解決する道が開けていることを示す。

#include <gtest/gtest.h>

// gen_funccode.py が出力するヘッダ。include path は sakura_core/CMakeLists.txt で
// SAKURA_GENERATED_DIR (= build/generated/sakura_core) を追加済み。
#include "Funccode_enum.h"

TEST(Funccode, SentinelValuesMatchUpstream) {
    // upstream sakura_core/Funccode_x.hsrc 抜粋:
    //   F_INVALID = -1
    //   F_DEFAULT = 0
    //   F_SEPARATOR = 1
    EXPECT_EQ(static_cast<int>(F_INVALID),   -1);
    EXPECT_EQ(static_cast<int>(F_DEFAULT),    0);
    EXPECT_EQ(static_cast<int>(F_SEPARATOR),  1);
}

TEST(Funccode, EditCommandCodesPresent) {
    // 編集系の代表的なコマンドが定義されていることを確認 (値は存在さえすれば良い)。
    // 不在ならコンパイルエラーになる。
    EFunctionCode codes[] = {
        F_UNDO, F_REDO,
        F_CUT,  F_COPY, F_PASTE,
        F_DELETE_BACK, F_DELETE,
        F_FILESAVE, F_FILESAVEAS_DIALOG,
        F_FILEOPEN, F_FILENEW,
    };
    EXPECT_GT(std::size(codes), 0u);
    for (auto c : codes) {
        // F_INVALID(-1) でないことのみ確認
        EXPECT_NE(static_cast<int>(c), -1);
    }
}
