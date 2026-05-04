// tests/wx_tests/test_encoding.cpp
//
// sakura_wx::charset::{Detect, DecodeToWxString, EncodeFromWxString} の
// round-trip 単体テスト。

#include <gtest/gtest.h>

#include "charset/Encoding.h"

#include <wx/init.h>
#include <wx/string.h>

#include <cstring>
#include <vector>

using namespace sakura_wx::charset;

namespace {

// wxString / wxCSConv 等の利用に必要な wx ライブラリ初期化を
// テストプロセス全体で 1 回だけ行う。wxInitializer は RAII。
wxInitializer& WxLifetime() {
    static wxInitializer g_init;
    return g_init;
}
struct WxInitGuard { WxInitGuard() { (void)WxLifetime(); } };
WxInitGuard g_wx_init;

std::vector<char> Bytes(std::initializer_list<unsigned char> bs) {
    std::vector<char> v;
    v.reserve(bs.size());
    for (auto b : bs) v.push_back(static_cast<char>(b));
    return v;
}

} // namespace

// ----- Detect -----------------------------------------------

TEST(Encoding_Detect, EmptyIsUtf8) {
    EXPECT_EQ(Detect({}), Encoding::UTF8);
}

TEST(Encoding_Detect, AsciiIsUtf8) {
    auto v = Bytes({'h','e','l','l','o','\n'});
    EXPECT_EQ(Detect(v), Encoding::UTF8);
}

TEST(Encoding_Detect, Utf8BomDetected) {
    // BOM + "ABC"
    auto v = Bytes({0xEF, 0xBB, 0xBF, 'A', 'B', 'C'});
    EXPECT_EQ(Detect(v), Encoding::UTF8_BOM);
}

TEST(Encoding_Detect, Utf16LE_BomDetected) {
    auto v = Bytes({0xFF, 0xFE, 'A', 0x00});
    EXPECT_EQ(Detect(v), Encoding::UTF16LE);
}

TEST(Encoding_Detect, Utf16BE_BomDetected) {
    auto v = Bytes({0xFE, 0xFF, 0x00, 'A'});
    EXPECT_EQ(Detect(v), Encoding::UTF16BE);
}

TEST(Encoding_Detect, ValidUtf8MultibyteIsUtf8) {
    // "あ" は UTF-8 で E3 81 82
    auto v = Bytes({0xE3, 0x81, 0x82});
    EXPECT_EQ(Detect(v), Encoding::UTF8);
}

TEST(Encoding_Detect, ShiftJisOnlyBytesIsSjis) {
    // "あ" は CP932 で 82 A0、UTF-8 妥当性チェックを失敗させる
    auto v = Bytes({0x82, 0xA0, 0x82, 0xA2});  // "あい"
    EXPECT_EQ(Detect(v), Encoding::SJIS);
}

// ----- DecodeToWxString -------------------------------------

TEST(Encoding_Decode, Utf8RoundTrip) {
    wxString original = wxString::FromUTF8("Hello, 世界");
    auto bytes = EncodeFromWxString(original, Encoding::UTF8);
    wxString decoded = DecodeToWxString(bytes, Encoding::UTF8);
    EXPECT_EQ(decoded, original);
}

TEST(Encoding_Decode, Utf8BomRoundTrip) {
    wxString original = wxString::FromUTF8("テキスト");
    auto bytes = EncodeFromWxString(original, Encoding::UTF8_BOM);
    // BOM 付きで書き出されている
    ASSERT_GE(bytes.size(), size_t{3});
    EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0xEF);
    EXPECT_EQ(static_cast<unsigned char>(bytes[1]), 0xBB);
    EXPECT_EQ(static_cast<unsigned char>(bytes[2]), 0xBF);
    // 自動判定が UTF-8 BOM を返す
    EXPECT_EQ(Detect(bytes), Encoding::UTF8_BOM);
    // BOM をスキップして正しく decode される
    wxString decoded = DecodeToWxString(bytes, Encoding::UTF8_BOM);
    EXPECT_EQ(decoded, original);
}

TEST(Encoding_Decode, ShiftJisRoundTrip) {
    wxString original = wxString::FromUTF8("日本語テスト");
    auto bytes = EncodeFromWxString(original, Encoding::SJIS);
    EXPECT_FALSE(bytes.empty());
    wxString decoded = DecodeToWxString(bytes, Encoding::SJIS);
    EXPECT_EQ(decoded, original);
}

TEST(Encoding_Decode, EucJpRoundTrip) {
    wxString original = wxString::FromUTF8("日本語");
    auto bytes = EncodeFromWxString(original, Encoding::EUCJP);
    EXPECT_FALSE(bytes.empty());
    wxString decoded = DecodeToWxString(bytes, Encoding::EUCJP);
    EXPECT_EQ(decoded, original);
}

TEST(Encoding_Decode, Utf16LeRoundTrip) {
    wxString original = wxString::FromUTF8("こんにちは");
    auto bytes = EncodeFromWxString(original, Encoding::UTF16LE);
    // BOM 付き
    ASSERT_GE(bytes.size(), size_t{2});
    EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(bytes[1]), 0xFE);
    EXPECT_EQ(Detect(bytes), Encoding::UTF16LE);
    wxString decoded = DecodeToWxString(bytes, Encoding::UTF16LE);
    EXPECT_EQ(decoded, original);
}

TEST(Encoding_Decode, Utf16BeRoundTrip) {
    wxString original = wxString::FromUTF8("世界");
    auto bytes = EncodeFromWxString(original, Encoding::UTF16BE);
    ASSERT_GE(bytes.size(), size_t{2});
    EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0xFE);
    EXPECT_EQ(static_cast<unsigned char>(bytes[1]), 0xFF);
    EXPECT_EQ(Detect(bytes), Encoding::UTF16BE);
    wxString decoded = DecodeToWxString(bytes, Encoding::UTF16BE);
    EXPECT_EQ(decoded, original);
}

// ----- EncodingName -----------------------------------------

TEST(Encoding_Name, NamesAreNonEmpty) {
    EXPECT_STRNE(EncodingName(Encoding::UTF8),     "");
    EXPECT_STRNE(EncodingName(Encoding::UTF8_BOM), "");
    EXPECT_STRNE(EncodingName(Encoding::SJIS),     "");
    EXPECT_STRNE(EncodingName(Encoding::EUCJP),    "");
    EXPECT_STRNE(EncodingName(Encoding::UTF16LE),  "");
    EXPECT_STRNE(EncodingName(Encoding::UTF16BE),  "");
    EXPECT_STRNE(EncodingName(Encoding::Unknown),  "");
}
