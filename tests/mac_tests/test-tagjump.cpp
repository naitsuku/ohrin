/*
	test-tagjump.cpp — タグジャンプ一覧のテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include "env/CSortedTagJumpList.h"

// 備考の無い tags 行（"Foo\ta.cpp\t3;\"\tc"）では備考が空文字列になる。to_wchar("") は nullptr なので落ちていた
TEST(CSortedTagJumpList, EmptyNoteBecomesEmptyString)
{
	CSortedTagJumpList list(10);
	const int dir = list.AddBaseDir(L"/tmp/");
	EXPECT_TRUE(list.AddParamA("Foo", "a.cpp", 3, 'c', "", 0, dir));
	EXPECT_TRUE(list.AddParamA("Foo", "b.cpp", 1, 'c', "class:Bar", 0, dir));
	ASSERT_EQ(2, list.GetCount());
	WCHAR keyword[1024], filename[1024], type[2], note[1024], baseDir[1024];
	int no = 0, depth = 0;
	ASSERT_TRUE(list.GetParam(0, keyword, filename, &no, type, note, &depth, baseDir));
	EXPECT_STREQ(L"Foo", keyword);
	EXPECT_STREQ(L"", note);
	ASSERT_TRUE(list.GetParam(1, keyword, filename, &no, type, note, &depth, baseDir));
	EXPECT_STREQ(L"class:Bar", note);
}
