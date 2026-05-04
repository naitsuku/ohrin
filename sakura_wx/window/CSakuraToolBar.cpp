#include "CSakuraToolBar.h"
#include "CEditWndWx.h"

#include <wx/artprov.h>
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

constexpr int kCellSize  = 16;  // 1 アイコンの一辺ピクセル
constexpr int kStripCols = 33;  // 528 / 16
constexpr int kStripRows = 15;  // 240 / 16

#ifndef SAKURA_RESOURCE_SRCDIR
#define SAKURA_RESOURCE_SRCDIR ""
#endif

fs::path FindIconStripPath() {
    auto exeWx = wxStandardPaths::Get().GetExecutablePath();
    fs::path exePath(exeWx.utf8_string());
    std::error_code ec;

    // 1) macOS .app バンドル: <exe>/../Resources/
#if defined(__APPLE__)
    fs::path appResources = exePath.parent_path() / ".." / "Resources" / "my_icons.bmp";
    if (fs::exists(appResources, ec)) {
        return fs::weakly_canonical(appResources, ec);
    }
#endif

    // 2) <bin>/resources/
    fs::path nextToBin = exePath.parent_path() / "resources" / "my_icons.bmp";
    if (fs::exists(nextToBin, ec)) return nextToBin;

    // 3) 開発時フォールバック（CMake で焼き込んだ src/ パス）
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
        wxLogWarning(wxT("my_icons.bmp の読み込みに失敗しました: %s"),
                     p.string());
        return wxBitmap();
    }
    if (img.GetWidth() != kCellSize * kStripCols
        || img.GetHeight() != kCellSize * kStripRows) {
        wxLogWarning(wxT("my_icons.bmp サイズが想定外: %dx%d"),
                     img.GetWidth(), img.GetHeight());
    }
    // 8bpp BMP の透過: 左上ピクセルを透過色に設定（オリジナル方式）
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
    // 通常の wxToolBar を frame の子として作成。
    // wxFrame::CreateToolBar() は macOS で NSToolbar（タイトルバー統合の
    // 大アイコン）になってしまうため使わない。呼び出し側で sizer に Add する。
    auto* tb = new wxToolBar(frame, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    tb->SetToolBitmapSize(wxSize(kCellSize, kCellSize));

    wxBitmap strip = LoadIconStrip();
    auto pick = [&](int col, int row, const wxString& artFallback) -> wxBitmap {
        if (strip.IsOk()) return ExtractIcon(strip, col, row);
        return GetFallback(artFallback);
    };

    // Phase 7: 主要 12 アイコンを並べる。
    // 列インデックスはオリジナル MainMenu.ini を読まずに「最初の 1 行 33 アイコン」
    // からの推測。間違っていれば Phase 8 で MainMenu.ini をパースして正規化する。
    tb->AddTool(wxID_NEW,    wxT("新規"),         pick( 0, 0, wxART_NEW),         wxT("新規ファイル"));
    tb->AddTool(wxID_OPEN,   wxT("開く"),         pick( 1, 0, wxART_FILE_OPEN),   wxT("ファイルを開く"));
    tb->AddTool(wxID_SAVE,   wxT("保存"),         pick( 2, 0, wxART_FILE_SAVE),   wxT("上書き保存"));
    tb->AddTool(wxID_SAVEAS, wxT("名前を付けて"), pick( 3, 0, wxART_FILE_SAVE_AS),wxT("名前を付けて保存"));
    tb->AddSeparator();
    tb->AddTool(wxID_UNDO,   wxT("元に戻す"),     pick( 4, 0, wxART_UNDO),        wxT("元に戻す"));
    tb->AddTool(wxID_REDO,   wxT("やり直し"),     pick( 5, 0, wxART_REDO),        wxT("やり直し"));
    tb->AddSeparator();
    tb->AddTool(wxID_CUT,    wxT("切り取り"),     pick( 6, 0, wxART_CUT),         wxT("切り取り"));
    tb->AddTool(wxID_COPY,   wxT("コピー"),       pick( 7, 0, wxART_COPY),        wxT("コピー"));
    tb->AddTool(wxID_PASTE,  wxT("貼り付け"),     pick( 8, 0, wxART_PASTE),       wxT("貼り付け"));
    tb->AddSeparator();
    tb->AddTool(wxID_FIND,    wxT("検索"),        pick( 9, 0, wxART_FIND),        wxT("検索"));
    tb->AddTool(wxID_REPLACE, wxT("置換"),        pick(10, 0, wxART_FIND_AND_REPLACE), wxT("置換"));

    tb->Realize();
    return tb;
}

} // namespace sakura_wx::ui
