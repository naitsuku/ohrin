#include "CSakuraMiniMap.h"
#include "view/CEditViewWx.h"

#include <wx/wx.h>

namespace sakura_wx::ui {

CSakuraMiniMap::CSakuraMiniMap(wxWindow* parent)
    : wxStyledTextCtrl(parent, wxID_ANY)
{
    // 表示専用設定
    SetReadOnly(true);
    SetMarginWidth(0, 0);
    SetMarginWidth(1, 0);
    SetMarginWidth(2, 0);
    SetUseHorizontalScrollBar(false);
    SetUseVerticalScrollBar(false);
    SetWrapMode(wxSTC_WRAP_NONE);

    // 小フォント (約 2-3pt 相当)
    StyleSetSize(wxSTC_STYLE_DEFAULT, 2);
    StyleClearAll();

    SetCaretPeriod(0);
    SetSelBackground(true, wxColour(180, 200, 230));

    Bind(wxEVT_LEFT_DOWN, &CSakuraMiniMap::OnSelfClick, this);
}

void CSakuraMiniMap::Attach(CEditViewWx* view) {
    if (m_main == view) return;
    Detach();
    m_main = view;
    if (!m_main) return;

    SyncTextFromMain();
    SyncFirstVisibleLine();

    m_main->Bind(wxEVT_STC_MODIFIED, &CSakuraMiniMap::OnMainModified, this);
    m_main->Bind(wxEVT_STC_UPDATEUI, &CSakuraMiniMap::OnMainUpdateUI, this);
}

void CSakuraMiniMap::Detach() {
    if (!m_main) return;
    m_main->Unbind(wxEVT_STC_MODIFIED, &CSakuraMiniMap::OnMainModified, this);
    m_main->Unbind(wxEVT_STC_UPDATEUI, &CSakuraMiniMap::OnMainUpdateUI, this);
    m_main = nullptr;
}

void CSakuraMiniMap::SyncTextFromMain() {
    if (!m_main) return;
    m_suppressSync = true;
    SetReadOnly(false);
    SetText(m_main->GetText());
    SetReadOnly(true);
    m_suppressSync = false;
}

void CSakuraMiniMap::SyncFirstVisibleLine() {
    if (!m_main) return;
    int line = m_main->GetFirstVisibleLine();
    int visibleLines = m_main->LinesOnScreen();
    int total = GetLineCount();
    // ミニマップは可能な限り全体を表示する (大きいファイルの場合はスクロール)
    if (total > 0) {
        int targetTop = line - visibleLines / 4;
        if (targetTop < 0) targetTop = 0;
        SetFirstVisibleLine(targetTop);
    }
}

void CSakuraMiniMap::OnMainModified(wxStyledTextEvent& event) {
    if (m_suppressSync) { event.Skip(); return; }
    const int mt = event.GetModificationType();
    if (mt & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) {
        SyncTextFromMain();
    }
    event.Skip();
}

void CSakuraMiniMap::OnMainUpdateUI(wxStyledTextEvent& event) {
    if (!m_suppressSync) SyncFirstVisibleLine();
    event.Skip();
}

void CSakuraMiniMap::OnSelfClick(wxMouseEvent& event) {
    if (!m_main) { event.Skip(); return; }
    int pos  = PositionFromPoint(event.GetPosition());
    int line = LineFromPosition(pos);
    m_main->SetFirstVisibleLine(line - m_main->LinesOnScreen() / 2);
    m_main->GotoLine(line);
    m_main->SetFocus();
    event.Skip();
}

} // namespace sakura_wx::ui
