/*
	debug.cpp — 未実装 API の報告、UTF-16(wchar_t) ⇔ UTF-8 変換

	SPDX-License-Identifier: Zlib
*/
#include "winport_internal.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>

namespace winport {

void ReportUnimplemented(const char* dll, const char* name)
{
	static std::mutex mtx;
	static std::set<std::string> reported;
	std::lock_guard lock(mtx);
	if (reported.insert(name).second && !std::getenv("WINPORT_QUIET")) {
		std::fprintf(stderr, "winport: unimplemented %s!%s\n", dll, name);
	}
}

// wchar_t は 4 バイトだが、中身は UTF-16 コード単位（サロゲートペアは 2 要素）。
std::string WideToUtf8(std::wstring_view ws)
{
	std::string out;
	out.reserve(ws.size());
	for (size_t i = 0; i < ws.size(); ++i) {
		char32_t c = static_cast<char16_t>(ws[i]);
		if (0xD800 <= c && c <= 0xDBFF && i + 1 < ws.size()) {
			const char32_t lo = static_cast<char16_t>(ws[i + 1]);
			if (0xDC00 <= lo && lo <= 0xDFFF) {
				c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
				++i;
			}
		}
		if (c < 0x80) {
			out += static_cast<char>(c);
		} else if (c < 0x800) {
			out += static_cast<char>(0xC0 | (c >> 6));
			out += static_cast<char>(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
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
	return out;
}

std::wstring Utf8ToWide(std::string_view s)
{
	std::wstring out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();) {
		const auto b = static_cast<unsigned char>(s[i]);
		char32_t c;
		size_t n;
		if (b < 0x80) { c = b; n = 1; }
		else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; n = 2; }
		else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; n = 3; }
		else if ((b & 0xF8) == 0xF0) { c = b & 0x07; n = 4; }
		else { out += L'\xFFFD'; ++i; continue; }
		if (i + n > s.size()) { out += L'\xFFFD'; break; }
		for (size_t k = 1; k < n; ++k) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
		i += n;
		if (c >= 0x10000) {
			c -= 0x10000;
			out += static_cast<wchar_t>(0xD800 + (c >> 10));
			out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
		} else {
			out += static_cast<wchar_t>(c);
		}
	}
	return out;
}

}  // namespace winport
