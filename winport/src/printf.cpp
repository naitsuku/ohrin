/*
	printf.cpp — MSVC 書式の printf 系（自前の書式化エンジン）

	macOS の vswprintf は出力をロケールのマルチバイト表現経由で扱うため、C ロケールでは
	日本語を含む書式で失敗し、UTF-16 コード単位（孤立サロゲートを含み得る）も通らない。
	そこで書式解釈は自前で行い、数値の整形だけ libc の snprintf に任せる。

	MSVC 書式の要点:
	- ワイド版の "%s" "%c" はワイド文字列・ワイド文字、"%S" "%C" は narrow（narrow 版は逆）
	- "%hs" は常に narrow、"%ls" "%ws" は常にワイド
	- 長さ修飾子 I64 / I32 / I（= size_t）
	- narrow ⇔ ワイドの変換は CP_ACP（winport では CP932）

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>

#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "winport_crt.h"

// winport 自身は libc の vswscanf を使う（winport_crt.h の差し替えマクロを解除）
#undef swprintf
#undef vswprintf

namespace {

enum class Len { none, hh, h, l, ll, z, t, j, L, i32 };

std::wstring AcpToWide(const char* s, int n)
{
	if (n == 0) return {};
	const int len = MultiByteToWideChar(CP_ACP, 0, s, n, nullptr, 0);
	std::wstring w(len, L'\0');
	MultiByteToWideChar(CP_ACP, 0, s, n, w.data(), len);
	return w;
}

std::string WideToAcp(const wchar_t* s, int n)
{
	if (n == 0) return {};
	const int len = WideCharToMultiByte(CP_ACP, 0, s, n, nullptr, 0, nullptr, nullptr);
	std::string a(len, '\0');
	WideCharToMultiByte(CP_ACP, 0, s, n, a.data(), len, nullptr, nullptr);
	return a;
}

template <typename CharT>
void AppendAscii(std::basic_string<CharT>& out, const char* s)
{
	for (; *s; ++s) out += static_cast<CharT>(static_cast<unsigned char>(*s));
}

template <typename CharT>
void Pad(std::basic_string<CharT>& out, const std::basic_string<CharT>& body, int width, bool left)
{
	const int padLen = width > static_cast<int>(body.size()) ? width - static_cast<int>(body.size()) : 0;
	if (!left) out.append(padLen, CharT(' '));
	out += body;
	if (left) out.append(padLen, CharT(' '));
}

// 書式化の本体。wideFn: ワイド版関数として解釈するか（%s/%c の意味が変わる）
template <typename CharT>
int FormatImpl(std::basic_string<CharT>& out, const CharT* fmt, va_list ap)
{
	constexpr bool wideFn = sizeof(CharT) > 1;
	for (const CharT* p = fmt; *p; ++p) {
		if (*p != CharT('%')) { out += *p; continue; }
		++p;
		if (*p == CharT('%')) { out += *p; continue; }

		std::string flags;
		while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') flags += static_cast<char>(*p++);
		const bool left = flags.find('-') != std::string::npos;

		int width = -1;
		if (*p == '*') {
			width = va_arg(ap, int);
			++p;
			if (width < 0) { flags += '-'; width = -width; }
		} else if (*p >= '0' && *p <= '9') {
			width = 0;
			while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
		}
		int prec = -1;
		if (*p == '.') {
			++p;
			prec = 0;
			if (*p == '*') { prec = va_arg(ap, int); ++p; }
			else while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
		}

		Len len = Len::none;
		int strWidth = 0;  // -1: narrow を強制, +1: ワイドを強制
		if (p[0] == 'I' && p[1] == '6' && p[2] == '4') { len = Len::ll; p += 3; }
		else if (p[0] == 'I' && p[1] == '3' && p[2] == '2') { len = Len::i32; p += 3; }
		else if (p[0] == 'I') { len = Len::z; ++p; }
		else if (p[0] == 'h' && p[1] == 'h') { len = Len::hh; p += 2; }
		else if (p[0] == 'h') { len = Len::h; strWidth = -1; ++p; }
		else if (p[0] == 'l' && p[1] == 'l') { len = Len::ll; p += 2; }
		else if (p[0] == 'l' || p[0] == 'w') { len = Len::l; strWidth = +1; ++p; }
		else if (p[0] == 'z') { len = Len::z; ++p; }
		else if (p[0] == 't') { len = Len::t; ++p; }
		else if (p[0] == 'j') { len = Len::j; ++p; }
		else if (p[0] == 'L') { len = Len::L; ++p; }
		if (!*p) break;
		const char conv = static_cast<char>(*p);

		// 数値用の narrow 書式（"%" + flags + width + precision + 長さ + 変換）
		auto spec = [&](const char* lenMod) {
			std::string s = "%" + flags;
			if (width >= 0) s += std::to_string(width);
			if (prec >= 0) s += "." + std::to_string(prec);
			s += lenMod;
			s += conv;
			return s;
		};
		char num[512];

		switch (conv) {
		case 'd': case 'i': {
			long long v;
			switch (len) {
			case Len::l: v = va_arg(ap, long); break;
			case Len::ll: v = va_arg(ap, long long); break;
			case Len::z: case Len::t: v = va_arg(ap, ptrdiff_t); break;
			case Len::j: v = va_arg(ap, intmax_t); break;
			case Len::hh: v = static_cast<signed char>(va_arg(ap, int)); break;
			case Len::h: v = static_cast<short>(va_arg(ap, int)); break;
			default: v = va_arg(ap, int); break;
			}
			std::snprintf(num, sizeof(num), spec("ll").c_str(), v);
			AppendAscii(out, num);
			break;
		}
		case 'u': case 'o': case 'x': case 'X': {
			unsigned long long v;
			switch (len) {
			case Len::l: v = va_arg(ap, unsigned long); break;
			case Len::ll: v = va_arg(ap, unsigned long long); break;
			case Len::z: case Len::t: v = va_arg(ap, size_t); break;
			case Len::j: v = va_arg(ap, uintmax_t); break;
			case Len::hh: v = static_cast<unsigned char>(va_arg(ap, unsigned)); break;
			case Len::h: v = static_cast<unsigned short>(va_arg(ap, unsigned)); break;
			default: v = va_arg(ap, unsigned); break;
			}
			std::snprintf(num, sizeof(num), spec("ll").c_str(), v);
			AppendAscii(out, num);
			break;
		}
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
			if (len == Len::L) {
				const long double v = va_arg(ap, long double);
				std::snprintf(num, sizeof(num), spec("L").c_str(), v);
			} else {
				const double v = va_arg(ap, double);
				std::snprintf(num, sizeof(num), spec("").c_str(), v);
			}
			AppendAscii(out, num);
			break;
		}
		case 'p': {
			// MSVC は 0x を付けず、大文字 16 桁で出す
			const auto v = reinterpret_cast<uintptr_t>(va_arg(ap, void*));
			std::snprintf(num, sizeof(num), "%016llX", static_cast<unsigned long long>(v));
			std::basic_string<CharT> body;
			AppendAscii(body, num);
			Pad(out, body, width, left);
			break;
		}
		case 'n': {
			*va_arg(ap, int*) = static_cast<int>(out.size());
			break;
		}
		case 'c': case 'C': {
			const bool wantWide = strWidth ? strWidth > 0 : ((conv == 'c') == wideFn);
			const int v = va_arg(ap, int);
			std::basic_string<CharT> body;
			if constexpr (sizeof(CharT) > 1) {
				if (wantWide) body += static_cast<CharT>(v);
				else { const char ch = static_cast<char>(v); body = AcpToWide(&ch, 1); }
			} else {
				if (wantWide) { const wchar_t wc = static_cast<wchar_t>(v); body = WideToAcp(&wc, 1); }
				else body += static_cast<CharT>(v);
			}
			Pad(out, body, width, left);
			break;
		}
		case 's': case 'S': {
			const bool wantWide = strWidth ? strWidth > 0 : ((conv == 's') == wideFn);
			std::basic_string<CharT> body;
			if (wantWide) {
				const wchar_t* s = va_arg(ap, const wchar_t*);
				if (!s) s = L"(null)";
				int n = static_cast<int>(std::wcslen(s));
				if (prec >= 0 && prec < n) n = prec;
				if constexpr (sizeof(CharT) > 1) body.assign(s, s + n);
				else body = WideToAcp(s, n);
			} else {
				const char* s = va_arg(ap, const char*);
				if (!s) s = "(null)";
				int n = static_cast<int>(std::strlen(s));
				if (prec >= 0 && prec < n) n = prec;
				if constexpr (sizeof(CharT) > 1) body = AcpToWide(s, n);
				else body.assign(s, s + n);
			}
			Pad(out, body, width, left);
			break;
		}
		default:
			// 未知の変換指定はそのまま出す
			out += CharT('%');
			out += *p;
			break;
		}
	}
	return static_cast<int>(out.size());
}

template <typename CharT>
std::basic_string<CharT> Format(const CharT* fmt, va_list ap)
{
	std::basic_string<CharT> out;
	va_list cp;
	va_copy(cp, ap);
	FormatImpl(out, fmt, cp);
	va_end(cp);
	return out;
}

//! MSVC の _vsnwprintf_s / _vsnprintf_s と同じ規則で書き出す
template <typename CharT>
int WriteS(CharT* buf, size_t n, size_t count, const std::basic_string<CharT>& s)
{
	if (!buf || n == 0) return -1;
	const size_t len = s.size();
	if (count == _TRUNCATE) {
		const size_t keep = len < n ? len : n - 1;
		std::memcpy(buf, s.data(), keep * sizeof(CharT));
		buf[keep] = 0;
		return len < n ? static_cast<int>(len) : -1;
	}
	const size_t limit = count < len ? count : len;
	if (limit >= n) { buf[0] = 0; return -1; }  // 収まらない（MSVC では無効パラメータ）
	std::memcpy(buf, s.data(), limit * sizeof(CharT));
	buf[limit] = 0;
	return limit < len ? -1 : static_cast<int>(len);
}

//! サイズ指定の無い書き出し（_vswprintf 等）
template <typename CharT>
int WriteUnbounded(CharT* buf, const std::basic_string<CharT>& s)
{
	std::memcpy(buf, s.c_str(), (s.size() + 1) * sizeof(CharT));
	return static_cast<int>(s.size());
}

//! C 標準の vswprintf / vsnprintf と同じ規則（n 以上なら -1。narrow は切り詰めて必要長を返す）
int WriteStdWide(wchar_t* buf, size_t n, const std::wstring& s)
{
	if (!buf || n == 0) return -1;
	if (s.size() >= n) {
		std::wmemcpy(buf, s.data(), n - 1);
		buf[n - 1] = 0;
		return -1;
	}
	std::wmemcpy(buf, s.c_str(), s.size() + 1);
	return static_cast<int>(s.size());
}

}  // namespace

extern "C" {

int _vswprintf(wchar_t* buf, const wchar_t* fmt, va_list ap) { return WriteUnbounded(buf, Format(fmt, ap)); }
int _swprintf(wchar_t* buf, const wchar_t* fmt, ...)
{
	va_list ap; va_start(ap, fmt); const int r = _vswprintf(buf, fmt, ap); va_end(ap); return r;
}

int vswprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, va_list ap) { return WriteS(buf, n, n - 1, Format(fmt, ap)); }
int swprintf_s(wchar_t* buf, size_t n, const wchar_t* fmt, ...)
{
	va_list ap; va_start(ap, fmt); const int r = vswprintf_s(buf, n, fmt, ap); va_end(ap); return r;
}

int _vsnwprintf_s(wchar_t* buf, size_t n, size_t count, const wchar_t* fmt, va_list ap) { return WriteS(buf, n, count, Format(fmt, ap)); }
int _snwprintf_s(wchar_t* buf, size_t n, size_t count, const wchar_t* fmt, ...)
{
	va_list ap; va_start(ap, fmt); const int r = _vsnwprintf_s(buf, n, count, fmt, ap); va_end(ap); return r;
}

int _vscwprintf(const wchar_t* fmt, va_list ap) { return static_cast<int>(Format(fmt, ap).size()); }

int _vsnprintf_s(char* buf, size_t n, size_t count, const char* fmt, va_list ap) { return WriteS(buf, n, count, Format(fmt, ap)); }
int _snprintf_s(char* buf, size_t n, size_t count, const char* fmt, ...)
{
	va_list ap; va_start(ap, fmt); const int r = _vsnprintf_s(buf, n, count, fmt, ap); va_end(ap); return r;
}
int vsprintf_s(char* buf, size_t n, const char* fmt, va_list ap) { return WriteS(buf, n, n - 1, Format(fmt, ap)); }
int _vscprintf(const char* fmt, va_list ap) { return static_cast<int>(Format(fmt, ap).size()); }

int swscanf_s(const wchar_t* buf, const wchar_t* fmt, ...)
{
	// sakura_core の使用箇所は数値変換のみ（%zx 等）なので、バッファサイズ引数の扱いは不要
	va_list ap; va_start(ap, fmt); const int r = std::vswscanf(buf, fmt, ap); va_end(ap); return r;
}

/* 標準名の swprintf / vswprintf も MSVC 書式で動かす（winport_crt.h のマクロで差し替え） */
int winport_vswprintf_ms(wchar_t* buf, size_t n, const wchar_t* fmt, va_list ap) { return WriteStdWide(buf, n, Format(fmt, ap)); }
int winport_swprintf_ms(wchar_t* buf, size_t n, const wchar_t* fmt, ...)
{
	va_list ap; va_start(ap, fmt); const int r = winport_vswprintf_ms(buf, n, fmt, ap); va_end(ap); return r;
}

/* user32 の wsprintfW / wvsprintfW（MSVC 書式、出力上限 1024 文字） */
int WINAPI wvsprintfW(LPWSTR buf, LPCWSTR fmt, va_list ap) { return WriteS(buf, 1025, _TRUNCATE, Format(fmt, ap)); }
int WINAPIV wsprintfW(LPWSTR buf, LPCWSTR fmt, ...)
{
	va_list ap; va_start(ap, fmt); const int r = wvsprintfW(buf, fmt, ap); va_end(ap); return r;
}

}  // extern "C"
