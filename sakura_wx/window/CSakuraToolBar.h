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
 * 実行時のアイコン探索順:
 *   1. macOS: <app>.app/Contents/Resources/my_icons.bmp
 *   2. Linux: <bin>/resources/my_icons.bmp
 *   3. 開発フォールバック: SAKURA_RESOURCE_SRCDIR/my_icons.bmp
 */
class CSakuraToolBar {
public:
    /**
     * 新しい wxToolBar を frame に作成し、サクラのアイコンを並べて返す。
     * 失敗（アイコンファイルが見つからない）時は空のツールバーを返し、
     * UseFallbackArtIcons() で wxArt 標準アイコンに切り替えられる。
     */
    static wxToolBar* Build(CEditWndWx* frame);

private:
    static wxBitmap LoadIconStrip();
    static wxBitmap ExtractIcon(const wxBitmap& strip, int col, int row);
    static wxBitmap GetFallback(const wxString& artId);
};

} // namespace sakura_wx::ui
