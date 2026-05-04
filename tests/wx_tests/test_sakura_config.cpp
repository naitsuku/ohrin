// tests/wx_tests/test_sakura_config.cpp
//
// SakuraConfig の round-trip テスト。実際の INI ファイルへ書き出し、
// 別インスタンスで読み出して値が一致することを確認する。

#include <gtest/gtest.h>

#include "config/SakuraConfig.h"

#include <wx/init.h>

namespace {
wxInitializer& WxLifetime() {
    static wxInitializer g_init;
    return g_init;
}
struct WxInitGuard { WxInitGuard() { (void)WxLifetime(); } };
WxInitGuard g_wx_init;
} // namespace

TEST(SakuraConfig, FontRoundTrip) {
    {
        SakuraConfig c;
        wxFont f(14, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                 wxFONTWEIGHT_NORMAL, false, wxT("Menlo"));
        c.SetEditorFont(f);
        c.Save();
    }
    {
        SakuraConfig c;
        c.Load();
        wxFont f = c.GetEditorFont();
        EXPECT_EQ(f.GetPointSize(), 14);
        EXPECT_EQ(f.GetFaceName(), wxT("Menlo"));
    }
}

TEST(SakuraConfig, BoolAndIntRoundTrip) {
    {
        SakuraConfig c;
        c.SetShowLineNumbers(false);
        c.SetWordWrap(true);
        c.SetTabWidth(8);
        c.Save();
    }
    {
        SakuraConfig c;
        c.Load();
        EXPECT_FALSE(c.GetShowLineNumbers());
        EXPECT_TRUE(c.GetWordWrap());
        EXPECT_EQ(c.GetTabWidth(), 8);
    }
    // テスト後に既定に戻す（後続テストへの汚染を最小化）
    {
        SakuraConfig c;
        c.SetShowLineNumbers(true);
        c.SetWordWrap(false);
        c.SetTabWidth(4);
        c.Save();
    }
}

TEST(SakuraConfig, WindowGeometryRoundTrip) {
    {
        SakuraConfig c;
        c.SetWindowPosition(wxPoint(123, 456));
        c.SetWindowSize(wxSize(1024, 768));
        c.Save();
    }
    {
        SakuraConfig c;
        c.Load();
        EXPECT_EQ(c.GetWindowPosition(), wxPoint(123, 456));
        EXPECT_EQ(c.GetWindowSize(),     wxSize(1024, 768));
    }
}

TEST(SakuraConfig, RecentFilesAddDeduplicateAndCap) {
    {
        SakuraConfig c;
        c.ClearRecentFiles();
        c.Save();
    }
    SakuraConfig c;
    c.Load();
    ASSERT_EQ(c.GetRecentFiles().GetCount(), 0u);

    c.AddRecentFile(wxT("/tmp/a.txt"));
    c.AddRecentFile(wxT("/tmp/b.txt"));
    c.AddRecentFile(wxT("/tmp/a.txt"));    // 重複: a が先頭に来る
    auto files = c.GetRecentFiles();
    ASSERT_EQ(files.GetCount(), 2u);
    EXPECT_EQ(files[0], wxT("/tmp/a.txt"));
    EXPECT_EQ(files[1], wxT("/tmp/b.txt"));

    // 上限超え
    for (int i = 0; i < 20; ++i) {
        c.AddRecentFile(wxString::Format(wxT("/tmp/x%d.txt"), i));
    }
    EXPECT_EQ(c.GetRecentFiles().GetCount(),
              static_cast<size_t>(SakuraConfig::kMaxRecentFiles));

    // クリーンアップ
    c.ClearRecentFiles();
    c.Save();
}

// === メタテーブル: String / Color round-trip (Phase 13b) ===
TEST(SakuraConfig, MetaStringAndColorRoundTrip) {
    {
        SakuraConfig c;
        c.SetMetaString(wxT("window"), wxT("m_szWindowCaptionActive"),
                        wxT("Sakura - $f"));
        c.SetMetaString(wxT("format"), wxT("m_szDateFormat"),
                        wxT("yyyy-MM-dd"));
        c.SetMetaColor (wxT("custom"), wxT("highlightColor"),
                        wxColour(128, 200, 64));
        c.Save();
    }
    {
        SakuraConfig c;
        c.Load();
        EXPECT_EQ(c.GetMetaString(wxT("window"), wxT("m_szWindowCaptionActive")),
                  wxT("Sakura - $f"));
        EXPECT_EQ(c.GetMetaString(wxT("format"), wxT("m_szDateFormat")),
                  wxT("yyyy-MM-dd"));
        wxColour col = c.GetMetaColor(wxT("custom"), wxT("highlightColor"));
        ASSERT_TRUE(col.IsOk());
        EXPECT_EQ(col.Red(),   128);
        EXPECT_EQ(col.Green(), 200);
        EXPECT_EQ(col.Blue(),   64);

        // 未設定キーは default
        EXPECT_EQ(c.GetMetaString(wxT("window"), wxT("nonexistent"),
                                  wxT("fallback")),
                  wxT("fallback"));
        EXPECT_FALSE(c.GetMetaColor(wxT("custom"), wxT("nonexistent")).IsOk());
    }
}

// === メタテーブル汎用ストア (Phase 12) ===
TEST(SakuraConfig, MetaStoreRoundTrip) {
    // 書き込み
    {
        SakuraConfig c;
        c.SetMetaBool(wxT("general"), wxT("m_bIsINSMode"),         false);
        c.SetMetaBool(wxT("general"), wxT("m_bIsFreeCursorMode"),  true);
        c.SetMetaInt (wxT("general"), wxT("m_nRepeatedScrollLineNum"), 7);
        c.SetMetaInt (wxT("general"), wxT("m_nCaretType"),         1);
        c.SetMetaBool(wxT("window"),  wxT("m_bDarkMode"),          true);
        c.SetMetaInt (wxT("window"),  wxT("m_nRulerHeight"),       20);
        c.SetMetaBool(wxT("edit"),    wxT("m_bAddCRLFWhenCopy"),   true);
        c.Save();
    }
    // 読み戻し
    {
        SakuraConfig c;
        c.Load();
        EXPECT_FALSE(c.GetMetaBool(wxT("general"), wxT("m_bIsINSMode"), true));
        EXPECT_TRUE (c.GetMetaBool(wxT("general"), wxT("m_bIsFreeCursorMode"), false));
        EXPECT_EQ   (c.GetMetaInt (wxT("general"), wxT("m_nRepeatedScrollLineNum"), 0), 7);
        EXPECT_EQ   (c.GetMetaInt (wxT("general"), wxT("m_nCaretType"), 0), 1);
        EXPECT_TRUE (c.GetMetaBool(wxT("window"),  wxT("m_bDarkMode"), false));
        EXPECT_EQ   (c.GetMetaInt (wxT("window"),  wxT("m_nRulerHeight"), 0), 20);
        EXPECT_TRUE (c.GetMetaBool(wxT("edit"),    wxT("m_bAddCRLFWhenCopy"), false));
        // 未設定キーは default を返す
        EXPECT_EQ   (c.GetMetaInt (wxT("general"), wxT("nonexistent"), 42), 42);
        EXPECT_TRUE (c.GetMetaBool(wxT("general"), wxT("nonexistent"), true));
    }
}
