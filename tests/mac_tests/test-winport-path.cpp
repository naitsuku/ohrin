/*
	test-winport-path.cpp — パスとテキスト読み込みのテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <cstdio>
#include <string>

#include "io/CTextStream.h"
#include "util/file.h"

namespace {

std::wstring ReadFirstLine(const char* bytes, size_t len)
{
	char path[] = "/tmp/winport_text_XXXXXX";
	const int fd = ::mkstemp(path);
	EXPECT_NE(-1, fd);
	EXPECT_EQ(static_cast<ssize_t>(len), ::write(fd, bytes, len));
	::close(fd);
	std::wstring wide;
	for (const char* p = path; *p; ++p) wide += static_cast<wchar_t>(*p);
	std::wstring line;
	{
		CTextInputStream in(wide.c_str());
		line = in.ReadLineW();
	}
	::unlink(path);
	return line;
}

}  // namespace

TEST(WinportPath, PosixAbsolutePathIsNotRelative)
{
	EXPECT_FALSE(_IS_REL_PATH(L"/Users/me/macro"));
	EXPECT_FALSE(_IS_REL_PATH(L"C:\\macro"));
	EXPECT_FALSE(_IS_REL_PATH(L"\\\\server\\macro"));
	EXPECT_TRUE(_IS_REL_PATH(L"macro/x.js"));
	EXPECT_TRUE(_IS_REL_PATH(L""));
}

TEST(WinportTextStream, Utf8WithoutBom)
{
	EXPECT_EQ(L"あ😀"[0], ReadFirstLine("\xE3\x81\x82\xF0\x9F\x98\x80\n", 8)[0]);
	EXPECT_EQ(3u, ReadFirstLine("\xE3\x81\x82\xF0\x9F\x98\x80\n", 8).size());	// サロゲートペアは 2 単位
}

TEST(WinportTextStream, Utf8WithBom)
{
	EXPECT_EQ(L"あ", ReadFirstLine("\xEF\xBB\xBF\xE3\x81\x82\n", 7));
}

// Windows から持ち込んだマクロは BOM なしの Shift_JIS
TEST(WinportTextStream, ShiftJisStaysShiftJis)
{
	EXPECT_EQ(L"あ", ReadFirstLine("\x82\xA0\n", 3));
}

TEST(WinportTextStream, TruncatedUtf8IsNotUtf8)
{
	EXPECT_NE(L"あ", ReadFirstLine("\xE3\x81", 2));
}

TEST(WinportTextStream, Ascii)
{
	EXPECT_EQ(L"abc", ReadFirstLine("abc\n", 4));
}
