/*
	bregonig_bridge.cpp — 組み込みの bregonig.dll

	Windows の上流は bregonig.dll を LoadLibrary して BMatchW などを GetProcAddress する。mac では同じ
	ソース（externals/bregonig, externals/Onigmo）を静的にリンクし、LoadLibrary("bregonig.dll") に応えて渡す。

	bregonig の wchar_t は 2 バイト（winport_regex は -fshort-wchar）で、上流の wchar_t は 4 バイトなので、
	呼び出しのたびに文字列を詰め替える。どちらも 1 要素 = 1 UTF-16 単位なので、位置はそのまま対応する。

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>

#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

#include "winport_internal.h"

namespace {

//! bregonig 側の BREGEXP
struct Regexp16 {
	const char16_t* outp;
	const char16_t* outendp;
	int splitctr;
	const char16_t** splitp;
	INT_PTR rsv1;
	char16_t* parap;
	char16_t* paraendp;
	char16_t* transtblp;
	char16_t** startp;
	char16_t** endp;
	int nparens;
};

//! 上流側の BREGEXP_W（sakura_core/extmodule/CBregexpDll2.h）
struct RegexpW {
	const wchar_t* outp;
	const wchar_t* outendp;
	int splitctr;
	const wchar_t** splitp;
	int rsv1;
	wchar_t* parap;
	wchar_t* paraendp;
	wchar_t* transtblp;
	wchar_t** startp;
	wchar_t** endp;
	int nparens;
};

constexpr size_t kMessageSize = 80;	// BREGEXP_MAX_ERROR_MESSAGE_LEN

}  // namespace

extern "C" {
int BMatchW(char16_t* str, char16_t* target, char16_t* targetendp, Regexp16** rxp, char16_t* msg);
int BSubstW(char16_t* str, char16_t* target, char16_t* targetendp, Regexp16** rxp, char16_t* msg);
int BMatchExW(char16_t* str, char16_t* targetbeg, char16_t* target, char16_t* targetendp, Regexp16** rxp, char16_t* msg);
int BSubstExW(char16_t* str, char16_t* targetbeg, char16_t* target, char16_t* targetendp, Regexp16** rxp, char16_t* msg);
int BTransW(char16_t* str, char16_t* target, char16_t* targetendp, Regexp16** rxp, char16_t* msg);
void BRegfreeW(Regexp16* rx);
char16_t* BRegexpVersionW(void);
}

//! bregonig.dll の初期化（Onigmo の初期化と構文の拡張）。DLL ではないので読み込み時に自分で呼ぶ
int DllMain(void* instance, unsigned int reason, void* reserved);

namespace {

//! 上流に渡す RegexpW と、bregonig 側の実体。public は先頭に置く（上流は RegexpW* として持つ）
struct Bridge {
	RegexpW pub{};
	Regexp16* inner = nullptr;
	std::u16string target;	// bregonig の startp / endp はこの中を指す
	const wchar_t* base = nullptr;
	std::vector<wchar_t*> starts;
	std::vector<wchar_t*> ends;
	std::wstring out;
};

std::u16string Narrow(const wchar_t* begin, const wchar_t* end)
{
	std::u16string s(static_cast<size_t>(end - begin), u'\0');
	for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char16_t>(begin[i]);
	return s;
}

void CopyMessage(const char16_t* from, wchar_t* to)
{
	if (!to) return;
	size_t i = 0;
	for (; i + 1 < kMessageSize && from[i]; ++i) to[i] = from[i];
	to[i] = L'\0';
}

//! bregonig の結果を上流から見える形に写す
void Publish(Bridge* b)
{
	const Regexp16* r = b->inner;
	const size_t groups = static_cast<size_t>(r->nparens) + 1;
	b->starts.assign(groups, nullptr);
	b->ends.assign(groups, nullptr);
	const char16_t* origin = b->target.data();
	if (r->startp && r->endp) {
		for (size_t i = 0; i < groups; ++i) {
			// 一致しなかったグループは nullptr
			if (r->startp[i]) b->starts[i] = const_cast<wchar_t*>(b->base) + (r->startp[i] - origin);
			if (r->endp[i]) b->ends[i] = const_cast<wchar_t*>(b->base) + (r->endp[i] - origin);
		}
	}
	b->pub.startp = b->starts.data();
	b->pub.endp = b->ends.data();
	b->pub.nparens = r->nparens;
	if (r->outp) {
		b->out.resize(static_cast<size_t>(r->outendp - r->outp));
		for (size_t i = 0; i < b->out.size(); ++i) b->out[i] = r->outp[i];
		b->pub.outp = b->out.c_str();
		b->pub.outendp = b->pub.outp + b->out.size();
	} else {
		b->pub.outp = nullptr;
		b->pub.outendp = nullptr;
	}
}

//! 共通処理。call は (パターン, 先頭, 開始位置, 終端, 実体, メッセージ) で bregonig を呼ぶ
template <class Call>
int Run(const wchar_t* str, const wchar_t* begin, const wchar_t* start, const wchar_t* end, RegexpW** rxp, wchar_t* msg, Call call)
{
	if (!rxp) return -1;
	auto* bridge = reinterpret_cast<Bridge*>(*rxp);
	const bool created = !bridge;
	if (created) bridge = new Bridge;

	std::u16string pattern;
	if (str) pattern = Narrow(str, str + std::wcslen(str));
	bridge->base = begin;
	bridge->target = begin && end ? Narrow(begin, end) : std::u16string();
	char16_t* origin = bridge->target.data();
	char16_t message[kMessageSize] = {};
	const int result = call(str ? pattern.data() : nullptr, begin ? origin : nullptr, begin ? origin + (start - begin) : nullptr,
		begin ? origin + (end - begin) : nullptr, &bridge->inner, message);
	CopyMessage(message, msg);

	if (bridge->inner) {
		Publish(bridge);
		*rxp = &bridge->pub;
	} else {
		delete bridge;	// 正規表現の誤りなどで実体ができなかった
		*rxp = nullptr;
	}
	return result;
}

int Match(const wchar_t* str, const wchar_t* target, const wchar_t* targetendp, RegexpW** rxp, wchar_t* msg)
{
	return Run(str, target, target, targetendp, rxp, msg, [](char16_t* s, char16_t* b, char16_t*, char16_t* e, Regexp16** r, char16_t* m) {
		return BMatchW(s, b, e, r, m);
	});
}

int Subst(const wchar_t* str, const wchar_t* target, const wchar_t* targetendp, RegexpW** rxp, wchar_t* msg)
{
	return Run(str, target, target, targetendp, rxp, msg, [](char16_t* s, char16_t* b, char16_t*, char16_t* e, Regexp16** r, char16_t* m) {
		return BSubstW(s, b, e, r, m);
	});
}

int MatchEx(const wchar_t* str, const wchar_t* targetbeg, const wchar_t* target, const wchar_t* targetendp, RegexpW** rxp, wchar_t* msg)
{
	return Run(str, targetbeg, target, targetendp, rxp, msg, BMatchExW);
}

int SubstEx(const wchar_t* str, const wchar_t* targetbeg, const wchar_t* target, const wchar_t* targetendp, RegexpW** rxp, wchar_t* msg)
{
	return Run(str, targetbeg, target, targetendp, rxp, msg, BSubstExW);
}

int Trans(const wchar_t* str, wchar_t* target, wchar_t* targetendp, RegexpW** rxp, wchar_t* msg)
{
	return Run(str, target, target, targetendp, rxp, msg, [](char16_t* s, char16_t* b, char16_t*, char16_t* e, Regexp16** r, char16_t* m) {
		return BTransW(s, b, e, r, m);
	});
}

//! 上流は読み込むだけで呼ばない
int Split(const wchar_t*, wchar_t*, wchar_t*, int, RegexpW**, wchar_t* msg)
{
	CopyMessage(u"BSplit is not supported", msg);
	return -1;
}

void Free(RegexpW* rx)
{
	if (!rx) return;
	auto* bridge = reinterpret_cast<Bridge*>(rx);
	if (bridge->inner) BRegfreeW(bridge->inner);
	delete bridge;
}

const wchar_t* Version()
{
	static const std::wstring version = [] {
		const char16_t* s = BRegexpVersionW();
		return std::wstring(s, s + std::char_traits<char16_t>::length(s));
	}();
	return version.c_str();
}

char g_module;

}  // namespace

namespace winport {

void* LoadBuiltinModule(const wchar_t* path)
{
	if (!path) return nullptr;
	const wchar_t* name = path;
	for (const wchar_t* p = path; *p; ++p) {
		if (*p == L'/' || *p == L'\\') name = p + 1;
	}
	if (_wcsicmp(name, L"bregonig.dll") != 0) return nullptr;
	static std::once_flag once;
	std::call_once(once, [] { DllMain(nullptr, 1 /* DLL_PROCESS_ATTACH */, nullptr); });
	return &g_module;
}

void* BuiltinProcAddress(void* module, const char* name)
{
	if (module != &g_module) return nullptr;
	const struct { const char* name; void* proc; } table[] = {
		{"BMatchW", reinterpret_cast<void*>(Match)},
		{"BSubstW", reinterpret_cast<void*>(Subst)},
		{"BTransW", reinterpret_cast<void*>(Trans)},
		{"BSplitW", reinterpret_cast<void*>(Split)},
		{"BRegfreeW", reinterpret_cast<void*>(Free)},
		{"BRegexpVersionW", reinterpret_cast<void*>(Version)},
		{"BMatchExW", reinterpret_cast<void*>(MatchEx)},
		{"BSubstExW", reinterpret_cast<void*>(SubstEx)},
	};
	for (const auto& entry : table) {
		if (std::strcmp(entry.name, name) == 0) return entry.proc;
	}
	return nullptr;
}

}  // namespace winport
