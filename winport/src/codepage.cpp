/*
	codepage.cpp — MultiByteToWideChar / WideCharToMultiByte / GetACP 等

	- CP_UTF8 は自前実装（UTF-16 単位 ⇔ UTF-8）
	- CP932 は初回利用時に CoreFoundation から変換表を作り、以後は表引きする
	  （sakura_core は 1 文字ずつ呼ぶため速度が要る）
	- CP_ACP は UTF-8（65001）とする。clang は narrow 文字列リテラルを UTF-8 で埋め込むので、
	  Windows と同じ「narrow リテラル = ACP」の関係が保たれ、POSIX へ渡す narrow 文字列とも一致する
	- その他のコードページは CoreFoundation でそのまま変換する

	SPDX-License-Identifier: Zlib
*/
#include <windows.h>

#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <mutex>
#include <string>
#include <vector>

#include "winport_internal.h"

namespace {

constexpr UINT kAnsiCodePage = CP_UTF8;

UINT Resolve(UINT cp)
{
	switch (cp) {
	case CP_ACP:
	case CP_OEMCP:
	case CP_THREAD_ACP:
	case CP_MACCP:
		return kAnsiCodePage;
	default:
		return cp;
	}
}

// ---- CP932 変換表 ----
struct Cp932Table {
	std::array<char16_t, 0x10000> toWide{};       // index = SJIS コード（1 バイトは 0x00XX）。0 = 未定義
	std::array<uint16_t, 0x10000> fromWide{};     // index = UTF-16 単位。0 = 変換不可（U+0000 は別扱い）
	std::array<bool, 0x100> lead{};
};

bool IsLead932(unsigned b) { return (0x81 <= b && b <= 0x9F) || (0xE0 <= b && b <= 0xFC); }

char16_t DecodeCF(const UInt8* bytes, CFIndex n)
{
	CFStringRef s = CFStringCreateWithBytes(nullptr, bytes, n, kCFStringEncodingDOSJapanese, false);
	if (!s) return 0;
	char16_t c = 0;
	if (CFStringGetLength(s) == 1) c = static_cast<char16_t>(CFStringGetCharacterAtIndex(s, 0));
	CFRelease(s);
	return c;
}

const Cp932Table& Table932()
{
	static Cp932Table* t = [] {
		auto p = new Cp932Table;
		for (unsigned b = 0; b < 0x100; ++b) p->lead[b] = IsLead932(b);
		// 1 バイト文字
		for (unsigned b = 0; b < 0x100; ++b) {
			if (p->lead[b]) continue;
			const UInt8 c = static_cast<UInt8>(b);
			p->toWide[b] = b < 0x80 ? static_cast<char16_t>(b) : DecodeCF(&c, 1);
		}
		// Windows の cp932 は未定義の 1 バイトを私用領域等に写す
		p->toWide[0x80] = 0x0080;
		p->toWide[0xA0] = 0xF8F0;
		p->toWide[0xFD] = 0xF8F1;
		p->toWide[0xFE] = 0xF8F2;
		p->toWide[0xFF] = 0xF8F3;
		// 2 バイト文字
		for (unsigned hi = 0x81; hi <= 0xFC; ++hi) {
			if (!p->lead[hi]) continue;
			for (unsigned lo = 0x40; lo <= 0xFC; ++lo) {
				if (lo == 0x7F) continue;
				const unsigned code = (hi << 8) | lo;
				if (0xF0 <= hi && hi <= 0xF9) {
					// ユーザー定義文字 F040-F9FC ⇔ U+E000-E757
					const unsigned idx = (hi - 0xF0) * 188 + (lo - 0x40 - (lo > 0x7F ? 1 : 0));
					p->toWide[code] = static_cast<char16_t>(0xE000 + idx);
					continue;
				}
				const UInt8 bytes[2] = {static_cast<UInt8>(hi), static_cast<UInt8>(lo)};
				p->toWide[code] = DecodeCF(bytes, 2);
			}
		}
		// 逆変換: Unicode → CP932。重複定義は Windows と同じ優先順位で 1 つに決める:
		//   1 バイト → JIS X 0208（81-84, 88-9F, E0-EA）→ NEC 特殊文字（87）→ IBM 拡張（FA-FC）
		//   → NEC 選定 IBM 拡張（ED-EE）→ ユーザー定義（F0-F9）
		auto assign = [p](unsigned code) {
			const char16_t u = p->toWide[code];
			if (u && !p->fromWide[u]) p->fromWide[u] = static_cast<uint16_t>(code);
		};
		auto assignLeads = [&](unsigned from, unsigned to) {
			for (unsigned hi = from; hi <= to; ++hi)
				for (unsigned lo = 0x40; lo <= 0xFC; ++lo)
					if (lo != 0x7F) assign((hi << 8) | lo);
		};
		for (unsigned b = 0; b < 0x100; ++b) if (!p->lead[b] && b != 0x80 && b != 0xA0 && b < 0xFD) assign(b);
		assignLeads(0x81, 0x84);
		assignLeads(0x88, 0x9F);
		assignLeads(0xE0, 0xEA);
		assignLeads(0x87, 0x87);
		assignLeads(0xFA, 0xFC);
		assignLeads(0xED, 0xEE);
		assignLeads(0xF0, 0xF9);
		assignLeads(0x85, 0x86);
		assignLeads(0xEB, 0xEC);
		assignLeads(0xEF, 0xEF);
		p->fromWide[0] = 0;
		return p;
	}();
	return *t;
}

// ---- UTF-8 ----
int Utf8ToUnits(const unsigned char* s, int n, std::u16string& out, bool strict, bool& invalid)
{
	for (int i = 0; i < n;) {
		const unsigned b = s[i];
		char32_t c;
		int len;
		if (b < 0x80) { c = b; len = 1; }
		else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; len = 2; }
		else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; len = 3; }
		else if ((b & 0xF8) == 0xF0) { c = b & 0x07; len = 4; }
		else { invalid = true; out += u'\xFFFD'; ++i; continue; }
		bool ok = i + len <= n;
		for (int k = 1; ok && k < len; ++k) {
			if ((s[i + k] & 0xC0) != 0x80) ok = false;
			else c = (c << 6) | (s[i + k] & 0x3F);
		}
		static constexpr char32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
		if (ok && (c < kMin[len] || c > 0x10FFFF || (0xD800 <= c && c <= 0xDFFF))) ok = false;
		if (!ok) { invalid = true; out += u'\xFFFD'; ++i; continue; }
		i += len;
		if (c >= 0x10000) {
			c -= 0x10000;
			out += static_cast<char16_t>(0xD800 + (c >> 10));
			out += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
		} else {
			out += static_cast<char16_t>(c);
		}
	}
	(void)strict;
	return static_cast<int>(out.size());
}

