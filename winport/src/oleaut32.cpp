/*
	oleaut32.cpp — BSTR と VARIANT

	マクロの引数と戻り値はすべて VARIANT を通る（CMacro::HandleFunction）。
	BSTR の中身は他の文字列と同じく UTF-16 単位を 1 要素ずつ入れた wchar_t 列。
	先頭に Win32 と同じ長さの欄を置くが、単位は要素数ではなくバイト数（要素数 × sizeof(OLECHAR)）。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>
#include <oleauto.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

struct BstrHeader {
	UINT bytes;
};

BstrHeader* HeaderOf(BSTR s) { return reinterpret_cast<BstrHeader*>(s) - 1; }

HRESULT ToBstr(const VARIANT& v, BSTR* out)
{
	std::wstring s;
	switch (v.vt) {
	case VT_EMPTY: break;
	case VT_BSTR: *out = SysAllocStringLen(v.bstrVal, SysStringLen(v.bstrVal)); return S_OK;
	case VT_I2: s = std::to_wstring(v.iVal); break;
	case VT_I4: s = std::to_wstring(v.lVal); break;
	case VT_UI1: s = std::to_wstring(v.bVal); break;
	case VT_UI2: s = std::to_wstring(v.uiVal); break;
	case VT_UI4: s = std::to_wstring(v.ulVal); break;
	case VT_BOOL: s = v.boolVal ? L"-1" : L"0"; break;	// VARIANT_ALPHABOOL なしの Win32 の動作
	case VT_R8: {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.15g", v.dblVal);
		for (const char* p = buf; *p; ++p) s += static_cast<wchar_t>(*p);
		break;
	}
	default:
		return DISP_E_TYPEMISMATCH;
	}
	*out = SysAllocStringLen(s.data(), static_cast<UINT>(s.size()));
	return S_OK;
}

//! 整数への変換の共通部分。小数は Win32 と同じく最近接偶数に丸める
HRESULT ToDouble(const VARIANT& v, double* out)
{
	switch (v.vt) {
	case VT_EMPTY: *out = 0; return S_OK;
	case VT_I2: *out = v.iVal; return S_OK;
	case VT_I4: *out = v.lVal; return S_OK;
	case VT_UI1: *out = v.bVal; return S_OK;
	case VT_UI2: *out = v.uiVal; return S_OK;
	case VT_UI4: *out = v.ulVal; return S_OK;
	case VT_BOOL: *out = v.boolVal ? -1 : 0; return S_OK;
	case VT_R8: *out = v.dblVal; return S_OK;
	case VT_BSTR: {
		if (!v.bstrVal) return DISP_E_TYPEMISMATCH;
		wchar_t* end = nullptr;
		const double d = std::wcstod(v.bstrVal, &end);
		if (end == v.bstrVal) return DISP_E_TYPEMISMATCH;
		while (*end == L' ' || *end == L'\t') ++end;
		if (*end) return DISP_E_TYPEMISMATCH;
		*out = d;
		return S_OK;
	}
	default:
		return DISP_E_TYPEMISMATCH;
	}
}

HRESULT ToInteger(const VARIANT& v, double low, double high, double* out)
{
	double d;
	const HRESULT hr = ToDouble(v, &d);
	if (FAILED(hr)) return hr;
	d = std::nearbyint(d);
	if (d < low || high < d) return DISP_E_OVERFLOW;
	*out = d;
	return S_OK;
}

}  // namespace

/* ---- BSTR ---- */
BSTR WINAPI SysAllocStringLen(const OLECHAR* s, UINT len)
{
	auto* header = static_cast<BstrHeader*>(std::malloc(sizeof(BstrHeader) + (static_cast<size_t>(len) + 1) * sizeof(OLECHAR)));
	if (!header) return nullptr;
	header->bytes = len * sizeof(OLECHAR);
	BSTR result = reinterpret_cast<BSTR>(header + 1);
	if (s) std::wmemcpy(result, s, len);
	result[len] = L'\0';
	return result;
}

BSTR WINAPI SysAllocString(const OLECHAR* s)
{
	return s ? SysAllocStringLen(s, static_cast<UINT>(std::wcslen(s))) : nullptr;
}

