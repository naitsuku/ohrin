/*
	winport_resource.h — rc2winport.py が生成するリソース表の型

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <cstddef>

namespace winport {

struct ResourceEntry {
	unsigned type;       //!< RT_STRING 等（数値 ID のみ対応）
	unsigned id;         //!< リソース ID（RT_STRING ではブロック番号）
	const void* data;    //!< Windows と同じバイナリ形式（文字列要素は WCHAR）
	size_t size;         //!< バイト数
};

struct ResourceTable {
	const ResourceEntry* entries;
	size_t count;
};

}  // namespace winport

//! メインモジュールのリソース表（アプリ／テスト側で rc2winport.py の生成物をリンクする）
extern const winport::ResourceTable g_winportResources;
