#include "CDlgSettingsWx.h"
#include "config/SakuraConfig.h"

#include <wx/checkbox.h>
#include <wx/fontpicker.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace sakura_wx::dialog {

wxBEGIN_EVENT_TABLE(CDlgSettingsWx, wxDialog)
    EVT_BUTTON(wxID_OK, CDlgSettingsWx::OnOk)
wxEND_EVENT_TABLE()

CDlgSettingsWx::CDlgSettingsWx(wxWindow* parent, SakuraConfig* config)
    : wxDialog(parent, wxID_ANY, wxT("設定"),
               wxDefaultPosition, wxSize(540, 400),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_config(config)
{
    BuildUI();
    Centre();
}

void CDlgSettingsWx::BuildUI() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_notebook = new wxNotebook(this, wxID_ANY);

    // タブ 1: 全般
    auto* pageGen = new wxPanel(m_notebook);
    BuildTabGeneral(pageGen);
    m_notebook->AddPage(pageGen, wxT("全般"));

    // タブ 2: フォント
    auto* pageFont = new wxPanel(m_notebook);
    BuildTabFont(pageFont);
    m_notebook->AddPage(pageFont, wxT("フォント"));

    // タブ 3: キーバインド一覧
    auto* pageKey = new wxPanel(m_notebook);
    BuildTabKeybind(pageKey);
    m_notebook->AddPage(pageKey, wxT("キーバインド"));

    outer->Add(m_notebook, 1, wxEXPAND | wxALL, 8);

    auto* btnSizer = CreateButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) outer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    SetSizer(outer);
}

void CDlgSettingsWx::BuildTabGeneral(wxPanel* page) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    m_cbLineNumbers = new wxCheckBox(page, wxID_ANY, wxT("行番号を表示"));
    m_cbWordWrap    = new wxCheckBox(page, wxID_ANY, wxT("折り返しを有効化"));
    if (m_config) {
        m_cbLineNumbers->SetValue(m_config->GetShowLineNumbers());
        m_cbWordWrap->SetValue(m_config->GetWordWrap());
    }
    sizer->Add(m_cbLineNumbers, 0, wxALL, 8);
    sizer->Add(m_cbWordWrap,    0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* tabRow = new wxBoxSizer(wxHORIZONTAL);
    tabRow->Add(new wxStaticText(page, wxID_ANY, wxT("タブ幅:")), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_spTabWidth = new wxSpinCtrl(page, wxID_ANY,
                                  wxString::Format(wxT("%d"),
                                      m_config ? m_config->GetTabWidth() : 4),
                                  wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, 1, 16,
                                  m_config ? m_config->GetTabWidth() : 4);
    tabRow->Add(m_spTabWidth);
    sizer->Add(tabRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    sizer->AddStretchSpacer(1);

    auto* note = new wxStaticText(page, wxID_ANY,
        wxT("注: ここの設定は新規ウィンドウから反映されます。\n"
            "現在のウィンドウへの即時反映は Phase 8 で対応予定。"));
    note->Wrap(500);
    sizer->Add(note, 0, wxALL, 8);

    page->SetSizer(sizer);
}

void CDlgSettingsWx::BuildTabFont(wxPanel* page) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(page, wxID_ANY, wxT("エディタフォント:")),
               0, wxALL, 8);
    wxFont initial = m_config ? m_config->GetEditorFont() : wxFont();
    m_fontPicker = new wxFontPickerCtrl(page, wxID_ANY, initial,
        wxDefaultPosition, wxDefaultSize,
        wxFNTP_FONTDESC_AS_LABEL | wxFNTP_USEFONT_FOR_LABEL);
    sizer->Add(m_fontPicker, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    sizer->AddStretchSpacer(1);
    auto* note = new wxStaticText(page, wxID_ANY,
        wxT("等幅 (monospace) フォントを推奨します。\n"
            "macOS: Menlo / Osaka-Mono、Linux: Noto Sans Mono CJK JP / DejaVu Sans Mono"));
    note->Wrap(500);
    sizer->Add(note, 0, wxALL, 8);
    page->SetSizer(sizer);
}

void CDlgSettingsWx::BuildTabKeybind(wxPanel* page) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(page, wxID_ANY,
        wxT("Phase 7 時点の主要キーバインド一覧（参照のみ）:")),
        0, wxALL, 8);

    wxArrayString items;
    items.Add(wxT("Ctrl+N           新規"));
    items.Add(wxT("Ctrl+O           開く"));
    items.Add(wxT("Ctrl+S           上書き保存"));
    items.Add(wxT("Ctrl+W           タブを閉じる"));
    items.Add(wxT("Ctrl+Z / Ctrl+Shift+Z   元に戻す / やり直し"));
    items.Add(wxT("Ctrl+X / Ctrl+C / Ctrl+V    切り取り / コピー / 貼り付け"));
    items.Add(wxT("Ctrl+A           すべて選択"));
    items.Add(wxT("Ctrl+F / Ctrl+H  検索 / 置換"));
    items.Add(wxT("Ctrl+G           Grep（フォルダ検索）"));
    items.Add(wxT("F2               ブックマーク 切替"));
    items.Add(wxT("F3 / Shift+F3    次のブックマーク / 前のブックマーク"));

    auto* lb = new wxListBox(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, items,
                             wxLB_SINGLE | wxLB_HSCROLL);
    sizer->Add(lb, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* note = new wxStaticText(page, wxID_ANY,
        wxT("カスタマイズ機能は Phase 8 以降で追加予定。"));
    sizer->Add(note, 0, wxALL, 8);
    page->SetSizer(sizer);
}

void CDlgSettingsWx::OnOk(wxCommandEvent& event) {
    if (m_config) {
        if (m_cbLineNumbers) m_config->SetShowLineNumbers(m_cbLineNumbers->GetValue());
        if (m_cbWordWrap)    m_config->SetWordWrap(m_cbWordWrap->GetValue());
        if (m_spTabWidth)    m_config->SetTabWidth(m_spTabWidth->GetValue());
        if (m_fontPicker)    m_config->SetEditorFont(m_fontPicker->GetSelectedFont());
        m_config->Save();
    }
    event.Skip();  // Default OK handling will close dialog with wxID_OK
}

} // namespace sakura_wx::dialog
