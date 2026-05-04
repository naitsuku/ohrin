#pragma once
#include <wx/string.h>

#include <vector>

namespace sakura_wx::charset {

enum class Encoding {
    UTF8,
    UTF8_BOM,
    SJIS,
    EUCJP,
    UTF16LE,
    UTF16BE,
    Unknown,
};

const char* EncodingName(Encoding e);

/**
 * バイト列から推定エンコーディングを返す。
 * - 先頭の BOM を最優先で確定（UTF-8 BOM, UTF-16 LE/BE）
 * - 残りは UTF-8 妥当性チェック → SJIS/EUC-JP のヒューリスティック
 */
Encoding Detect(const std::vector<char>& bytes);

/**
 * バイト列を wxString（内部 wchar_t）に変換する。
 * BOM はスキップされる。
 */
wxString DecodeToWxString(const std::vector<char>& bytes, Encoding enc);

/**
 * wxString をバイト列にエンコードする。
 * UTF-8 BOM / UTF-16 LE/BE では BOM を先頭に付与する。
 */
std::vector<char> EncodeFromWxString(const wxString& text, Encoding enc);

} // namespace sakura_wx::charset
