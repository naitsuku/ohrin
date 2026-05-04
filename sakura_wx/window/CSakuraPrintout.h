#pragma once
#include <wx/print.h>
#include <wx/string.h>

#include <vector>

class wxStyledTextCtrl;

namespace sakura_wx::ui {

/**
 * CSakuraPrintout — wxStyledTextCtrl の内容を印刷するための wxPrintout 実装。
 *
 * Phase 8 では:
 *   - シンタックスハイライト色を反映した出力 (wxSTC::FormatRange を利用)
 *   - ヘッダ: ファイル名 (左), 日時 (右)
 *   - フッタ: ページ番号 / 総ページ
 *   - ページ余白固定 (上下左右 15mm 相当)
 *
 * 印刷プレビューは wxPrintPreview + wxPreviewFrame で表示。
 */
class CSakuraPrintout : public wxPrintout {
public:
    CSakuraPrintout(wxStyledTextCtrl* editor,
                    const wxString& displayName);

    bool OnPrintPage(int page) override;
    void OnPreparePrinting() override;
    bool HasPage(int page) override;
    void GetPageInfo(int* minPage, int* maxPage,
                     int* pageFrom, int* pageTo) override;

private:
    void DrawHeaderFooter(wxDC* dc, int page,
                          const wxRect& pageRect);

    wxStyledTextCtrl* m_editor;
    wxString          m_displayName;
    std::vector<int>  m_pageStartPositions; // 各ページの開始 char position
    int               m_totalPages = 1;
};

} // namespace sakura_wx::ui
