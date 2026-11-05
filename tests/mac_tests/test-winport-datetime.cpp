/*
	test-winport-datetime.cpp — GetDateFormat / GetTimeFormat とレジストリのテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <string>

namespace {

constexpr LCID kJa = MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);
constexpr LCID kEn = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

SYSTEMTIME Make(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
{
	SYSTEMTIME st{};
	st.wYear = static_cast<WORD>(y);
	st.wMonth = static_cast<WORD>(mo);
	st.wDay = static_cast<WORD>(d);
	st.wHour = static_cast<WORD>(h);
	st.wMinute = static_cast<WORD>(mi);
	st.wSecond = static_cast<WORD>(s);
	return st;
}

std::wstring Date(LCID lcid, DWORD flags, const SYSTEMTIME& st, const wchar_t* format)
{
	wchar_t buf[128];
	EXPECT_GT(::GetDateFormatW(lcid, flags, &st, format, buf, 128), 0);
	return buf;
}

std::wstring Time(LCID lcid, const SYSTEMTIME& st, const wchar_t* format)
{
	wchar_t buf[128];
	EXPECT_GT(::GetTimeFormatW(lcid, 0, &st, format, buf, 128), 0);
	return buf;
}

}  // namespace

TEST(WinportDateTime, DefaultDate)
{
	const auto st = Make(2026, 11, 5);
	EXPECT_EQ(L"2026年11月5日", Date(kJa, DATE_LONGDATE, st, nullptr));
	EXPECT_EQ(L"2026/11/05", Date(kJa, 0, st, nullptr));
	EXPECT_EQ(L"Thursday, November 5, 2026", Date(kEn, DATE_LONGDATE, st, nullptr));
}

// sakura の既定のカスタム書式（CShareData.cpp）
TEST(WinportDateTime, UpstreamCustomFormats)
{
	EXPECT_EQ(L"2026年11月5日(木曜日)", Date(kJa, 0, Make(2026, 11, 5), L"yyyy'年'M'月'd'日('dddd')'"));
	EXPECT_EQ(L"午前07時43分04秒", Time(kJa, Make(2026, 11, 5, 7, 43, 4), L"tthh'時'mm'分'ss'秒'"));
	EXPECT_EQ(L"午後01時05分09秒", Time(kJa, Make(2026, 11, 5, 13, 5, 9), L"tthh'時'mm'分'ss'秒'"));
}

TEST(WinportDateTime, Specifiers)
{
	const auto st = Make(2024, 2, 9);
	EXPECT_EQ(L"9 09 金 金曜日", Date(kJa, 0, st, L"d dd ddd dddd"));
	EXPECT_EQ(L"2 02 2 2月", Date(kJa, 0, st, L"M MM MMM MMMM"));
	EXPECT_EQ(L"Fri Feb", Date(kEn, 0, st, L"ddd MMM"));
	EXPECT_EQ(L"24 24 2024", Date(kJa, 0, st, L"y yy yyyy"));
	EXPECT_EQ(L"it's d", Date(kJa, 0, st, L"'it''s d'"));
}

TEST(WinportDateTime, Time)
{
	EXPECT_EQ(L"7:03:04", Time(kJa, Make(2026, 1, 1, 7, 3, 4), nullptr));
	EXPECT_EQ(L"7:03:04 PM", Time(kEn, Make(2026, 1, 1, 19, 3, 4), nullptr));
	EXPECT_EQ(L"12 12 00", Time(kJa, Make(2026, 1, 1, 0, 0, 0), L"h hh HH"));
	EXPECT_EQ(L"12 12", Time(kJa, Make(2026, 1, 1, 12, 0, 0), L"h HH"));
}

// 上流は戻り値を見ずにバッファを使うので、失敗しても文字列として読める必要がある
TEST(WinportDateTime, Failure)
{
	const auto st = Make(2026, 11, 5);
	EXPECT_EQ(5, ::GetDateFormatW(kJa, 0, &st, L"yyyy", nullptr, 0));

	wchar_t small[3] = {L'x', L'x', L'x'};
	EXPECT_EQ(0, ::GetDateFormatW(kJa, 0, &st, L"yyyy", small, 3));
	EXPECT_EQ(ERROR_INSUFFICIENT_BUFFER, ::GetLastError());
	EXPECT_EQ(L'\0', small[0]);

	const auto bad = Make(2026, 13, 5);
	wchar_t buf[8] = {L'x'};
	EXPECT_EQ(0, ::GetDateFormatW(kJa, 0, &bad, nullptr, buf, 8));
	EXPECT_EQ(L'\0', buf[0]);
}

// 0 (ERROR_SUCCESS) を返すと、上流が未設定のバッファを読んでしまう
TEST(WinportRegistry, ReportsMissingKey)
{
	HKEY key = reinterpret_cast<HKEY>(1);
	EXPECT_NE(ERROR_SUCCESS, ::RegOpenKeyExW(HKEY_CLASSES_ROOT, L".txt", 0, KEY_READ, &key));
	EXPECT_EQ(nullptr, key);
	DWORD type = 0, size = 0;
	EXPECT_NE(ERROR_SUCCESS, ::RegQueryValueExW(nullptr, L"x", nullptr, &type, nullptr, &size));
}
