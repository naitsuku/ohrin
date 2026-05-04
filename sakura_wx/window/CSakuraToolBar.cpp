#include "CSakuraToolBar.h"
#include "CEditWndWx.h"

#include <wx/artprov.h>
#include <wx/aui/aui.h>
#include <wx/bitmap.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/toolbar.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace sakura_wx::ui {

namespace {

constexpr int kCellSize  = 16;
constexpr int kStripCols = 33;
constexpr int kStripRows = 15;

#ifndef SAKURA_RESOURCE_SRCDIR
#define SAKURA_RESOURCE_SRCDIR ""
#endif

fs::path FindIconStripPath() {
    auto exeWx = wxStandardPaths::Get().GetExecutablePath();
    fs::path exePath(exeWx.utf8_string());
    std::error_code ec;

#if defined(__APPLE__)
    fs::path appResources = exePath.parent_path() / ".." / "Resources" / "my_icons.bmp";
    if (fs::exists(appResources, ec)) {
        return fs::weakly_canonical(appResources, ec);
    }
#endif

    fs::path nextToBin = exePath.parent_path() / "resources" / "my_icons.bmp";
    if (fs::exists(nextToBin, ec)) return nextToBin;

    if (std::strlen(SAKURA_RESOURCE_SRCDIR) > 0) {
        fs::path dev = fs::path(SAKURA_RESOURCE_SRCDIR) / "my_icons.bmp";
        std::error_code ec3;
        if (fs::exists(dev, ec3)) return dev;
    }
    return {};
}

} // namespace

wxBitmap CSakuraToolBar::LoadIconStrip() {
    auto p = FindIconStripPath();
    if (p.empty()) {
        wxLogWarning(wxT("ツールバーアイコン strip が見つかりません"));
        return wxBitmap();
    }
    wxImage img;
    if (!img.LoadFile(wxString::FromUTF8(p.string()), wxBITMAP_TYPE_BMP)) {
        wxLogWarning(wxT("my_icons.bmp の読み込みに失敗しました: %s"), p.string());
        return wxBitmap();
    }
    if (img.GetWidth() != kCellSize * kStripCols
        || img.GetHeight() != kCellSize * kStripRows) {
        wxLogWarning(wxT("my_icons.bmp サイズが想定外: %dx%d"),
                     img.GetWidth(), img.GetHeight());
    }
    if (img.GetWidth() > 0 && img.GetHeight() > 0) {
        img.SetMaskColour(img.GetRed(0, 0), img.GetGreen(0, 0), img.GetBlue(0, 0));
    }
    return wxBitmap(img);
}

wxBitmap CSakuraToolBar::ExtractIcon(const wxBitmap& strip, int col, int row) {
    if (!strip.IsOk()) return GetFallback(wxART_HELP);
    wxRect r(col * kCellSize, row * kCellSize, kCellSize, kCellSize);
    if (r.x + r.width > strip.GetWidth() || r.y + r.height > strip.GetHeight()) {
        return GetFallback(wxART_HELP);
    }
    return strip.GetSubBitmap(r);
}

wxBitmap CSakuraToolBar::GetFallback(const wxString& artId) {
    return wxArtProvider::GetBitmap(artId, wxART_TOOLBAR, wxSize(16, 16));
}

