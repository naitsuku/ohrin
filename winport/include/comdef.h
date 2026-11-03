/*
	winport/include/comdef.h — MSVC <comdef.h> の最小代替

	mingw-w64 の comutil.h は LONG が int になる LP64 環境で
	_variant_t のオーバーロードが衝突するため使わない。
	sakura_core が使う _bstr_t / _variant_t / _com_error だけを最小実装する。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <ole2.h>
#include <oleauto.h>
#include <string>

class _bstr_t {
public:
	_bstr_t() = default;
	_bstr_t(const wchar_t* s) : m_s(s ? s : L"") {}
	_bstr_t(const _bstr_t&) = default;
	_bstr_t& operator=(const _bstr_t&) = default;
	_bstr_t& operator=(const wchar_t* s) { m_s = s ? s : L""; return *this; }
	operator const wchar_t*() const { return m_s.c_str(); }
	BSTR copy() const { return SysAllocString(m_s.c_str()); }
	unsigned int length() const { return static_cast<unsigned int>(m_s.size()); }
	// TODO(winport): BSTR 所有権の受け渡し（Attach/GetAddress）は COM 実装時に整備
	BSTR* GetAddress() { return &m_raw; }
private:
	std::wstring m_s;
	BSTR m_raw = nullptr;
};

class _variant_t : public tagVARIANT {
public:
	_variant_t() { VariantInit(this); }
};

class _com_error {
public:
	explicit _com_error(HRESULT hr) : m_hr(hr) {}
	HRESULT Error() const { return m_hr; }
private:
	HRESULT m_hr;
};

namespace _com_util {
inline void CheckError(HRESULT hr) { if (FAILED(hr)) throw _com_error(hr); }
}
