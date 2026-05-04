#pragma once
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/string.h>

#include <vector>

class SakuraConfig;
class wxFontPickerCtrl;
class wxSpinCtrl;
class wxCheckBox;
class wxChoice;
class wxColourPickerCtrl;
class wxColourPickerEvent;
class wxListBox;
class wxWindow;

namespace sakura_wx::dialog {

/**
 * CDlgSettingsWx — 共通設定ダイアログ。
 *
 * Phase 9 で upstream の sakura_core/prop/CPropCommon.cpp:193-211 の
 * 18 タブ構成 (タブ名・順序) に揃えた。コード自体は GDI 依存のため
 * upstream を流用せず、wxNotebook + wxPanel で再実装。
 *
 * Upstream 18 タブ:
 *   1. 全般 (General)              — フォント／表示／タブ幅 (実装)
 *   2. ウィンドウ (Window)         — ウィンドウサイズ・位置設定 (placeholder)
 *   3. メインメニュー (MainMenu)   — メニュー構成 (placeholder)
 *   4. ツールバー (Toolbar)        — ツールバー項目 (placeholder)
 *   5. タブバー (Tabs)             — タブの色／表示 (placeholder)
 *   6. ステータスバー (Statusbar)  — 表示項目 (placeholder)
 *   7. 編集 (Editing)              — 自動インデント等 (placeholder)
 *   8. ファイル (File)             — 文字コード／改行 (placeholder)
 *   9. ファイル名表示 (FileName)   — タイトル表示形式 (placeholder)
 *  10. バックアップ (Backup)       — 自動バックアップ (placeholder)
 *  11. 書式 (Format)               — 整形ルール (placeholder)
 *  12. 検索 (Search/Grep)          — 検索デフォルト (placeholder)
 *  13. キー割り当て (Keybind)      — ショートカット一覧 (実装、参照のみ)
 *  14. カスタムメニュー (Custmenu) — カスタムメニュー (placeholder)
 *  15. 強調キーワード (Keyword)    — タイプ別シンタックス (実装)
 *  16. 支援 (Support)              — 支援機能 (placeholder)
 *  17. マクロ (Macro)              — マクロ管理 (Tier 3 で断念済み — placeholder)
 *  18. プラグイン (Plugin)         — プラグイン (Tier 3 で断念済み — placeholder)
 *
 * 「強調キーワード」タブは Phase 8 で実装したタイプ別シンタックス色設定を
 * upstream の対応するタブに移動した形。
 */
class CDlgSettingsWx : public wxDialog {
public:
    CDlgSettingsWx(wxWindow* parent, SakuraConfig* config);

private:
    void BuildUI();

    // 実装済みタブ
    void BuildTabGeneral(wxPanel* page);    // 1
    void BuildTabKeybind(wxPanel* page);    // 13
    void BuildTabKeyword(wxPanel* page);    // 15 (旧「タイプ別」と同等)

    // メタテーブル駆動タブ (Phase 11/12)
    //   sakura_core/env/SettingsMeta.h の TabDef を引数に取り、フィールド種別に応じて
    //   wxCheckBox / wxSpinCtrl / wxChoice を自動生成する。
    //   生成された widget は m_metaWidgets に記録され、OnOk で SakuraConfig に保存。
    void BuildTabFromMeta(wxPanel* page, const wxString& tabId);

    // プレースホルダタブ (未対応)
    void BuildTabPlaceholder(wxPanel* page,
                             const wxString& title,
                             const wxString& note);

    void OnOk(wxCommandEvent& event);
    void OnTypeChange(wxCommandEvent& event);
    void OnColorReset(wxCommandEvent& event);
    void RefreshColorPickers();

    SakuraConfig*    m_config = nullptr;
    wxNotebook*      m_notebook = nullptr;

    // 全般タブ
    wxCheckBox*       m_cbLineNumbers = nullptr;
    wxCheckBox*       m_cbWordWrap    = nullptr;
    wxSpinCtrl*       m_spTabWidth    = nullptr;
    wxFontPickerCtrl* m_fontPicker    = nullptr;

    // 強調キーワードタブ (旧タイプ別)
    wxChoice*           m_typeChoice = nullptr;
    wxColourPickerCtrl* m_pkComment  = nullptr;
    wxColourPickerCtrl* m_pkString   = nullptr;
    wxColourPickerCtrl* m_pkKeyword  = nullptr;
    wxColourPickerCtrl* m_pkNumber   = nullptr;

    // メタテーブル駆動タブの widget レジストリ (Phase 12)
    //   tabId / fieldId は SettingsMeta.h の文字列をコピーして保持。
    //   wxString::FromUTF8 で日本語ラベルとは別に英語 ID を持つ前提。
    struct MetaWidget {
        wxString    tabId;
        wxString    fieldId;
        int         kind;        // sakura::settings_meta::FieldKind を int 化
        wxWindow*   widget;      // wxCheckBox / wxSpinCtrl / wxChoice のいずれか
        long long   defaultValue;
        // Choice は idx→value のマップを持つ
        std::vector<int> choiceValues;
    };
    std::vector<MetaWidget> m_metaWidgets;

    wxDECLARE_EVENT_TABLE();
};

} // namespace sakura_wx::dialog
