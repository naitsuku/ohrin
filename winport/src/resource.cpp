/*
	resource.cpp — FindResourceW / LoadResource / LockResource / SizeofResource / LoadStringW

	リソースは rc2winport.py がビルド時に生成した表（g_winportResources）から引く。
	HRSRC は表の要素へのポインタ、HGLOBAL はデータへのポインタそのもの。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>

#include <cwchar>

#include "winport_resource.h"

namespace {

const winport::ResourceEntry* Find(LPCWSTR name, LPCWSTR type)
{
	// 名前付き（文字列）リソースは未対応。上流は数値 ID しか使わない。
	if (!IS_INTRESOURCE(name) || !IS_INTRESOURCE(type)) return nullptr;
	const auto id = static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(name));
	const auto rt = static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(type));
	for (size_t i = 0; i < g_winportResources.count; ++i) {
		const auto& e = g_winportResources.entries[i];
		if (e.type == rt && e.id == id) return &e;
	}
	return nullptr;
}

}  // namespace

extern "C" {

HRSRC WINAPI FindResourceW(HMODULE, LPCWSTR lpName, LPCWSTR lpType)
{
	auto e = Find(lpName, lpType);
	if (!e) ::SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
	return reinterpret_cast<HRSRC>(const_cast<winport::ResourceEntry*>(e));
}

HRSRC WINAPI FindResourceExW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD)
{
	return FindResourceW(hModule, lpName, lpType);
}

HGLOBAL WINAPI LoadResource(HMODULE, HRSRC hResInfo)
{
	if (!hResInfo) return nullptr;
	return const_cast<void*>(reinterpret_cast<const winport::ResourceEntry*>(hResInfo)->data);
}

LPVOID WINAPI LockResource(HGLOBAL hResData) { return hResData; }

DWORD WINAPI SizeofResource(HMODULE, HRSRC hResInfo)
{
	return hResInfo ? static_cast<DWORD>(reinterpret_cast<const winport::ResourceEntry*>(hResInfo)->size) : 0;
}

WINBOOL WINAPI FreeResource(HGLOBAL) { return FALSE; }

int WINAPI LoadStringW(HINSTANCE, UINT uID, LPWSTR lpBuffer, int cchBufferMax)
{
	auto e = Find(MAKEINTRESOURCEW((uID >> 4) + 1), RT_STRING);
	if (!e || !lpBuffer) return 0;
	auto p = static_cast<const WCHAR*>(e->data);
	for (UINT i = 0; i < (uID & 0xF); ++i) p += 1 + *p;
	const int len = static_cast<int>(*p);
	if (cchBufferMax == 0) {
		// 読み取り専用の文字列本体へのポインタを返す（Windows と同じ）
		*reinterpret_cast<const WCHAR**>(lpBuffer) = p + 1;
		return len;
	}
	const int n = len < cchBufferMax - 1 ? len : cchBufferMax - 1;
	std::wmemcpy(lpBuffer, p + 1, n);
	lpBuffer[n] = 0;
	return n;
}

}  // extern "C"
