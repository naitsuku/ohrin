/*
	crt.cpp — MSVC CRT 拡張関数の実装（宣言は winport/include/winport_crt.h）

	wchar_t 列は UTF-16 コード単位を格納している前提。ファイル名など OS へ渡す文字列は
	UTF-8 に変換してから POSIX API を呼ぶ。printf 系は printf.cpp。

	SPDX-License-Identifier: Zlib
*/
#include "winport_internal.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <clocale>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "winport_crt.h"

// winport 自身は libc の swprintf / vswprintf を使う（winport_crt.h の差し替えマクロを解除）
#undef swprintf
#undef vswprintf

using winport::Utf8ToWide;
using winport::WideToUtf8;

namespace {

std::string NarrowPath(const wchar_t* path)
{
	std::string s = WideToUtf8(path ? path : L"");
	// Windows 形式の区切り文字も受け付ける
	for (auto& c : s) {
		if (c == '\\') c = '/';
	}
	return s;
}

template <typename CharT>
CharT FoldAscii(CharT c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<CharT>(c - 'A' + 'a') : c;
}

wint_t FoldWide(wchar_t c)
{
	return std::towlower(static_cast<wint_t>(c));
}

// strcpy_s 系の共通実装
template <typename CharT>
errno_t CopyS(CharT* dst, size_t n, const CharT* src, size_t count)
{
	if (!dst || n == 0) return EINVAL;
	if (!src) { dst[0] = 0; return EINVAL; }
	size_t len = 0;
	while (src[len] && len < count) ++len;
	if (len < n) {
		std::memcpy(dst, src, len * sizeof(CharT));
		dst[len] = 0;
		return 0;
	}
	if (count == _TRUNCATE) {
		std::memcpy(dst, src, (n - 1) * sizeof(CharT));
		dst[n - 1] = 0;
		return STRUNCATE;
	}
	dst[0] = 0;
	return ERANGE;
}

template <typename CharT>
errno_t CatS(CharT* dst, size_t n, const CharT* src, size_t count)
{
	if (!dst || n == 0) return EINVAL;
	size_t cur = 0;
	while (cur < n && dst[cur]) ++cur;
	if (cur == n) { dst[0] = 0; return EINVAL; }
	const errno_t r = CopyS(dst + cur, n - cur, src, count);
	if (r == ERANGE || r == EINVAL) dst[0] = 0;
	return r;
}

}  // namespace

extern "C" {

/* ---- 大文字小文字無視の比較 ---- */
int _memicmp(const void* a, const void* b, size_t n)
{
	auto p = static_cast<const unsigned char*>(a);
	auto q = static_cast<const unsigned char*>(b);
	for (size_t i = 0; i < n; ++i) {
		const int d = FoldAscii(p[i]) - FoldAscii(q[i]);
		if (d) return d;
	}
	return 0;
}

int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }

int _wcsicmp(const wchar_t* a, const wchar_t* b)
{
	for (;; ++a, ++b) {
		const int d = static_cast<int>(FoldWide(*a)) - static_cast<int>(FoldWide(*b));
		if (d || !*a) return d;
	}
}

int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		const int d = static_cast<int>(FoldWide(a[i])) - static_cast<int>(FoldWide(b[i]));
		if (d || !a[i]) return d;
	}
	return 0;
}

wchar_t* _wcslwr(wchar_t* s)
{
	for (auto p = s; *p; ++p) *p = static_cast<wchar_t>(std::towlower(*p));
	return s;
}

wchar_t* _wcsupr(wchar_t* s)
{
	for (auto p = s; *p; ++p) *p = static_cast<wchar_t>(std::towupper(*p));
	return s;
}

wint_t _towupper_l(wint_t c, _locale_t) { return std::towupper(c); }
wint_t _towlower_l(wint_t c, _locale_t) { return std::towlower(c); }