wxToolBar* CSakuraToolBar::Build(CEditWndWx* frame) {
    if (!frame) return nullptr;
    // 通常 wxToolBar を frame の子として作成 (NSToolbar 統合は使わない)
    auto* tb = new wxToolBar(frame, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    tb->SetToolBitmapSize(wxSize(kCellSize, kCellSize));

    wxBitmap strip = LoadIconStrip();

    // ============================================================
    // Phase 9 (b): upstream の sakura_core/uiparts/CMenuDrawer.cpp tbd[]
    // 配列に基づく正規 index → BMP セル位置マッピング。
    //   tbd[] は 1-based、各段 32 個。
    //   index N → row = (N-1) / 32, col = (N-1) % 32
    //
    // 並び順は upstream DEFAULT_TOOL_BUTTONS
    // (sakura_core/env/CShareData.cpp:1189) を踏襲。
    // 未実装の F_OUTLINE (232) / F_OPTION_TYPE (265) / F_TOOLOPTION (266 共通設定)
    // 等のうち、共通設定 266 はこちらでの設定ダイアログにマップ。
    // Cut/Copy/Paste は upstream 初期ツールバーには無いが、UX 用に末尾追加。
    // ============================================================

    auto pickIdx = [&](int idx, const wxString& art) -> wxBitmap {
        int row = (idx - 1) / 32;
        int col = (idx - 1) % 32;
        if (strip.IsOk()) return ExtractIcon(strip, col, row);
        return GetFallback(art);
    };

    // ファイル系 (upstream tbd index 1-4)
    tb->AddTool(wxID_NEW,    wxT("新規"),
                pickIdx(  1, wxART_NEW),         wxT("新規ファイル (Ctrl+N)"));
    tb->AddTool(wxID_OPEN,   wxT("開く"),
                pickIdx(  2, wxART_FILE_OPEN),   wxT("ファイルを開く (Ctrl+O)"));
    tb->AddTool(wxID_SAVE,   wxT("保存"),
                pickIdx(  3, wxART_FILE_SAVE),   wxT("上書き保存 (Ctrl+S)"));
    tb->AddTool(wxID_SAVEAS, wxT("名前を付けて保存"),
                pickIdx(  4, wxART_FILE_SAVE_AS),wxT("名前を付けて保存"));
    tb->AddSeparator();

    // 編集 (Undo/Redo, upstream tbd index 33/34)
    tb->AddTool(wxID_UNDO,   wxT("元に戻す"),
                pickIdx( 33, wxART_UNDO),        wxT("元に戻す (Ctrl+Z)"));
    tb->AddTool(wxID_REDO,   wxT("やり直し"),
                pickIdx( 34, wxART_REDO),        wxT("やり直し (Ctrl+Shift+Z)"));
    tb->AddSeparator();

    // 検索系 (upstream tbd index 225-230)
    tb->AddTool(wxID_FIND,             wxT("検索"),
                pickIdx(225, wxART_FIND),        wxT("検索 (Ctrl+F)"));
    tb->AddTool(ID_SEARCH_FIND_NEXT,   wxT("次を検索"),
                pickIdx(226, wxART_GO_FORWARD),  wxT("次を検索 (F3)"));
    tb->AddTool(ID_SEARCH_FIND_PREV,   wxT("前を検索"),
                pickIdx(227, wxART_GO_BACK),     wxT("前を検索 (Shift+F3)"));
    tb->AddTool(wxID_REPLACE,          wxT("置換"),
                pickIdx(228, wxART_FIND_AND_REPLACE),
                wxT("置換 (Ctrl+H)"));
    tb->AddTool(ID_SEARCH_CLEAR_MARKS, wxT("検索クリア"),
                pickIdx(229, wxART_DELETE),      wxT("検索ハイライトをクリア"));
    tb->AddTool(ID_SEARCH_GREP,        wxT("Grep"),
                pickIdx(230, wxART_FIND),        wxT("Grep (Ctrl+G)"));
    tb->AddSeparator();

    // ブックマーク (upstream tbd index 237-239)
    tb->AddTool(ID_BOOKMARK_TOGGLE, wxT("ブックマーク 切替"),
                pickIdx(237, wxART_TICK_MARK),   wxT("ブックマーク 切替 (Ctrl+F2)"));
    tb->AddTool(ID_BOOKMARK_NEXT,   wxT("次のブックマーク"),
                pickIdx(238, wxART_GO_DOWN),     wxT("次のブックマーク (F2)"));
    tb->AddTool(ID_BOOKMARK_PREV,   wxT("前のブックマーク"),
                pickIdx(239, wxART_GO_UP),       wxT("前のブックマーク (Shift+F2)"));
    tb->AddSeparator();

    // 設定 (upstream tbd index 266: 共通設定)
    tb->AddTool(wxID_PREFERENCES, wxT("設定"),
                pickIdx(266, wxART_HELP_SETTINGS),
                wxT("設定..."));
    tb->AddSeparator();

    // クリップボード (upstream 初期ツールバーには無いが UX 用に追加)
    tb->AddTool(wxID_CUT,   wxT("切り取り"), GetFallback(wxART_CUT),   wxT("切り取り (Ctrl+X)"));
    tb->AddTool(wxID_COPY,  wxT("コピー"),   GetFallback(wxART_COPY),  wxT("コピー (Ctrl+C)"));
    tb->AddTool(wxID_PASTE, wxT("貼り付け"), GetFallback(wxART_PASTE), wxT("貼り付け (Ctrl+V)"));

    tb->Realize();
    return tb;
}

} // namespace sakura_wx::ui
