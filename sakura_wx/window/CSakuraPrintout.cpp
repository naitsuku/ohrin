#include "CSakuraPrintout.h"

#include <wx/datetime.h>
#include <wx/dc.h>
#include <wx/font.h>
#include <wx/stc/stc.h>

namespace sakura_wx::ui {

namespace {
constexpr int kMarginMm = 15;  // ページ余白 (上下左右)
} // namespace

CSakuraPrintout::CSakuraPrintout(wxStyledTextCtrl* editor,
                                 const wxString& displayName)
    : wxPrintout(displayName)
    , m_editor(editor)
    , m_displayName(displayName)
{
}

void CSakuraPrintout::OnPreparePrinting() {
    if (!m_editor) {
        m_totalPages = 1;
        m_pageStartPositions = {0};
        return;
    }

    // FormatRange を計算用 (out=false) でループしてページ境界を割り出す
    int dcW, dcH;
    GetPageSizePixels(&dcW, &dcH);
    int ppiX, ppiY;
    GetPPIPrinter(&ppiX, &ppiY);

    int marginX = static_cast<int>(kMarginMm * ppiX / 25.4);
    int marginY = static_cast<int>(kMarginMm * ppiY / 25.4);

    // ヘッダ・フッタ用に上下にさらに余白
    int headerH = static_cast<int>(0.5 * ppiY);  // 0.5 inch
    int footerH = static_cast<int>(0.5 * ppiY);

    wxRect printArea(marginX, marginY + headerH,
                     dcW - 2 * marginX,
                     dcH - 2 * marginY - headerH - footerH);

    auto* dc = GetDC();
    if (!dc) {
        m_totalPages = 1;
        m_pageStartPositions = {0};
        return;
    }

    m_pageStartPositions.clear();
    m_pageStartPositions.push_back(0);

    int totalLen = m_editor->GetLength();
    int curPos = 0;
    while (curPos < totalLen) {
        // FormatRange の draw=false で次ページ開始位置を取得
        int nextPos = m_editor->FormatRange(/*doDraw=*/false,
            curPos, totalLen, dc, dc, printArea, printArea);
        if (nextPos <= curPos) break;  // 無限ループガード
        curPos = nextPos;
        if (curPos < totalLen) {
            m_pageStartPositions.push_back(curPos);
        }
    }
    m_totalPages = static_cast<int>(m_pageStartPositions.size());
    if (m_totalPages < 1) m_totalPages = 1;
}

bool CSakuraPrintout::HasPage(int page) {
    return page >= 1 && page <= m_totalPages;
}

void CSakuraPrintout::GetPageInfo(int* minPage, int* maxPage,
                                  int* pageFrom, int* pageTo) {
    *minPage = 1;
    *maxPage = m_totalPages;
    *pageFrom = 1;
    *pageTo = m_totalPages;
}

bool CSakuraPrintout::OnPrintPage(int page) {
    if (!m_editor || page < 1 || page > m_totalPages) return false;
    auto* dc = GetDC();
    if (!dc) return false;

    int dcW, dcH;
    GetPageSizePixels(&dcW, &dcH);
    int ppiX, ppiY;
    GetPPIPrinter(&ppiX, &ppiY);

    int marginX = static_cast<int>(kMarginMm * ppiX / 25.4);
    int marginY = static_cast<int>(kMarginMm * ppiY / 25.4);
    int headerH = static_cast<int>(0.5 * ppiY);
    int footerH = static_cast<int>(0.5 * ppiY);

    wxRect printArea(marginX, marginY + headerH,
                     dcW - 2 * marginX,
                     dcH - 2 * marginY - headerH - footerH);
    wxRect pageArea(0, 0, dcW, dcH);

    DrawHeaderFooter(dc, page, pageArea);

    int startPos = m_pageStartPositions[page - 1];
    int endPos   = (page < m_totalPages)
                    ? m_pageStartPositions[page]
                    : m_editor->GetLength();

    m_editor->FormatRange(/*doDraw=*/true,
        startPos, endPos, dc, dc, printArea, printArea);
    return true;
}

void CSakuraPrintout::DrawHeaderFooter(wxDC* dc, int page,
                                       const wxRect& pageRect) {
    wxFont oldFont = dc->GetFont();
    wxFont hf(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
              wxFONTWEIGHT_NORMAL);
    dc->SetFont(hf);
    dc->SetTextForeground(*wxBLACK);

    int ppiX, ppiY;
    GetPPIPrinter(&ppiX, &ppiY);
    int marginX = static_cast<int>(kMarginMm * ppiX / 25.4);
    int marginY = static_cast<int>(kMarginMm * ppiY / 25.4);

    // ヘッダ: 左 = ファイル名、右 = 日時
    wxString date = wxDateTime::Now().Format(wxT("%Y-%m-%d %H:%M"));
    int tw, th;
    dc->GetTextExtent(date, &tw, &th);

    int headerY = marginY;
    dc->DrawText(m_displayName, marginX, headerY);
    dc->DrawText(date, pageRect.GetRight() - marginX - tw, headerY);

    // ヘッダ下線
    dc->SetPen(wxPen(*wxBLACK, 1));
    int headerLineY = headerY + th + 4;
    dc->DrawLine(marginX, headerLineY,
                 pageRect.GetRight() - marginX, headerLineY);

    // フッタ: 中央にページ番号
    wxString footer = wxString::Format(wxT("- %d / %d -"), page, m_totalPages);
    dc->GetTextExtent(footer, &tw, &th);
    int footerY = pageRect.GetBottom() - marginY - th;
    dc->DrawText(footer, (pageRect.GetWidth() - tw) / 2, footerY);

    // フッタ上線
    int footerLineY = footerY - 4;
    dc->DrawLine(marginX, footerLineY,
                 pageRect.GetRight() - marginX, footerLineY);

    dc->SetFont(oldFont);
}

} // namespace sakura_wx::ui