void UnitsToUtf8(const char16_t* s, int n, std::string& out)
{
	for (int i = 0; i < n; ++i) {
		char32_t c = s[i];
		if (0xD800 <= c && c <= 0xDBFF && i + 1 < n && 0xDC00 <= s[i + 1] && s[i + 1] <= 0xDFFF) {
			c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			++i;
		} else if (0xD800 <= c && c <= 0xDFFF) {
			c = 0xFFFD;  // 孤立サロゲート（Windows も U+FFFD に置換する）
		}
		if (c < 0x80) out += static_cast<char>(c);
		else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) {
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}
}

// ---- 汎用（CoreFoundation） ----
//! Windows の cp1252 は未定義の 5 バイト（0x81 0x8D 0x8F 0x90 0x9D）を同じ値の制御文字に写す
bool IsCp1252Hole(unsigned c) { return c == 0x81 || c == 0x8D || c == 0x8F || c == 0x90 || c == 0x9D; }

bool DecodeGeneric(UINT cp, const unsigned char* s, int n, std::u16string& out)
{
	if (cp == 1252) {
		for (int i = 0; i < n; ++i) {
			if (IsCp1252Hole(s[i])) { out += static_cast<char16_t>(s[i]); continue; }
			CFStringRef str = CFStringCreateWithBytes(nullptr, s + i, 1, kCFStringEncodingWindowsLatin1, false);
			if (!str) { out += u'\xFFFD'; continue; }
			out += static_cast<char16_t>(CFStringGetCharacterAtIndex(str, 0));
			CFRelease(str);
		}
		return true;
	}
	const CFStringEncoding enc = CFStringConvertWindowsCodepageToEncoding(cp);
	if (enc == kCFStringEncodingInvalidId) return false;
	CFStringRef str = CFStringCreateWithBytes(nullptr, s, n, enc, false);
	if (!str) return false;
	const CFIndex len = CFStringGetLength(str);
	out.resize(len);
	CFStringGetCharacters(str, CFRangeMake(0, len), reinterpret_cast<UniChar*>(out.data()));
	CFRelease(str);
	return true;
}

bool EncodeGeneric(UINT cp, const char16_t* s, int n, std::string& out, char defChar, bool& usedDefault)
{
	const CFStringEncoding enc = CFStringConvertWindowsCodepageToEncoding(cp);
	if (enc == kCFStringEncodingInvalidId) return false;
	for (int i = 0; i < n; ++i) {
		if (cp == 1252 && IsCp1252Hole(s[i])) { out += static_cast<char>(s[i]); continue; }
		const int units = (0xD800 <= s[i] && s[i] <= 0xDBFF && i + 1 < n) ? 2 : 1;
		CFStringRef str = CFStringCreateWithCharactersNoCopy(nullptr, reinterpret_cast<const UniChar*>(s + i), units, kCFAllocatorNull);
		UInt8 buf[8];
		CFIndex used = 0;
		if (CFStringGetBytes(str, CFRangeMake(0, units), enc, 0, false, buf, sizeof(buf), &used) == units) {
			out.append(reinterpret_cast<char*>(buf), used);
		} else {
			out += defChar;
			usedDefault = true;
		}
		CFRelease(str);
		i += units - 1;
	}
	return true;
}

}  // namespace