/* ---- セキュア CRT ---- */
errno_t strcpy_s(char* dst, size_t n, const char* src) { return CopyS(dst, n, src, SIZE_MAX - 1); }
errno_t strncpy_s(char* dst, size_t n, const char* src, size_t count) { return CopyS(dst, n, src, count); }
errno_t strcat_s(char* dst, size_t n, const char* src) { return CatS(dst, n, src, SIZE_MAX - 1); }
errno_t wcscpy_s(wchar_t* dst, size_t n, const wchar_t* src) { return CopyS(dst, n, src, SIZE_MAX - 1); }
errno_t wcsncpy_s(wchar_t* dst, size_t n, const wchar_t* src, size_t count) { return CopyS(dst, n, src, count); }
errno_t wcscat_s(wchar_t* dst, size_t n, const wchar_t* src) { return CatS(dst, n, src, SIZE_MAX - 1); }
errno_t wcsncat_s(wchar_t* dst, size_t n, const wchar_t* src, size_t count) { return CatS(dst, n, src, count); }

errno_t memcpy_s(void* dst, size_t n, const void* src, size_t count)
{
	if (count == 0) return 0;
	if (!dst || !src) return EINVAL;
	if (count > n) { std::memset(dst, 0, n); return ERANGE; }
	std::memcpy(dst, src, count);
	return 0;
}

errno_t wmemcpy_s(wchar_t* dst, size_t n, const wchar_t* src, size_t count)
{
	return memcpy_s(dst, n * sizeof(wchar_t), src, count * sizeof(wchar_t));
}

size_t wcsnlen_s(const wchar_t* s, size_t n) { return s ? wcsnlen(s, n) : 0; }

wchar_t* wcstok_s(wchar_t* s, const wchar_t* delim, wchar_t** ctx) { return std::wcstok(s, delim, ctx); }

errno_t _localtime64_s(struct tm* out, const long long* t)
{
	if (!out || !t) return EINVAL;
	const time_t tt = static_cast<time_t>(*t);
	return localtime_r(&tt, out) ? 0 : EINVAL;
}

/* ---- 数値変換 ---- */
int _wtoi(const wchar_t* s) { return static_cast<int>(std::wcstol(s, nullptr, 10)); }
long _wtol(const wchar_t* s) { return std::wcstol(s, nullptr, 10); }

wchar_t* _ultow(unsigned long v, wchar_t* buf, int radix)
{
	wchar_t tmp[72];
	int n = 0;
	do {
		const int d = static_cast<int>(v % radix);
		tmp[n++] = static_cast<wchar_t>(d < 10 ? L'0' + d : L'a' + d - 10);
		v /= radix;
	} while (v);
	for (int i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
	buf[n] = 0;
	return buf;
}

wchar_t* _itow(int v, wchar_t* buf, int radix)
{
	if (radix == 10 && v < 0) {
		buf[0] = L'-';
		_ultow(static_cast<unsigned long>(-static_cast<long long>(v)), buf + 1, radix);
		return buf;
	}
	return _ultow(static_cast<unsigned int>(v), buf, radix);
}

/* ---- 文字列 ---- */
wchar_t* _wcsdup(const wchar_t* s)
{
	if (!s) return nullptr;
	const size_t n = std::wcslen(s) + 1;
	auto p = static_cast<wchar_t*>(std::malloc(n * sizeof(wchar_t)));
	if (p) std::wmemcpy(p, s, n);
	return p;
}

wchar_t* _wcstok(wchar_t* s, const wchar_t* delim)
{
	static thread_local wchar_t* ctx = nullptr;
	return std::wcstok(s, delim, &ctx);
}

/* ---- パス・ファイル ---- */
errno_t _wsplitpath_s(const wchar_t* path, wchar_t* drive, size_t nd, wchar_t* dir, size_t ndir,
                      wchar_t* fname, size_t nf, wchar_t* ext, size_t ne)
{
	if (!path) return EINVAL;
	std::wstring_view p(path);
	std::wstring_view d, di, f, e;
	if (p.size() >= 2 && p[1] == L':') {
		d = p.substr(0, 2);
		p.remove_prefix(2);
	}
	const auto sep = p.find_last_of(L"\\/");
	if (sep != std::wstring_view::npos) {
		di = p.substr(0, sep + 1);
		p.remove_prefix(sep + 1);
	}
	const auto dot = p.find_last_of(L'.');
	if (dot != std::wstring_view::npos) {
		f = p.substr(0, dot);
		e = p.substr(dot);
	} else {
		f = p;
	}
	auto put = [](wchar_t* dst, size_t n, std::wstring_view v) -> errno_t {
		if (!dst) return 0;
		if (v.size() >= n) { if (n) dst[0] = 0; return ERANGE; }
		std::wmemcpy(dst, v.data(), v.size());
		dst[v.size()] = 0;
		return 0;
	};
	errno_t r = 0;
	if (auto x = put(drive, nd, d)) r = x;
	if (auto x = put(dir, ndir, di)) r = x;
	if (auto x = put(fname, nf, f)) r = x;
	if (auto x = put(ext, ne, e)) r = x;
	return r;
}

void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
	wchar_t d[_MAX_DRIVE], di[_MAX_DIR], f[_MAX_FNAME], e[_MAX_EXT];
	const std::wstring wp = Utf8ToWide(path ? path : "");
	_wsplitpath_s(wp.c_str(), d, _MAX_DRIVE, di, _MAX_DIR, f, _MAX_FNAME, e, _MAX_EXT);
	if (drive) std::strcpy(drive, WideToUtf8(d).c_str());
	if (dir) std::strcpy(dir, WideToUtf8(di).c_str());
	if (fname) std::strcpy(fname, WideToUtf8(f).c_str());
	if (ext) std::strcpy(ext, WideToUtf8(e).c_str());
}

