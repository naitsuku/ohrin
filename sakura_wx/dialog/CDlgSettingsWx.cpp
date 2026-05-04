#include "CDlgSettingsWx.h"
#include "config/SakuraConfig.h"
#include "env/SettingsMeta.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clrpicker.h>
#include <wx/fontpicker.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/statline.h>

namespace sakura_wx::dialog {

wxBEGIN_EVENT_TABLE(CDlgSettingsWx, wxDialog)
    EVT_BUTTON(wxID_OK,    CDlgSettingsWx::OnOk)
    EVT_BUTTON(wxID_RESET, CDlgSettingsWx::OnColorReset)
wxEND_EVENT_TABLE()

namespace {
struct TypeEntry { const wxChar* label; const wxChar* key; };
const TypeEntry kTypes[] = {
    { wxT("C / C++"),  wxT("cpp")     },
    { wxT("Python"),   wxT("py")      },
    { wxT("Markdown"), wxT("md")      },
    { wxT("既定"),     wxT("default") },
};

wxColour DefaultColor(const wxString& /*type*/, const wxString& kind) {
    if (kind == wxT("comment")) return wxColour(  0, 128,   0);
    if (kind == wxT("string"))  return wxColour(128,   0,  64);
    if (kind == wxT("keyword")) return wxColour(  0,   0, 255);
    if (kind == wxT("number"))  return wxColour(235,   0,   0);
    return *wxBLACK;
}
} // namespace

CDlgSettingsWx::CDlgSettingsWx(wxWindow* parent, SakuraConfig* config)
    : wxDialog(parent, wxID_ANY, wxT("共通設定"),
               wxDefaultPosition, wxSize(640, 480),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_config(config)
{
    BuildUI();
    Centre();
}

void CDlgSettingsWx::BuildUI() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    m_notebook = new wxNotebook(this, wxID_ANY);

    // ============================================================
    // upstream sakura_core/prop/CPropCommon.cpp:193-211 のタブ順を踏襲。
    // 未実装タブは BuildTabPlaceholder で「Phase X で対応予定」を表示。
    // ============================================================

    auto addPlaceholder = [&](const wxString& title, const wxString& note) {
        auto* page = new wxPanel(m_notebook);
        BuildTabPlaceholder(page, title, note);
        m_notebook->AddPage(page, title);
    };
    auto addPanel = [&](const wxString& title,
                        void (CDlgSettingsWx::*builder)(wxPanel*)) {
        auto* page = new wxPanel(m_notebook);
        (this->*builder)(page);
        m_notebook->AddPage(page, title);
    };

    auto addMetaPanel = [&](const wxString& title, const wxString& tabId) {
        auto* page = new wxPanel(m_notebook);
        BuildTabFromMeta(page, tabId);
        m_notebook->AddPage(page, title);
    };

    addPanel(wxT("全般"),         &CDlgSettingsWx::BuildTabGeneral);          // 1
    addMetaPanel(wxT("ウィンドウ"), wxT("window"));                              // 2 (Phase 12 メタ化)
    addPlaceholder(wxT("メインメニュー"),
                   wxT("メインメニューのカスタマイズ。Phase 10 以降で対応予定。"));         // 3
    addPlaceholder(wxT("ツールバー"),
                   wxT("ツールバー項目の選択。Phase 10 以降で対応予定。\n"
                       "現在は upstream DEFAULT_TOOL_BUTTONS 準拠の固定配列。"));            // 4
    addMetaPanel(wxT("タブバー"), wxT("tabbar"));                                 // 5 (Phase 13a メタ化)
    addPlaceholder(wxT("ステータスバー"),
                   wxT("ステータスバーの表示項目。Phase 10 以降で対応予定。"));             // 6
    addMetaPanel(wxT("編集"), wxT("edit"));                                       // 7 (Phase 12 メタ化)
    addPlaceholder(wxT("ファイル"),
                   wxT("既定の文字コード／改行コード／BOM 設定。Phase 10 以降で対応予定。"));// 8
    addMetaPanel(wxT("ファイル名表示"), wxT("filename"));                         // 9 (Phase 13a メタ化)
    addMetaPanel(wxT("バックアップ"),   wxT("backup"));                            // 10 (Phase 13a メタ化)
    addMetaPanel(wxT("書式"),           wxT("format"));                            // 11 (Phase 13a メタ化)
    addPlaceholder(wxT("検索"),
                   wxT("検索／Grep のデフォルト設定。Phase 10 以降で対応予定。"));          // 12
    addPanel(wxT("キー割り当て"),    &CDlgSettingsWx::BuildTabKeybind);        // 13
    addPlaceholder(wxT("カスタムメニュー"),
                   wxT("カスタムメニュー。Phase 10 以降で対応予定。"));                     // 14
    addPanel(wxT("強調キーワード"),  &CDlgSettingsWx::BuildTabKeyword);        // 15
    addPlaceholder(wxT("支援"),
                   wxT("支援機能 (タグジャンプ等)。Tier 3 として断念済み。\n"
                       "外部 LSP の利用を推奨。"));                                        // 16
    addPlaceholder(wxT("マクロ"),
                   wxT("マクロエンジン。Tier 3 として断念済み。\n"
                       "代替: 外部スクリプト (zsh/python) または将来 LSP コードアクション。"));// 17
    addPlaceholder(wxT("プラグイン"),
                   wxT("プラグイン管理。Tier 3 として断念済み。"));                         // 18

    outer->Add(m_notebook, 1, wxEXPAND | wxALL, 8);

    auto* btnSizer = CreateButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) outer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    SetSizer(outer);
}

