#include "Encoding.h"

#include <wx/strconv.h>

namespace sakura_wx::charset {

const char* EncodingName(Encoding e) {
    switch (e) {
    case Encoding::UTF8:     return "UTF-8";
    case Encoding::UTF8_BOM: return "UTF-8 BOM";
    case Encoding::SJIS:     return "Shift-JIS";
    case Encoding::EUCJP:    return "EUC-JP";
    case Encoding::UTF16LE:  return "UTF-16LE";
    case Encoding::UTF16BE:  return "UTF-16BE";
    default:                 return "Unknown";
    }
}

Encoding Detect(const std::vector<char>& bytes) {
    auto u = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };

    if (bytes.size() >= 3 && u(0) == 0xEF && u(1) == 0xBB && u(2) == 0xBF) {
        return Encoding::UTF8_BOM;
    }
    if (bytes.size() >= 2) {
        if (u(0) == 0xFF && u(1) == 0xFE) return Encoding::UTF16LE;
        if (u(0) == 0xFE && u(1) == 0xFF) return Encoding::UTF16BE;
    }

    int hi = 0;
    int sjisHits = 0;
    int eucHits  = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        unsigned char c = u(i);
        if (c < 0x80) continue;
        ++hi;
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) ++sjisHits;
        if ((c >= 0xA1 && c <= 0xFE) || c == 0x8E || c == 0x8F)   ++eucHits;
    }

    if (hi == 0) return Encoding::UTF8;

    auto looksUtf8 = [&]() {
        for (size_t i = 0; i < bytes.size();) {
            unsigned char c = u(i);
            if (c < 0x80) { ++i; continue; }
            int extra = 0;
            if      ((c & 0xE0) == 0xC0) extra = 1;
            else if ((c & 0xF0) == 0xE0) extra = 2;
            else if ((c & 0xF8) == 0xF0) extra = 3;
            else return false;
            if (i + extra >= bytes.size()) return false;
            for (int k = 1; k <= extra; ++k) {
                if ((u(i + k) & 0xC0) != 0x80) return false;
            }
            i += extra + 1;
        }
        return true;
    };
    if (looksUtf8()) return Encoding::UTF8;

    if (sjisHits > eucHits) return Encoding::SJIS;
    if (eucHits > 0)        return Encoding::EUCJP;
    return Encoding::UTF8;
}

wxString DecodeToWxString(const std::vector<char>& bytes, Encoding enc) {
    if (bytes.empty()) return wxString();

    size_t offset = 0;
    if (enc == Encoding::UTF8_BOM) {
        offset = (bytes.size() >= 3 ? 3 : bytes.size());
    }

    auto data = bytes.data() + offset;
    auto len  = bytes.size() - offset;

    switch (enc) {
    case Encoding::UTF8:
    case Encoding::UTF8_BOM:
        return wxString::FromUTF8(data, len);

    case Encoding::SJIS:
        return wxString(data, wxCSConv(wxT("CP932")), len);

    case Encoding::EUCJP:
        return wxString(data, wxCSConv(wxT("EUC-JP")), len);

    case Encoding::UTF16LE: {
        // UTF-16LE: BOM 2 byte をスキップ済み前提で扱う
        size_t skip = (bytes.size() >= 2
                       && static_cast<unsigned char>(bytes[0]) == 0xFF
                       && static_cast<unsigned char>(bytes[1]) == 0xFE) ? 2 : 0;
        if (skip > bytes.size()) skip = bytes.size();
        return wxString(reinterpret_cast<const char*>(bytes.data() + skip),
                        wxMBConvUTF16LE(),
                        bytes.size() - skip);
    }
    case Encoding::UTF16BE: {
        size_t skip = (bytes.size() >= 2
                       && static_cast<unsigned char>(bytes[0]) == 0xFE
                       && static_cast<unsigned char>(bytes[1]) == 0xFF) ? 2 : 0;
        if (skip > bytes.size()) skip = bytes.size();
        return wxString(reinterpret_cast<const char*>(bytes.data() + skip),
                        wxMBConvUTF16BE(),
                        bytes.size() - skip);
    }

    default:
        return wxString::FromUTF8(data, len);
    }
}

std::vector<char> EncodeFromWxString(const wxString& text, Encoding enc) {
    auto from_utf8 = [&]() {
        auto utf8 = text.ToUTF8();
        return std::vector<char>(utf8.data(), utf8.data() + utf8.length());
    };

    switch (enc) {
    case Encoding::UTF8: return from_utf8();
    case Encoding::UTF8_BOM: {
        auto v = from_utf8();
        v.insert(v.begin(), {static_cast<char>(0xEF),
                             static_cast<char>(0xBB),
                             static_cast<char>(0xBF)});
        return v;
    }
    case Encoding::SJIS: {
        auto mb = text.mb_str(wxCSConv(wxT("CP932")));
        return std::vector<char>(mb.data(), mb.data() + mb.length());
    }
    case Encoding::EUCJP: {
        auto mb = text.mb_str(wxCSConv(wxT("EUC-JP")));
        return std::vector<char>(mb.data(), mb.data() + mb.length());
    }
    case Encoding::UTF16LE: {
        auto mb = text.mb_str(wxMBConvUTF16LE());
        std::vector<char> v;
        v.push_back(static_cast<char>(0xFF));
        v.push_back(static_cast<char>(0xFE));
        v.insert(v.end(), mb.data(), mb.data() + mb.length());
        return v;
    }
    case Encoding::UTF16BE: {
        auto mb = text.mb_str(wxMBConvUTF16BE());
        std::vector<char> v;
        v.push_back(static_cast<char>(0xFE));
        v.push_back(static_cast<char>(0xFF));
        v.insert(v.end(), mb.data(), mb.data() + mb.length());
        return v;
    }
    default:
        return from_utf8();
    }
}

} // namespace sakura_wx::charset