void _wmakepath(wchar_t* path, const wchar_t* drive, const wchar_t* dir, const wchar_t* fname, const wchar_t* ext)
{
	std::wstring s;
	if (drive && *drive) s += drive;
	if (dir && *dir) {
		s += dir;
		if (s.back() != L'\\' && s.back() != L'/') s += L'/';
	}
	if (fname) s += fname;
	if (ext && *ext) {
		if (*ext != L'.') s += L'.';
		s += ext;
	}
	std::wmemcpy(path, s.c_str(), s.size() + 1);
}

wchar_t* _wfullpath(wchar_t* abs, const wchar_t* rel, size_t n)
{
	std::error_code ec;
	const auto full = std::filesystem::absolute(std::filesystem::path(NarrowPath(rel)), ec).lexically_normal();
	if (ec) return nullptr;
	const std::wstring w = Utf8ToWide(full.string());
	if (!abs) return _wcsdup(w.c_str());
	if (w.size() >= n) return nullptr;
	std::wmemcpy(abs, w.c_str(), w.size() + 1);
	return abs;
}

FILE* _wfopen(const wchar_t* path, const wchar_t* mode)
{
	// MSVC 固有の "t" と ",ccs=..." は落とす
	std::string m;
	for (auto p = mode; p && *p && *p != L','; ++p) {
		if (*p != L't' && *p != L' ') m += static_cast<char>(*p);
	}
	return std::fopen(NarrowPath(path).c_str(), m.c_str());
}

int _wunlink(const wchar_t* path) { return ::unlink(NarrowPath(path).c_str()); }
int _waccess(const wchar_t* path, int mode) { return ::access(NarrowPath(path).c_str(), mode & 06); }
errno_t _waccess_s(const wchar_t* path, int mode) { return _waccess(path, mode) == 0 ? 0 : errno; }

wchar_t* _wtempnam(const wchar_t* dir, const wchar_t* prefix)
{
	static std::mt19937 rng{std::random_device{}()};
	std::wstring base = (dir && *dir) ? dir : Utf8ToWide(std::filesystem::temp_directory_path().string());
	if (base.back() != L'/' && base.back() != L'\\') base += L'/';
	for (int i = 0; i < 100; ++i) {
		wchar_t num[16];
		_ultow(rng() & 0xFFFFFF, num, 16);
		std::wstring cand = base + (prefix ? prefix : L"") + num;
		if (_waccess(cand.c_str(), 0) != 0) return _wcsdup(cand.c_str());
	}
	return nullptr;
}