void CDlgSettingsWx::BuildTabPlaceholder(wxPanel* page,
                                         const wxString& title,
                                         const wxString& note) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* heading = new wxStaticText(page, wxID_ANY, title);
    wxFont hf = heading->GetFont();
    hf.SetWeight(wxFONTWEIGHT_BOLD);
    hf.SetPointSize(hf.GetPointSize() + 2);
    heading->SetFont(hf);
    sizer->Add(heading, 0, wxALL, 16);

    auto* msg = new wxStaticText(page, wxID_ANY, note);
    msg->Wrap(540);
    sizer->Add(msg, 0, wxLEFT | wxRIGHT, 16);

    sizer->AddStretchSpacer(1);
    page->SetSizer(sizer);
}

void CDlgSettingsWx::BuildTabGeneral(wxPanel* page) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // フォント (旧フォントタブと統合)
    auto* fontRow = new wxBoxSizer(wxHORIZONTAL);
    fontRow->Add(new wxStaticText(page, wxID_ANY, wxT("エディタフォント:")),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    wxFont initial = m_config ? m_config->GetEditorFont() : wxFont();
    m_fontPicker = new wxFontPickerCtrl(page, wxID_ANY, initial,
        wxDefaultPosition, wxDefaultSize,
        wxFNTP_FONTDESC_AS_LABEL | wxFNTP_USEFONT_FOR_LABEL);
    fontRow->Add(m_fontPicker, 1, wxEXPAND);
    sizer->Add(fontRow, 0, wxEXPAND | wxALL, 8);

    // 表示
    m_cbLineNumbers = new wxCheckBox(page, wxID_ANY, wxT("行番号を表示"));
    m_cbWordWrap    = new wxCheckBox(page, wxID_ANY, wxT("折り返しを有効化"));
    if (m_config) {
        m_cbLineNumbers->SetValue(m_config->GetShowLineNumbers());
        m_cbWordWrap->SetValue(m_config->GetWordWrap());
    }
    sizer->Add(m_cbLineNumbers, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sizer->Add(m_cbWordWrap,    0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // タブ幅
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

    auto* note = new wxStaticText(page, wxID_ANY,
        wxT("OK 押下後、開いている全タブに即時反映されます。"));
    note->Wrap(540);
    sizer->Add(note, 0, wxALL, 8);

    // 「全般」タブの拡張: メタテーブル駆動セクション (Phase 11/12)
    sizer->Add(new wxStaticLine(page, wxID_ANY), 0, wxEXPAND | wxALL, 6);
    {
        auto* heading = new wxStaticText(page, wxID_ANY,
            wxT("--- 共通設定メタテーブル (sakura_core/env/SettingsMeta.h) ---"));
        wxFont hf = heading->GetFont();
        hf.MakeBold();
        heading->SetFont(hf);
        sizer->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    }

    // メタ駆動 widget を生成しコンテナに追加
    auto* metaPanel = new wxPanel(page);
    BuildTabFromMeta(metaPanel, wxT("general"));
    sizer->Add(metaPanel, 1, wxEXPAND | wxALL, 8);

    page->SetSizer(sizer);
}

// ============================================================
// Phase 11/12: メタテーブル駆動タブビルダー
//   tabId に該当する SettingsMeta::TabDef を引数 page に展開する。
//   生成された widget は m_metaWidgets に記録され、OnOk で SakuraConfig へ保存。
// ============================================================
void CDlgSettingsWx::BuildTabFromMeta(wxPanel* page, const wxString& tabId) {
    namespace meta = sakura::settings_meta;

    // tabId に対応する TabDef を検索
    const meta::TabDef* tab = nullptr;
    std::string tabIdUtf8 = tabId.ToStdString(wxConvUTF8);
    for (std::size_t i = 0; i < meta::kSettingsTabCount; ++i) {
        if (meta::kSettingsTabs[i].id == tabIdUtf8) {
            tab = &meta::kSettingsTabs[i];
            break;
        }
    }
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    if (!tab || !tab->fields || tab->field_count == 0) {
        sizer->Add(new wxStaticText(page, wxID_ANY,
            wxT("(このタブはメタテーブル未登録です)")), 0, wxALL, 8);
        page->SetSizer(sizer);
        return;
    }

    auto* grid = new wxFlexGridSizer(2, wxSize(8, 4));
    grid->AddGrowableCol(1, 1);

    for (std::size_t i = 0; i < tab->field_count; ++i) {
        const meta::FieldDef& f = tab->fields[i];
        if (!f.visible) continue;
        wxString label    = wxString::FromUTF8(std::string(f.label).c_str());
        wxString tip      = wxString::FromUTF8(std::string(f.tooltip).c_str());
        wxString fieldId  = wxString::FromUTF8(std::string(f.id).c_str());

        MetaWidget mw{};
        mw.tabId        = tabId;
        mw.fieldId      = fieldId;
        mw.kind         = static_cast<int>(f.kind);
        mw.defaultValue = f.default_value;

        switch (f.kind) {
        case meta::FieldKind::Bool: {
            bool initial = m_config
                ? m_config->GetMetaBool(tabId, fieldId, f.default_value != 0)
                : (f.default_value != 0);
            auto* cb = new wxCheckBox(page, wxID_ANY, label);
            cb->SetValue(initial);
            cb->SetToolTip(tip);
            grid->Add(cb, 0, wxALIGN_CENTER_VERTICAL);
            grid->AddSpacer(0);
            mw.widget = cb;
            break;
        }
        case meta::FieldKind::Int: {
            int initial = m_config
                ? m_config->GetMetaInt(tabId, fieldId,
                                       static_cast<int>(f.default_value))
                : static_cast<int>(f.default_value);
            grid->Add(new wxStaticText(page, wxID_ANY, label),
                      0, wxALIGN_CENTER_VERTICAL);
            auto* sp = new wxSpinCtrl(page, wxID_ANY, wxEmptyString,
                wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
                static_cast<int>(f.min_value),
                static_cast<int>(f.max_value),
                initial);
            sp->SetToolTip(tip);
            grid->Add(sp, 0, wxEXPAND);
            mw.widget = sp;
            break;
        }
        case meta::FieldKind::Choice: {
            int initial = m_config
                ? m_config->GetMetaInt(tabId, fieldId,
                                       static_cast<int>(f.default_value))
                : static_cast<int>(f.default_value);
            grid->Add(new wxStaticText(page, wxID_ANY, label),
                      0, wxALIGN_CENTER_VERTICAL);
            wxArrayString items;
            int sel = 0;
            for (std::size_t k = 0; k < f.choice_count; ++k) {
                items.Add(wxString::FromUTF8(
                    std::string(f.choices[k].label).c_str()));
                mw.choiceValues.push_back(f.choices[k].value);
                if (f.choices[k].value == initial) sel = static_cast<int>(k);
            }
            auto* ch = new wxChoice(page, wxID_ANY,
                wxDefaultPosition, wxDefaultSize, items);
            ch->SetSelection(sel);
            ch->SetToolTip(tip);
            grid->Add(ch, 0, wxEXPAND);
            mw.widget = ch;
            break;
        }
        default:
            grid->Add(new wxStaticText(page, wxID_ANY, label),
                      0, wxALIGN_CENTER_VERTICAL);
            grid->Add(new wxStaticText(page, wxID_ANY,
                wxT("(未対応の種別)")), 0);
            continue; // 未対応種別は永続化対象外
        }
        m_metaWidgets.push_back(std::move(mw));
    }

    sizer->Add(grid, 1, wxEXPAND | wxALL, 8);
    page->SetSizer(sizer);
}

void CDlgSettingsWx::BuildTabKeybind(wxPanel* page) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(page, wxID_ANY,
        wxT("Phase 9 時点の主要キーバインド一覧 (参照のみ):")),
        0, wxALL, 8);

    wxArrayString items;
    items.Add(wxT("Ctrl+N           新規"));
    items.Add(wxT("Ctrl+O           開く"));
    items.Add(wxT("Ctrl+S           上書き保存"));
    items.Add(wxT("Ctrl+W           タブを閉じる"));
    items.Add(wxT("Ctrl+P           印刷"));
    items.Add(wxT("Ctrl+Z / Ctrl+Shift+Z   元に戻す / やり直し"));
    items.Add(wxT("Ctrl+X / Ctrl+C / Ctrl+V    切り取り / コピー / 貼り付け"));
    items.Add(wxT("Ctrl+A           すべて選択"));
    items.Add(wxT("Ctrl+F           検索"));
    items.Add(wxT("Ctrl+H           置換"));
    items.Add(wxT("Ctrl+G           Grep（フォルダ検索）"));
    items.Add(wxT("F3 / Shift+F3    次を検索 / 前を検索"));
    items.Add(wxT("F2 / Shift+F2    次のブックマーク / 前のブックマーク"));
    items.Add(wxT("Ctrl+F2          ブックマーク 切替"));
    items.Add(wxT("F5               アウトラインを更新"));

    auto* lb = new wxListBox(page, wxID_ANY,
                             wxDefaultPosition, wxDefaultSize, items,
                             wxLB_SINGLE | wxLB_HSCROLL);
    sizer->Add(lb, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* note = new wxStaticText(page, wxID_ANY,
        wxT("カスタマイズ機能は Phase 10 以降で追加予定。"));
    sizer->Add(note, 0, wxALL, 8);
    page->SetSizer(sizer);
}

void CDlgSettingsWx::BuildTabKeyword(wxPanel* page) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* lead = new wxStaticText(page, wxID_ANY,
        wxT("ファイルタイプごとのシンタックス強調色を設定します。"));
    lead->Wrap(540);
    sizer->Add(lead, 0, wxALL, 8);

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
    sizer->Add(typeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto addRow = [&](const wxString& label, wxColourPickerCtrl*& outPicker) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(page, wxID_ANY, label,
                                  wxDefaultPosition, wxSize(120, -1)),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        outPicker = new wxColourPickerCtrl(page, wxID_ANY);
        row->Add(outPicker);
        sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    };
    addRow(wxT("コメント:"),  m_pkComment);
    addRow(wxT("文字列:"),    m_pkString);
    addRow(wxT("キーワード:"), m_pkKeyword);
    addRow(wxT("数値:"),      m_pkNumber);

    sizer->AddStretchSpacer(1);

    auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->AddStretchSpacer(1);
    btnRow->Add(new wxButton(page, wxID_RESET, wxT("初期値に戻す")));
    sizer->Add(btnRow, 0, wxEXPAND | wxALL, 8);

    auto* note = new wxStaticText(page, wxID_ANY,
        wxT("変更は OK 押下後、開いている全タブに即時反映されます。"));
    note->Wrap(540);
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

        // メタテーブル駆動 widget の永続化 (Phase 12)
        namespace meta = sakura::settings_meta;
        for (const auto& mw : m_metaWidgets) {
            if (!mw.widget) continue;
            switch (static_cast<meta::FieldKind>(mw.kind)) {
            case meta::FieldKind::Bool: {
                if (auto* cb = dynamic_cast<wxCheckBox*>(mw.widget)) {
                    m_config->SetMetaBool(mw.tabId, mw.fieldId, cb->GetValue());
                }
                break;
            }
            case meta::FieldKind::Int: {
                if (auto* sp = dynamic_cast<wxSpinCtrl*>(mw.widget)) {
                    m_config->SetMetaInt(mw.tabId, mw.fieldId, sp->GetValue());
                }
                break;
            }
            case meta::FieldKind::Choice: {
                if (auto* ch = dynamic_cast<wxChoice*>(mw.widget)) {
                    int sel = ch->GetSelection();
                    if (sel >= 0 &&
                        sel < static_cast<int>(mw.choiceValues.size())) {
                        m_config->SetMetaInt(mw.tabId, mw.fieldId,
                                             mw.choiceValues[sel]);
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        m_config->Save();
    }
    event.Skip();
}

} // namespace sakura_wx::dialog
