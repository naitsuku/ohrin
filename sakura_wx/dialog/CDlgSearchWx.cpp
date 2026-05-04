#include "CDlgSearchWx.h"
#include "view/CEditViewWx.h"

#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>

wxBEGIN_EVENT_TABLE(CDlgSearchWx, wxFrame)
    EVT_BUTTON(ID_FIND_NEXT,     CDlgSearchWx::OnFindNext)
    EVT_BUTTON(ID_FIND_PREV,     CDlgSearchWx::OnFindPrev)
    EVT_BUTTON(ID_HIGHLIGHT_ALL, CDlgSearchWx::OnHighlightAll)
    EVT_BUTTON(wxID_CLOSE,       CDlgSearchWx::OnClose)
    EVT_CLOSE(                   CDlgSearchWx::OnCloseWindow)
wxEND_EVENT_TABLE()

CDlgSearchWx::CDlgSearchWx(wxWindow* parent, CEditViewWx* editor)
    : wxFrame(parent, wxID_ANY, wxT("検索"),
              wxDefaultPosition, wxSize(420, 200),
              wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX | wxMINIMIZE_BOX))
    , m_editor(editor)
{
    BuildUI();
    Layout();
    Centre();
    if (m_input) m_input->SetFocus();
}

void CDlgSearchWx::BuildUI() {
    auto* panel = new wxPanel(this);
    auto* topSizer = new wxBoxSizer(wxVERTICAL);

    // 検索文字列入力
    auto* inputSizer = new wxBoxSizer(wxHORIZONTAL);
    inputSizer->Add(new wxStaticText(panel, wxID_ANY, wxT("検索文字列:")),
                    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_input = new wxTextCtrl(panel, wxID_ANY, wxEmptyString,
                             wxDefaultPosition, wxSize(280, -1),
                             wxTE_PROCESS_ENTER);
    m_input->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { DoFind(true); });
    inputSizer->Add(m_input, 1, wxEXPAND);
    topSizer->Add(inputSizer, 0, wxEXPAND | wxALL, 8);

    // オプション
    auto* optsSizer = new wxBoxSizer(wxHORIZONTAL);
    m_caseCB  = new wxCheckBox(panel, wxID_ANY, wxT("大小区別"));
    m_wholeCB = new wxCheckBox(panel, wxID_ANY, wxT("単語単位"));
    m_regexCB = new wxCheckBox(panel, wxID_ANY, wxT("正規表現"));
    optsSizer->Add(m_caseCB,  0, wxRIGHT, 12);
    optsSizer->Add(m_wholeCB, 0, wxRIGHT, 12);
    optsSizer->Add(m_regexCB, 0, wxRIGHT, 12);
    topSizer->Add(optsSizer, 0, wxLEFT | wxRIGHT, 8);

    // ボタン
    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    btnSizer->Add(new wxButton(panel, ID_FIND_PREV,     wxT("前を検索")));
    btnSizer->Add(new wxButton(panel, ID_FIND_NEXT,     wxT("次を検索")), 0, wxLEFT, 6);
    btnSizer->Add(new wxButton(panel, ID_HIGHLIGHT_ALL, wxT("すべてハイライト")), 0, wxLEFT, 6);
    btnSizer->AddStretchSpacer(1);
    btnSizer->Add(new wxButton(panel, wxID_CLOSE,       wxT("閉じる")), 0, wxLEFT, 6);
    topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    // ステータス
    m_status = new wxStaticText(panel, wxID_ANY, wxT(""));
    topSizer->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    panel->SetSizerAndFit(topSizer);

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(panel, 1, wxEXPAND);
    SetSizerAndFit(outer);
}

int CDlgSearchWx::BuildSearchFlags() const {
    int flags = 0;
    if (m_caseCB && m_caseCB->IsChecked())   flags |= wxSTC_FIND_MATCHCASE;
    if (m_wholeCB && m_wholeCB->IsChecked()) flags |= wxSTC_FIND_WHOLEWORD;
    if (m_regexCB && m_regexCB->IsChecked()) flags |= wxSTC_FIND_REGEXP;
    return flags;
}

void CDlgSearchWx::DoFind(bool forward) {
    if (!m_editor) return;
    wxString needle = m_input->GetValue();
    if (needle.IsEmpty()) {
        ReportStatus(wxT("検索文字列を入力してください"));
        return;
    }

    int flags = BuildSearchFlags();
    int start, end;
    if (forward) {
        start = m_editor->GetCurrentPos();
        end   = m_editor->GetLength();
    } else {
        start = m_editor->GetCurrentPos() - 1;
        if (start < 0) start = 0;
        end   = 0;
    }

    m_editor->SetTargetStart(start);
    m_editor->SetTargetEnd(end);
    m_editor->SetSearchFlags(flags);
    int found = m_editor->SearchInTarget(needle);

    if (found < 0) {
        // 折り返し検索
        m_editor->SetTargetStart(forward ? 0 : m_editor->GetLength());
        m_editor->SetTargetEnd(forward ? m_editor->GetLength() : 0);
        found = m_editor->SearchInTarget(needle);
        if (found < 0) {
            ReportStatus(wxT("見つかりません"));
            return;
        }
        ReportStatus(wxT("先頭/末尾から再検索"));
    } else {
        ReportStatus(wxT(""));
    }

    int matchStart = m_editor->GetTargetStart();
    int matchEnd   = m_editor->GetTargetEnd();
    m_editor->SetSelection(matchStart, matchEnd);
    m_editor->EnsureCaretVisible();
}

void CDlgSearchWx::OnFindNext(wxCommandEvent&) { DoFind(true);  }
void CDlgSearchWx::OnFindPrev(wxCommandEvent&) { DoFind(false); }

void CDlgSearchWx::OnHighlightAll(wxCommandEvent&) {
    if (!m_editor) return;
    wxString needle = m_input->GetValue();
    if (needle.IsEmpty()) {
        ReportStatus(wxT("検索文字列を入力してください"));
        return;
    }

    // インジケータ #0 を「ハイライト」用に確保
    constexpr int kIndicator = 0;
    m_editor->IndicatorSetStyle(kIndicator, wxSTC_INDIC_ROUNDBOX);
    m_editor->IndicatorSetForeground(kIndicator, wxColour(255, 200, 0));
    m_editor->IndicatorSetAlpha(kIndicator, 80);
    m_editor->SetIndicatorCurrent(kIndicator);
    m_editor->IndicatorClearRange(0, m_editor->GetLength());

    int flags = BuildSearchFlags();
    m_editor->SetSearchFlags(flags);

    int from = 0;
    int hits = 0;
    while (true) {
        m_editor->SetTargetStart(from);
        m_editor->SetTargetEnd(m_editor->GetLength());
        int pos = m_editor->SearchInTarget(needle);
        if (pos < 0) break;
        int matchEnd = m_editor->GetTargetEnd();
        if (matchEnd <= pos) break;  // 0 幅マッチ（regex）の暴走防止
        m_editor->IndicatorFillRange(pos, matchEnd - pos);
        ++hits;
        from = matchEnd;
    }

    ReportStatus(wxString::Format(wxT("%d 件ハイライト"), hits));
}

void CDlgSearchWx::ReportStatus(const wxString& msg) {
    if (m_status) m_status->SetLabel(msg);
}

void CDlgSearchWx::OnClose(wxCommandEvent&) { Close(); }
void CDlgSearchWx::OnCloseWindow(wxCloseEvent& event) {
    event.Skip();
}