extern "C" {

UINT WINAPI GetACP(void) { return kAnsiCodePage; }
UINT WINAPI GetOEMCP(void) { return kAnsiCodePage; }

WINBOOL WINAPI IsValidCodePage(UINT CodePage)
{
	const UINT cp = Resolve(CodePage);
	if (cp == CP_UTF8 || cp == CP_UTF7 || cp == 932) return TRUE;
	return CFStringConvertWindowsCodepageToEncoding(cp) != kCFStringEncodingInvalidId;
}

WINBOOL WINAPI IsDBCSLeadByteEx(UINT CodePage, BYTE TestChar)
{
	return Resolve(CodePage) == 932 && IsLead932(TestChar);
}

WINBOOL WINAPI IsDBCSLeadByte(BYTE TestChar) { return IsDBCSLeadByteEx(CP_ACP, TestChar); }

int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte,
                               LPWSTR lpWideCharStr, int cchWideChar)
{
	if (!lpMultiByteStr || cbMultiByte == 0 || cchWideChar < 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	const auto src = reinterpret_cast<const unsigned char*>(lpMultiByteStr);
	const int n = cbMultiByte < 0 ? static_cast<int>(std::strlen(lpMultiByteStr)) + 1 : cbMultiByte;
	const bool strict = (dwFlags & MB_ERR_INVALID_CHARS) != 0;
	const UINT cp = Resolve(CodePage);

	std::u16string out;
	bool invalid = false;
	if (cp == CP_UTF8) {
		Utf8ToUnits(src, n, out, strict, invalid);
	} else if (cp == 932) {
		const auto& t = Table932();
		for (int i = 0; i < n; ++i) {
			const unsigned b = src[i];
			if (t.lead[b] && i + 1 < n) {
				const char16_t c = t.toWide[(b << 8) | src[i + 1]];
				if (c) { out += c; ++i; continue; }
				invalid = true;
				out += u'\x30FB';  // Windows は未定義の 2 バイトを「・」にする
				++i;
				continue;
			}
			if (t.lead[b]) { invalid = true; out += u'\x30FB'; continue; }
			const char16_t c = t.toWide[b];
			if (!c && b) { invalid = true; out += u'\x30FB'; continue; }
			out += c;
		}
	} else if (!DecodeGeneric(cp, src, n, out)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	if (strict && invalid) {
		SetLastError(ERROR_NO_UNICODE_TRANSLATION);
		return 0;
	}
	const int len = static_cast<int>(out.size());
	if (cchWideChar == 0) return len;
	if (len > cchWideChar) {
		for (int i = 0; i < cchWideChar; ++i) lpWideCharStr[i] = out[i];
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	for (int i = 0; i < len; ++i) lpWideCharStr[i] = out[i];
	return len;
}

int WINAPI WideCharToMultiByte(UINT CodePage, DWORD, LPCWCH lpWideCharStr, int cchWideChar,
                               LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
	if (!lpWideCharStr || cchWideChar == 0 || cbMultiByte < 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	const UINT cp = Resolve(CodePage);
	// 現行の Windows は CP_UTF8 で lpUsedDefaultChar を受け付ける（上流のテストもそれを前提にしている）。
	// lpDefaultChar は従来どおり不可。
	if (cp == CP_UTF8 && lpDefaultChar) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	const int n = cchWideChar < 0 ? static_cast<int>(std::wcslen(lpWideCharStr)) + 1 : cchWideChar;
	std::u16string units(n, u'\0');
	for (int i = 0; i < n; ++i) units[i] = static_cast<char16_t>(lpWideCharStr[i]);

	const char defChar = lpDefaultChar ? *lpDefaultChar : '?';
	bool usedDefault = false;
	std::string out;
	if (cp == CP_UTF8) {
		UnitsToUtf8(units.data(), n, out);
	} else if (cp == 932) {
		const auto& t = Table932();
		for (int i = 0; i < n; ++i) {
			const char16_t u = units[i];
			if (u == 0) { out += '\0'; continue; }
			if (0xD800 <= u && u <= 0xDBFF && i + 1 < n && 0xDC00 <= units[i + 1] && units[i + 1] <= 0xDFFF) {
				// サロゲートペアは CP932 にない。Windows は 1 文字分の既定文字にする
				out += defChar;
				usedDefault = true;
				++i;
				continue;
			}
			const uint16_t m = t.fromWide[u];
			if (!m) { out += defChar; usedDefault = true; continue; }
			if (m > 0xFF) out += static_cast<char>(m >> 8);
			out += static_cast<char>(m & 0xFF);
		}
	} else if (!EncodeGeneric(cp, units.data(), n, out, defChar, usedDefault)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return 0;
	}
	if (lpUsedDefaultChar) *lpUsedDefaultChar = usedDefault ? TRUE : FALSE;
	const int len = static_cast<int>(out.size());
	if (cbMultiByte == 0) return len;
	if (len > cbMultiByte) {
		std::memcpy(lpMultiByteStr, out.data(), cbMultiByte);
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	std::memcpy(lpMultiByteStr, out.data(), len);
	return len;
}

}  // extern "C"
