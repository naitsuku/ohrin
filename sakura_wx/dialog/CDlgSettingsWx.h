#pragma once
#include <wx/dialog.h>
#include <wx/notebook.h>

class SakuraConfig;
class wxFontPickerCtrl;
class wxSpinCtrl;
class wxCheckBox;
class wxListBox;

namespace sakura_wx::dialog {

/**
 * CDlgSettingsWx — 設定ダイアログ（基本タブのみ）。
 *
 * Phase 7 では「全般」「フォント」「キーバインド一覧（参照のみ）」の 3 タブ。
 * タイプ別シンタックス設定は Phase 8 で別ダイアログとして追加予定。
 *
 * モーダル動作：OK 時に SakuraConfig を更新して Save し、メインウィンドウへ
 * 反映を促す。Cancel 時は破棄。
 */
class CDlgSettingsWx : public wxDialog {
public:
    CDlgSettingsWx(wxWindow* parent, SakuraConfig* config);

private:
    void BuildUI();
    void BuildTabGeneral(wxPanel* page);
    void BuildTabFont(wxPanel* page);
    void BuildTabKeybind(wxPanel* page);

    void OnOk(wxCommandEvent& event);

    SakuraConfig*    m_config = nullptr;
    wxNotebook*      m_notebook = nullptr;

    // 全般タブ
    wxCheckBox*      m_cbLineNumbers = nullptr;
    wxCheckBox*      m_cbWordWrap    = nullptr;
    wxSpinCtrl*      m_spTabWidth    = nullptr;

    // フォントタブ
    wxFontPickerCtrl* m_fontPicker = nullptr;

    wxDECLARE_EVENT_TABLE();
};

} // namespace sakura_wx::dialog
