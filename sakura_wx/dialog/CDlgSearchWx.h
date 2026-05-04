#pragma once
#include <wx/wx.h>

class CEditViewWx;
class wxStyledTextCtrl;
class wxCheckBox;
class wxTextCtrl;
class wxStaticText;

/**
 * CDlgSearchWx — モードレス検索ダイアログ（最小版）
 *
 * Phase 2 では「次／前を検索」「すべてハイライト」「大小区別」「正規表現」のみ。
 * 置換は Phase 4 で対応予定。
 */
class CDlgSearchWx : public wxFrame {
public:
    CDlgSearchWx(wxWindow* parent, CEditViewWx* editor);
    ~CDlgSearchWx() override = default;

private:
    void BuildUI();

    void OnFindNext(wxCommandEvent& event);
    void OnFindPrev(wxCommandEvent& event);
    void OnHighlightAll(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event);
    void OnCloseWindow(wxCloseEvent& event);

    int  BuildSearchFlags() const;
    void DoFind(bool forward);
    void ReportStatus(const wxString& msg);

    CEditViewWx*  m_editor    = nullptr;
    wxTextCtrl*   m_input     = nullptr;
    wxCheckBox*   m_caseCB    = nullptr;
    wxCheckBox*   m_regexCB   = nullptr;
    wxCheckBox*   m_wholeCB   = nullptr;
    wxStaticText* m_status    = nullptr;

    enum {
        ID_FIND_NEXT = wxID_HIGHEST + 200,
        ID_FIND_PREV,
        ID_HIGHLIGHT_ALL,
    };

    wxDECLARE_EVENT_TABLE();
};
