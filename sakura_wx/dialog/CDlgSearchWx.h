#pragma once
#include <wx/wx.h>

class CEditViewWx;
class wxStyledTextCtrl;
class wxCheckBox;
class wxTextCtrl;
class wxStaticText;

/**
 * CDlgSearchWx — モードレス検索／置換ダイアログ。
 *
 * Phase 2 で検索のみ実装、Phase 5 Task 5-1 で置換機能を追加。
 *   - 「次／前を検索」「すべてハイライト」
 *   - 「置換して次へ」「すべて置換」
 *   - 大小区別 / 単語単位 / 正規表現
 *   - 折り返し検索
 */
class CDlgSearchWx : public wxFrame {
public:
    /**
     * @param replaceFocus true なら起動時に置換欄にフォーカス（Ctrl+H 用）
     */
    CDlgSearchWx(wxWindow* parent, CEditViewWx* editor, bool replaceFocus = false);
    ~CDlgSearchWx() override = default;

private:
    void BuildUI();

    void OnFindNext(wxCommandEvent& event);
    void OnFindPrev(wxCommandEvent& event);
    void OnHighlightAll(wxCommandEvent& event);
    void OnReplace(wxCommandEvent& event);
    void OnReplaceAll(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event);
    void OnCloseWindow(wxCloseEvent& event);

    int  BuildSearchFlags() const;
    bool DoFind(bool forward);
    bool MatchesAtCurrentSelection() const;
    void ReportStatus(const wxString& msg);

    CEditViewWx*  m_editor    = nullptr;
    wxTextCtrl*   m_input     = nullptr;
    wxTextCtrl*   m_replInput = nullptr;
    wxCheckBox*   m_caseCB    = nullptr;
    wxCheckBox*   m_regexCB   = nullptr;
    wxCheckBox*   m_wholeCB   = nullptr;
    wxStaticText* m_status    = nullptr;

    bool m_focusReplace;

    enum {
        ID_FIND_NEXT = wxID_HIGHEST + 200,
        ID_FIND_PREV,
        ID_HIGHLIGHT_ALL,
        ID_REPLACE,
        ID_REPLACE_ALL,
    };

    wxDECLARE_EVENT_TABLE();
};
