#include "CDlgSettingsWx.h"
#include "config/SakuraConfig.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clrpicker.h>
#include <wx/fontpicker.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

namespace sakura_wx::dialog {

wxBEGIN_EVENT_TABLE(CDlgSettingsWx, wxDialog)
    EVT_BUTTON(wxID_OK, CDlgSettingsWx::OnOk)
    EVT_BUTTON(wxID_RESET, CDlgSettingsWx::OnColorReset)
wxEND_EVENT_TABLE()

namespace {
// タブのタイプ名 (UI 表示) と config キー名のペア
struct TypeEntry { const wxChar* label; const wxChar* key; };
const TypeEntry kTypes[] = {
    { wxT("C / C++"),  wxT("cpp")     },
    { wxT("Python"),   wxT("py")      },
    { wxT("Markdown"), wxT("md")      },
    { wxT("既定"),     wxT("default") },
};

// 各タイプ kind のサクラ初期色 (CEditViewWx と同じ値)
wxColour DefaultColor(const wxString& /*type*/, const wxString& kind) {
    if (kind == wxT("comment")) return wxColour(  0, 128,   0);
    if (kind == wxT("string"))  return wxColour(128,   0,  64);
    if (kind == wxT("keyword")) return wxColour(  0,   0, 255);
    if (kind == wxT("number"))  return wxColour(235,   0,   0);
    return *wxBLACK;
}
} // namespace

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

    // タブ 4: タイプ別シンタックス色 (Phase 8 追加)
    auto* pageTypes = new wxPanel(m_notebook);
    BuildTabTypes(pageTypes);
    m_notebook->AddPage(pageTypes, wxT("タイプ別"));

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

void CDlgSettingsWx::BuildTabTypes(wxPanel* page) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // タイプ選択
    auto* typeRow = new wxBoxSizer(wxHORIZONTAL);
    typeRow->Add(new wxStaticText(page, wxID_ANY, wxT("タイプ:")), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    wxArrayString choices;
    for (auto& t : kTypes) choices.Add(t.label);
    m_typeChoice = new wxChoice(page, wxID_ANY,
                                wxDefaultPosition, wxDefaultSize, choices);
    m_typeChoice->SetSelection(0);
    m_typeChoice->Bind(wxEVT_CHOICE,
        [this](wxCommandEvent&) { RefreshColorPickers(); });
    typeRow->Add(m_typeChoice, 1, wxEXPAND);
    sizer->Add(typeRow, 0, wxEXPAND | wxALL, 8);

    // 4 色のピッカー
    auto addRow = [&](const wxString& label, wxColourPickerCtrl*& outPicker) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(page, wxID_ANY, label,
                                  wxDefaultPosition, wxSize(120, -1)),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        outPicker = new wxColourPickerCtrl(page, wxID_ANY);
        row->Add(outPicker);
        sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    };
    addRow(wxT("コメント:"), m_pkComment);
    addRow(wxT("文字列:"),   m_pkString);
    addRow(wxT("キーワード:"),m_pkKeyword);
    addRow(wxT("数値:"),     m_pkNumber);

    sizer->AddStretchSpacer(1);

    auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->AddStretchSpacer(1);
    btnRow->Add(new wxButton(page, wxID_RESET, wxT("初期値に戻す")));
    sizer->Add(btnRow, 0, wxEXPAND | wxALL, 8);

    auto* note = new wxStaticText(page, wxID_ANY,
        wxT("色の変更は OK 押下後、新規タブまたはファイルを開き直すと反映されます。"));
    note->Wrap(500);
    sizer->Add(note, 0, wxALL, 8);

    page->SetSizer(sizer);
    RefreshColorPickers();
}

void CDlgSettingsWx::RefreshColorPickers() {
    if (!m_typeChoice || !m_pkComment) return;
    int sel = m_typeChoice->GetSelection();
    if (sel < 0 || sel >= static_cast<int>(std::size(kTypes))) sel = 0;
    wxString typeKey = kTypes[sel].key;

    auto getOrDefault = [&](const wxString& kind) -> wxColour {
        if (m_config) {
            wxColour c = m_config->GetSyntaxColor(typeKey, kind);
            if (c.IsOk()) return c;
        }
        return DefaultColor(typeKey, kind);
    };

    m_pkComment->SetColour(getOrDefault(wxT("comment")));
    m_pkString ->SetColour(getOrDefault(wxT("string")));
    m_pkKeyword->SetColour(getOrDefault(wxT("keyword")));
    m_pkNumber ->SetColour(getOrDefault(wxT("number")));
}

void CDlgSettingsWx::OnTypeChange(wxCommandEvent&) {
    RefreshColorPickers();
}

void CDlgSettingsWx::OnColorReset(wxCommandEvent&) {
    if (!m_typeChoice || !m_pkComment) return;
    int sel = m_typeChoice->GetSelection();
    if (sel < 0 || sel >= static_cast<int>(std::size(kTypes))) sel = 0;
    wxString typeKey = kTypes[sel].key;

    m_pkComment->SetColour(DefaultColor(typeKey, wxT("comment")));
    m_pkString ->SetColour(DefaultColor(typeKey, wxT("string")));
    m_pkKeyword->SetColour(DefaultColor(typeKey, wxT("keyword")));
    m_pkNumber ->SetColour(DefaultColor(typeKey, wxT("number")));

    // 設定ストアからもクリア (デフォルトに戻す)
    if (m_config) {
        m_config->SetSyntaxColor(typeKey, wxT("comment"), wxNullColour);
        m_config->SetSyntaxColor(typeKey, wxT("string"),  wxNullColour);
        m_config->SetSyntaxColor(typeKey, wxT("keyword"), wxNullColour);
        m_config->SetSyntaxColor(typeKey, wxT("number"),  wxNullColour);
    }
}

void CDlgSettingsWx::OnOk(wxCommandEvent& event) {
    if (m_config) {
        if (m_cbLineNumbers) m_config->SetShowLineNumbers(m_cbLineNumbers->GetValue());
        if (m_cbWordWrap)    m_config->SetWordWrap(m_cbWordWrap->GetValue());
        if (m_spTabWidth)    m_config->SetTabWidth(m_spTabWidth->GetValue());
        if (m_fontPicker)    m_config->SetEditorFont(m_fontPicker->GetSelectedFont());

        // タイプ別シンタックス色: 現在選択中のタイプの 4 色を保存
        if (m_typeChoice && m_pkComment) {
            int sel = m_typeChoice->GetSelection();
            if (sel >= 0 && sel < static_cast<int>(std::size(kTypes))) {
                wxString typeKey = kTypes[sel].key;
                m_config->SetSyntaxColor(typeKey, wxT("comment"),
                                         m_pkComment->GetColour());
                m_config->SetSyntaxColor(typeKey, wxT("string"),
                                         m_pkString->GetColour());
                m_config->SetSyntaxColor(typeKey, wxT("keyword"),
                                         m_pkKeyword->GetColour());
                m_config->SetSyntaxColor(typeKey, wxT("number"),
                                         m_pkNumber->GetColour());
            }
        }
        m_config->Save();
    }
    event.Skip();
}

} // namespace sakura_wx::dialog
