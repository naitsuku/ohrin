/*
	test-winport-oleaut.cpp — BSTR と VARIANT のテスト（mac 版独自）

	SPDX-License-Identifier: Zlib
*/
#include "pch.h"

#include <oleauto.h>

#include <string>

namespace {

struct Var {
	VARIANT v;
	Var() { ::VariantInit(&v); }
	~Var() { ::VariantClear(&v); }
};

void SetString(VARIANT& v, const wchar_t* s)
{
	v.vt = VT_BSTR;
	v.bstrVal = ::SysAllocString(s);
}

std::wstring AsString(VARIANT& src)
{
	Var out;
	EXPECT_EQ(S_OK, ::VariantChangeType(&out.v, &src, 0, VT_BSTR));
	return std::wstring(out.v.bstrVal, ::SysStringLen(out.v.bstrVal));
}

}  // namespace

TEST(WinportOleAut, BstrKeepsEmbeddedNul)
{
	const wchar_t text[] = {L'a', L'\0', L'b'};
	BSTR s = ::SysAllocStringLen(text, 3);
	EXPECT_EQ(3u, ::SysStringLen(s));
	EXPECT_EQ(L'b', s[2]);
	EXPECT_EQ(L'\0', s[3]);
	::SysFreeString(s);

	EXPECT_EQ(0u, ::SysStringLen(nullptr));
	EXPECT_EQ(nullptr, ::SysAllocString(nullptr));
	::SysFreeString(nullptr);
}

TEST(WinportOleAut, BstrReAlloc)
{
	BSTR s = ::SysAllocString(L"short");
	EXPECT_TRUE(::SysReAllocString(&s, L"much longer text"));
	EXPECT_EQ(16u, ::SysStringLen(s));
	::SysFreeString(s);
}

TEST(WinportOleAut, CopyDuplicatesString)
{
	Var a, b;
	SetString(a.v, L"あいう");
	EXPECT_EQ(S_OK, ::VariantCopy(&b.v, &a.v));
	EXPECT_NE(a.v.bstrVal, b.v.bstrVal);
	EXPECT_STREQ(L"あいう", b.v.bstrVal);
}

TEST(WinportOleAut, CopyIndFollowsReference)
{
	Var inner;
	SetString(inner.v, L"x");
	VARIANT ref;
	::VariantInit(&ref);
	ref.vt = VT_BYREF | VT_VARIANT;
	ref.pvarVal = &inner.v;
	Var out;
	EXPECT_EQ(S_OK, ::VariantCopyInd(&out.v, &ref));
	EXPECT_EQ(VT_BSTR, out.v.vt);
	EXPECT_NE(inner.v.bstrVal, out.v.bstrVal);
	EXPECT_STREQ(L"x", out.v.bstrVal);
}

TEST(WinportOleAut, ChangeTypeToString)
{
	Var v;
	v.v.vt = VT_I4;
	v.v.lVal = -12;
	EXPECT_EQ(L"-12", AsString(v.v));
	v.v.vt = VT_R8;
	v.v.dblVal = 1.5;
	EXPECT_EQ(L"1.5", AsString(v.v));
	v.v.vt = VT_BOOL;
	v.v.boolVal = VARIANT_TRUE;
	EXPECT_EQ(L"-1", AsString(v.v));
	v.v.vt = VT_EMPTY;
	EXPECT_EQ(L"", AsString(v.v));
}

TEST(WinportOleAut, ChangeTypeToInteger)
{
	Var v, out;
	SetString(v.v, L" 42 ");
	EXPECT_EQ(S_OK, ::VariantChangeType(&out.v, &v.v, 0, VT_I4));
	EXPECT_EQ(42, out.v.lVal);

	Var r;
	r.v.vt = VT_R8;
	r.v.dblVal = 2.5;	// 最近接偶数
	EXPECT_EQ(S_OK, ::VariantChangeType(&out.v, &r.v, 0, VT_I4));
	EXPECT_EQ(2, out.v.lVal);
	r.v.dblVal = 3e10;
	EXPECT_EQ(DISP_E_OVERFLOW, ::VariantChangeType(&out.v, &r.v, 0, VT_I4));

	Var bad;
	SetString(bad.v, L"abc");
	EXPECT_EQ(DISP_E_TYPEMISMATCH, ::VariantChangeType(&out.v, &bad.v, 0, VT_I4));
}

// 変換元と変換先が同じでも壊れない
TEST(WinportOleAut, ChangeTypeInPlace)
{
	Var v;
	SetString(v.v, L"7");
	EXPECT_EQ(S_OK, ::VariantChangeType(&v.v, &v.v, 0, VT_I4));
	EXPECT_EQ(VT_I4, v.v.vt);
	EXPECT_EQ(7, v.v.lVal);
}
