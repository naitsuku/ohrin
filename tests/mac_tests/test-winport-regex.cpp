/*
	test-winport-regex.cpp — 正規表現のテスト（mac 版独自）

	上流の CBregexp を通して、組み込みの bregonig.dll を確かめる。

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <string>

#include "extmodule/CBregexp.h"

namespace {

struct Regex {
	CBregexp re;
	explicit Regex(const wchar_t* pattern, int option = CBregexp::optCaseSensitive)
	{
		EXPECT_EQ(DLL_SUCCESS, re.InitDll(L"bregonig.dll"));
		EXPECT_TRUE(re.Compile(pattern, option)) << re.GetLastMessage();
	}
};

}  // namespace

TEST(WinportRegex, VersionIsTheRealLibrary)
{
	CBregexp re;
	ASSERT_EQ(DLL_SUCCESS, re.InitDll(L"bregonig.dll"));
	EXPECT_NE(nullptr, wcsstr(re.GetVersionW(), L"Onigmo"));
}

TEST(WinportRegex, MatchPosition)
{
	Regex r(L"b+");
	const std::wstring text = L"aabbbcc";
	ASSERT_TRUE(r.re.Match(text.c_str(), int(text.size()), 0));
	EXPECT_EQ(2, r.re.GetIndex());
	EXPECT_EQ(3, r.re.GetMatchLen());
	EXPECT_FALSE(r.re.Match(text.c_str(), int(text.size()), 5));
}

TEST(WinportRegex, IgnoreCaseAndJapanese)
{
	Regex r(L"あ.う", 0);
	const std::wstring text = L"xxあいうyy";
	ASSERT_TRUE(r.re.Match(text.c_str(), int(text.size()), 0));
	EXPECT_EQ(2, r.re.GetIndex());
	EXPECT_EQ(3, r.re.GetMatchLen());

	Regex c(L"abc", 0);
	const std::wstring upper = L"xABCx";
	EXPECT_TRUE(c.re.Match(upper.c_str(), int(upper.size()), 0));
}

// . はサロゲートペアを 1 文字として扱い、長さは UTF-16 単位で返る
TEST(WinportRegex, SurrogatePairIsOneCharacter)
{
	Regex r(L"a.b");
	const wchar_t text[] = {L'a', 0xD83D, 0xDE00, L'b', 0};
	ASSERT_TRUE(r.re.Match(text, 4, 0));
	EXPECT_EQ(0, r.re.GetIndex());
	EXPECT_EQ(4, r.re.GetMatchLen());
}

TEST(WinportRegex, LineStartAndEnd)
{
	Regex top(L"^b");
	const std::wstring text = L"ab\r\n";
	EXPECT_FALSE(top.re.Match(text.c_str(), int(text.size()), 1));	// 開始位置が途中でも行頭ではない

	Regex end(L"b$");
	ASSERT_TRUE(end.re.Match(text.c_str(), int(text.size()), 0));
	EXPECT_EQ(1, end.re.GetIndex());
	EXPECT_EQ(1, end.re.GetMatchLen());	// 改行は含まない
}

TEST(WinportRegex, LookBehindSeesTextBeforeStart)
{
	Regex r(L"(?<=a)b");
	const std::wstring text = L"ab";
	ASSERT_TRUE(r.re.Match(text.c_str(), int(text.size()), 1));
	EXPECT_EQ(1, r.re.GetIndex());
}

TEST(WinportRegex, ReplaceWithBackreference)
{
	CBregexp re;
	ASSERT_EQ(DLL_SUCCESS, re.InitDll(L"bregonig.dll"));
	ASSERT_TRUE(re.Compile(L"(\\w+)@(\\w+)", L"$2 at $1", CBregexp::optCaseSensitive));
	const std::wstring text = L"mail me@host now";
	EXPECT_EQ(1, re.Replace(text.c_str(), int(text.size()), 0));
	EXPECT_EQ(L"mail host at me now", std::wstring(re.GetString(), re.GetStringLen()));
}

TEST(WinportRegex, GlobalReplace)
{
	CBregexp re;
	ASSERT_EQ(DLL_SUCCESS, re.InitDll(L"bregonig.dll"));
	ASSERT_TRUE(re.Compile(L"あ", L"い", CBregexp::optCaseSensitive | CBregexp::optGlobal));
	const std::wstring text = L"あxあ";
	EXPECT_EQ(2, re.Replace(text.c_str(), int(text.size()), 0));
	EXPECT_EQ(L"いxい", std::wstring(re.GetString(), re.GetStringLen()));
}

TEST(WinportRegex, InvalidPatternReportsMessage)
{
	CBregexp re;
	ASSERT_EQ(DLL_SUCCESS, re.InitDll(L"bregonig.dll"));
	EXPECT_FALSE(re.Compile(L"(abc", CBregexp::optCaseSensitive));
	EXPECT_NE(L'\0', re.GetLastMessage()[0]);
}

TEST(WinportRegex, RecompileReplacesPattern)
{
	CBregexp re;
	ASSERT_EQ(DLL_SUCCESS, re.InitDll(L"bregonig.dll"));
	const std::wstring text = L"abc";
	ASSERT_TRUE(re.Compile(L"a", CBregexp::optCaseSensitive));
	EXPECT_TRUE(re.Match(text.c_str(), 3, 0));
	ASSERT_TRUE(re.Compile(L"z", CBregexp::optCaseSensitive));
	EXPECT_FALSE(re.Match(text.c_str(), 3, 0));
}

TEST(WinportRegex, OtherLibrariesStillFail)
{
	EXPECT_EQ(nullptr, ::LoadLibraryW(L"nosuch.dll"));
}