void WINAPI SysFreeString(BSTR s)
{
	if (s) std::free(HeaderOf(s));
}

UINT WINAPI SysStringLen(BSTR s)
{
	return s ? HeaderOf(s)->bytes / sizeof(OLECHAR) : 0;
}

INT WINAPI SysReAllocString(BSTR* dest, const OLECHAR* s)
{
	BSTR next = SysAllocString(s);
	if (s && !next) return FALSE;
	SysFreeString(*dest);
	*dest = next;
	return TRUE;
}

/* ---- VARIANT ---- */
void WINAPI VariantInit(VARIANTARG* v)
{
	std::memset(v, 0, sizeof(*v));
	v->vt = VT_EMPTY;
}

HRESULT WINAPI VariantClear(VARIANTARG* v)
{
	if (!(v->vt & VT_BYREF)) {
		switch (v->vt) {
		case VT_BSTR: SysFreeString(v->bstrVal); break;
		case VT_DISPATCH:
		case VT_UNKNOWN:
			if (v->punkVal) v->punkVal->Release();
			break;
		default: break;
		}
	}
	VariantInit(v);
	return S_OK;
}

HRESULT WINAPI VariantCopy(VARIANTARG* dest, VARIANTARG* src)
{
	if (dest == src) return S_OK;
	VariantClear(dest);
	*dest = *src;
	if (!(src->vt & VT_BYREF)) {
		if (src->vt == VT_BSTR) {
			dest->bstrVal = SysAllocStringLen(src->bstrVal, SysStringLen(src->bstrVal));
		} else if ((src->vt == VT_DISPATCH || src->vt == VT_UNKNOWN) && src->punkVal) {
			src->punkVal->AddRef();
		}
	}
	return S_OK;
}

HRESULT WINAPI VariantCopyInd(VARIANT* dest, VARIANTARG* src)
{
	if (!(src->vt & VT_BYREF)) return VariantCopy(dest, src);
	if (src->vt == (VT_BYREF | VT_VARIANT)) return VariantCopyInd(dest, src->pvarVal);
	VARIANT value;
	VariantInit(&value);
	value.vt = src->vt & ~VT_BYREF;
	switch (value.vt) {
	case VT_I2: value.iVal = *src->piVal; break;
	case VT_I4: value.lVal = *src->plVal; break;
	case VT_R8: value.dblVal = *src->pdblVal; break;
	case VT_BOOL: value.boolVal = *src->pboolVal; break;
	case VT_BSTR: value.bstrVal = *src->pbstrVal; break;
	default: return DISP_E_BADVARTYPE;
	}
	return VariantCopy(dest, &value);	// value の BSTR は借り物。コピーするので解放しない
}

HRESULT WINAPI VariantChangeType(VARIANTARG* dest, VARIANTARG* src, USHORT, VARTYPE vt)
{
	VARIANT from;
	VariantInit(&from);
	HRESULT hr = VariantCopyInd(&from, src);
	if (FAILED(hr)) return hr;

	VARIANT to;
	VariantInit(&to);
	double d = 0;
	switch (vt) {
	case VT_BSTR:
		hr = ToBstr(from, &to.bstrVal);
		break;
	case VT_I4:
		hr = ToInteger(from, INT_MIN, INT_MAX, &d);
		to.lVal = static_cast<LONG>(d);
		break;
	case VT_I2:
		hr = ToInteger(from, SHRT_MIN, SHRT_MAX, &d);
		to.iVal = static_cast<SHORT>(d);
		break;
	case VT_R8:
		hr = ToDouble(from, &to.dblVal);
		break;
	case VT_BOOL:
		hr = ToDouble(from, &d);
		to.boolVal = d != 0 ? VARIANT_TRUE : VARIANT_FALSE;
		break;
	default:
		hr = DISP_E_TYPEMISMATCH;
		break;
	}
	VariantClear(&from);
	if (FAILED(hr)) return hr;
	to.vt = vt;
	VariantClear(dest);
	*dest = to;
	return S_OK;
}
