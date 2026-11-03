/*
	winport_internal.h — winport 実装ファイル共通の内部ヘッダ

	Windows ヘッダは含めない（generated_stubs.cpp が引数なしのスタブを定義するため）。
	Win32 型が必要な実装ファイルは自分で <windows.h> を include すること。

	SPDX-License-Identifier: Zlib
*/
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace winport {

//! 未実装 API が呼ばれたことを報告する（同じ名前は 1 回だけ stderr に出す）
void ReportUnimplemented(const char* dll, const char* name);

//! UTF-16 単位を格納した wchar_t 列 ⇔ UTF-8
std::string WideToUtf8(std::wstring_view ws);
std::wstring Utf8ToWide(std::string_view s);

//! Win32 API に渡されたパスを POSIX パス（UTF-8、区切りは '/'）にする。'\\' も区切りとして扱う
std::string ToNativePath(const wchar_t* path);

//! errno を Win32 のエラーコードに変換して SetLastError する
void SetLastErrorFromErrno(int err);

//! カーネルオブジェクト（HANDLE の実体）。参照カウントで寿命を管理する
struct KernelObject {
	virtual ~KernelObject() = default;
	//! WaitForSingleObject 用。timeoutMs は INFINITE 可。シグナルされたら true
	virtual bool Wait(unsigned long timeoutMs);
	int refs = 1;
};

//! HANDLE を KernelObject として取り出す（擬似ハンドル・無効値は nullptr）
KernelObject* ObjectFromHandle(void* h);

}  // namespace winport