wchar_t* _wgetenv(const wchar_t* name)
{
	static auto& mtx = *new std::mutex;
	static auto& cache = *new std::map<std::wstring, std::wstring>;
	const char* v = std::getenv(WideToUtf8(name).c_str());
	if (!v) return nullptr;
	std::lock_guard lock(mtx);
	auto& slot = cache[name];
	slot = Utf8ToWide(v);
	return slot.data();
}

int _fileno(FILE* fp) { return fileno(fp); }

long _filelength(int fd)
{
	struct stat st {};
	return fstat(fd, &st) == 0 ? static_cast<long>(st.st_size) : -1;
}

/* ---- ロケール・時刻 ---- */
_locale_t _create_locale(int, const char*)
{
	// sakura_core はロケール付き関数（_towupper_l 等）に渡すだけなので、非 null のダミーを返す
	static int dummy;
	return reinterpret_cast<_locale_t>(&dummy);
}

void _free_locale(_locale_t) {}

wchar_t* _wsetlocale(int category, const wchar_t* name)
{
	static thread_local std::wstring result;
	const char* r = std::setlocale(category, name ? WideToUtf8(name).c_str() : nullptr);
	if (!r) return nullptr;
	result = Utf8ToWide(r);
	return result.data();
}

wchar_t* _wstrdate(wchar_t* buf)
{
	const time_t t = std::time(nullptr);
	struct tm tmv {};
	localtime_r(&t, &tmv);
	std::swprintf(buf, 9, L"%02d/%02d/%02d", tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_year % 100);
	return buf;
}

long long _time64(long long* t)
{
	const long long now = static_cast<long long>(std::time(nullptr));
	if (t) *t = now;
	return now;
}

void _tzset(void) { tzset(); }

/* ---- スレッド ---- */
uintptr_t winport_beginthreadex(void*, unsigned, unsigned (*)(void*), void*, unsigned, unsigned*);  // process.cpp
uintptr_t _beginthreadex(void* sec, unsigned stack, unsigned (*start)(void*), void* arg, unsigned flags, unsigned* tid)
{
	// 待機・CloseHandle できるスレッドハンドルを返す
	return winport_beginthreadex(sec, stack, start, arg, flags, tid);
}

/* ---- その他 ---- */
unsigned short _byteswap_ushort(unsigned short v) { return static_cast<unsigned short>((v << 8) | (v >> 8)); }

}  // extern "C"

/* ---- <mbstring.h>: JIS ⇔ Shift_JIS（算術変換。範囲外は 0 を返す MSVC と同じ挙動） ---- */
extern "C" {

unsigned int _mbcjistojms(unsigned int c)
{
	unsigned int hi = (c >> 8) & 0xFF, lo = c & 0xFF;
	if (hi < 0x21 || hi > 0x7E || lo < 0x21 || lo > 0x7E) return 0;
	if (hi & 1) {
		lo += (lo < 0x60) ? 0x1F : 0x20;
	} else {
		lo += 0x7E;
	}
	hi = ((hi - 0x21) >> 1) + 0x81;
	if (hi > 0x9F) hi += 0x40;
	return (hi << 8) | lo;
}

unsigned int _mbcjmstojis(unsigned int c)
{
	unsigned int hi = (c >> 8) & 0xFF, lo = c & 0xFF;
	const bool leadOk = (0x81 <= hi && hi <= 0x9F) || (0xE0 <= hi && hi <= 0xEF);
	if (!leadOk || lo < 0x40 || lo == 0x7F || lo > 0xFC) return 0;
	hi -= (hi > 0x9F) ? 0xB1 : 0x71;
	hi = hi * 2 + 1;
	if (lo > 0x7F) --lo;
	if (lo >= 0x9E) {
		lo -= 0x7D;
		++hi;
	} else {
		lo -= 0x1F;
	}
	return (hi << 8) | lo;
}

unsigned int _mbcjistojms_l(unsigned int c, _locale_t) { return _mbcjistojms(c); }
unsigned int _mbcjmstojis_l(unsigned int c, _locale_t) { return _mbcjmstojis(c); }

}  // extern "C"
