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
