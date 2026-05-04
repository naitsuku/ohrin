#pragma once
#include <wx/bitmap.h>
#include <wx/string.h>
#include <wx/toolbar.h>

class CEditWndWx;

namespace sakura_wx::ui {

/**
 * CSakuraToolBar — オリジナル サクラエディタのアイコン strip
 * (src/main/resources/images/my_icons.bmp, 528×240, 16×16 グリッド)
 * を読み込んで wxToolBar として表示する。
 *
 * Phase 7 では主要 12 アイコンのみ使用。残りは Phase 8 以降で拡張。
 *
 * macOS 注: wxFrame::CreateToolBar() を使うと NSToolbar (タイトルバー統合の
 * 大アイコン) になる。Windows サクラ風の通常ツールバーストリップにするため、
 * wxToolBar を frame の通常子として直接生成し、呼び出し側で sizer 配置する。
 *
 * 実行時のアイコン探索順:
 *   1. macOS: <app>.app/Contents/Resources/my_icons.bmp
 *   2. Linux: <bin>/resources/my_icons.bmp
 *   3. 開発フォールバック: SAKURA_RESOURCE_SRCDIR/my_icons.bmp
 */
class CSakuraToolBar {
public:
    /**
     * frame の子として通常 wxToolBar を作成して返す。
     * 呼び出し側で sizer に Add すること（タイトル直下、タブの上に配置）。
     * NSToolbar / Unity HUD 等のネイティブ統合は使わない。
     */
    static wxToolBar* Build(CEditWndWx* frame);

private:
    static wxBitmap LoadIconStrip();
    static wxBitmap ExtractIcon(const wxBitmap& strip, int col, int row);
    static wxBitmap GetFallback(const wxString& artId);
};

} // namespace sakura_wx::ui
