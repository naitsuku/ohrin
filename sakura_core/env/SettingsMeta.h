/*! @file
 *  @brief 設定ダイアログのメタテーブル（プラットフォーム非依存・純粋データ）
 *
 *  Phase 11 で導入。Win32 版 (CPropCommon 系) と wxWidgets 版 (CDlgSettingsWx) の
 *  両方から参照可能な「設定タブ／フィールドの宣言的記述」を提供する。
 *
 *  目的:
 *      - 設定の量が多いため、別々の実装でメンテすると差分が広がる懸念に対応
 *      - 「どのタブにどのフィールドがあるか」をひとつの宣言で持ち、
 *        各プラットフォームの GUI 実装はその記述を読んで widget を生成する
 *
 *  設計方針:
 *      - 本ヘッダは <Windows.h> や basis/, util/ に依存しない（純粋 C++ のみ）
 *      - 値の格納は CommonSetting_* 構造体側に残す（オフセット参照ではなく ID 文字列）
 *      - 段階的拡充: まず "全般" タブから本テーブルに移行、徐々に他タブを追加
 *
 *  Phase 11 では「全般」タブの一部のみメタ化。Phase 12+ で順次他タブを移行する。
 */
#ifndef SAKURA_SETTINGSMETA_E5C8B6E0_4A82_4A47_B4A7_98C0DE2A1F58_H_
#define SAKURA_SETTINGSMETA_E5C8B6E0_4A82_4A47_B4A7_98C0DE2A1F58_H_
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sakura::settings_meta {

//! フィールドの種別
enum class FieldKind : std::uint8_t {
    Bool,        //!< チェックボックス (BOOL / bool)
    Int,         //!< 整数値 (上限・下限あり)
    Choice,      //!< 選択肢 (ラジオ or コンボ)
    String,      //!< 1 行文字列
    KeyCode,     //!< 仮想キーコード
    Color,       //!< 色 (RGB)
    Reserved     //!< 表示しない／予約 (現在は対応プラットフォーム外など)
};

//! 選択肢項目 (FieldKind::Choice 用)
struct ChoiceItem {
    int                     value;
    std::string_view        label;
};

//! 個別フィールドの宣言
struct FieldDef {
    //! 内部 ID (CommonSetting 構造体メンバ名と一致させる: 例 "m_bIsINSMode")
    std::string_view        id;
    //! 表示ラベル (将来的にローカライズ ID 化)
    std::string_view        label;
    //! 簡単な説明 (ツールチップ等)
    std::string_view        tooltip;
    //! 種別
    FieldKind               kind;
    //! 既定値 (Bool/Int/Choice/Color は数値、文字列フィールドでは未使用)
    long long               default_value;
    //! Int の最小値 (Int/KeyCode 用)
    long long               min_value;
    //! Int の最大値 (Int/KeyCode 用)
    long long               max_value;
    //! Choice の選択肢配列 (なければ nullptr/0)
    const ChoiceItem*       choices;
    std::size_t             choice_count;
    //! このフィールドを Win32/wx の双方で実際の GUI に出すか (false なら非表示)
    bool                    visible;
};

//! 設定タブの宣言
struct TabDef {
    //! タブ内部 ID (例 "general", "window")
    std::string_view        id;
    //! 表示ラベル
    std::string_view        label;
    //! このタブのフィールド配列
    const FieldDef*         fields;
    std::size_t             field_count;
    //! 現状サポート中か (false: Phase X+ 対応予定として placeholder 表示)
    bool                    supported;
};

// ============================================================
// 「全般」タブ — Phase 11 メタ化対象
//   CommonSetting_General に対応する。値の get/set は構造体側で行い、
//   GUI ビルダはここの id でフィールド名を解決する想定。
// ============================================================
inline constexpr ChoiceItem kCaretTypeChoices[] = {
    {0, "Windows 風"},
    {1, "DOS 風"},
};

inline constexpr ChoiceItem kPageScrollByWheelChoices[] = {
    {0, "なし"},
    {1, "Ctrl 押下時"},
    {2, "Shift 押下時"},
    {3, "常に"},
};

inline constexpr FieldDef kGeneralFields[] = {
    // カーソル
    {"m_nCaretType",
        "カーソル形状",
        "カーソルの表示スタイル",
        FieldKind::Choice, 0, 0, 1,
        kCaretTypeChoices, std::size(kCaretTypeChoices), true},
    {"m_bIsINSMode",
        "起動時は挿入モード",
        "起動時の入力モード（オフ＝上書き）",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bIsFreeCursorMode",
        "フリーカーソルモード",
        "行末を超えてカーソル移動を許可",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bStopsBothEndsWhenSearchWord",
        "単語移動で両端停止",
        "単語単位移動時に単語の両端で止まる",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bStopsBothEndsWhenSearchParagraph",
        "段落移動で両端停止",
        "段落単位移動時に段落の両端で止まる",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bNoCaretMoveByActivation",
        "アクティベート時カーソル移動なし",
        "クリックでウィンドウをアクティブにしてもカーソルは動かさない",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},

    // スクロール
    {"m_nRepeatedScrollLineNum",
        "リピートスクロール行数",
        "キーリピート時の縦スクロール行数",
        FieldKind::Int, 1, 1, 100, nullptr, 0, true},
    {"m_nRepeatedMoveCaretNum",
        "リピート左右移動文字数",
        "キーリピート時の左右移動量",
        FieldKind::Int, 1, 1, 100, nullptr, 0, true},
    {"m_nPageScrollByWheel",
        "ホイールでページスクロール",
        "マウスホイールでのページスクロール条件",
        FieldKind::Choice, 0, 0, 3,
        kPageScrollByWheelChoices, std::size(kPageScrollByWheelChoices), true},

    // 履歴
    {"m_nMRUArrNum_MAX",
        "ファイル履歴の上限",
        "[ファイル]→[最近使用したファイル] の最大件数",
        FieldKind::Int, 16, 0, 50, nullptr, 0, true},
    {"m_nOPENFOLDERArrNum_MAX",
        "フォルダ履歴の上限",
        "ダイアログのフォルダ履歴の最大件数",
        FieldKind::Int, 16, 0, 50, nullptr, 0, true},

    // 確認
    {"m_bCloseAllConfirm",
        "すべて閉じるで確認",
        "[すべて閉じる] で他に編集用のウィンドウがあれば確認",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bExitConfirm",
        "終了時に確認",
        "アプリ終了時に確認ダイアログを表示",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
};

// ============================================================
// 「ウィンドウ」タブ — Phase 12 メタ化対象
//   CommonSetting_Window に対応。
// ============================================================
inline constexpr ChoiceItem kFuncKeyPlaceChoices[] = {
    {0, "上"},
    {1, "下"},
};

inline constexpr ChoiceItem kSaveModeChoices[] = {
    {0, "指定なし (前回値を継承)"},
    {1, "保存して継承"},
    {2, "直接指定で固定"},
};

inline constexpr FieldDef kWindowFields[] = {
    {"m_bDispTOOLBAR",
        "ツールバーを表示",
        "次回ウィンドウを開いたときツールバーを表示する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bDispSTATUSBAR",
        "ステータスバーを表示",
        "次回ウィンドウを開いたときステータスバーを表示する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bDispFUNCKEYWND",
        "ファンクションキーバーを表示",
        "次回ウィンドウを開いたときファンクションキーを表示する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bDispMiniMap",
        "ミニマップを表示",
        "右側にミニマップを表示する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bMenuIcon",
        "メニューアイコン",
        "メニュー項目にアイコンを付ける",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bScrollBarHorz",
        "水平スクロールバー",
        "水平スクロールバーを使う",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bDarkMode",
        "ダークモード",
        "ダークモードで表示する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_eSaveWindowSize",
        "ウィンドウサイズの保存方法",
        "次回起動時のウィンドウサイズの扱い",
        FieldKind::Choice, 1, 0, 2,
        kSaveModeChoices, std::size(kSaveModeChoices), true},
    {"m_eSaveWindowPos",
        "ウィンドウ位置の保存方法",
        "次回起動時のウィンドウ位置の扱い",
        FieldKind::Choice, 1, 0, 2,
        kSaveModeChoices, std::size(kSaveModeChoices), true},
    {"m_nFUNCKEYWND_Place",
        "ファンクションキー表示位置",
        "ファンクションキーバーを上下どちらに置くか",
        FieldKind::Choice, 1, 0, 1,
        kFuncKeyPlaceChoices, std::size(kFuncKeyPlaceChoices), true},
    {"m_nRulerHeight",
        "ルーラー高さ (px)",
        "ルーラーの高さ (ピクセル)",
        FieldKind::Int, 13, 8, 30, nullptr, 0, true},
    {"m_bSplitterWndHScroll",
        "分割ウィンドウの横スクロール同期",
        "分割ビュー間で水平スクロールを同期",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bSplitterWndVScroll",
        "分割ウィンドウの縦スクロール同期",
        "分割ビュー間で垂直スクロールを同期",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
};

// ============================================================
// 「編集」タブ — Phase 12 メタ化対象
//   CommonSetting_Edit に対応。
// ============================================================
inline constexpr ChoiceItem kOpenDialogDirChoices[] = {
    {0, "カレントフォルダ"},
    {1, "最近使ったフォルダ"},
    {2, "指定フォルダ"},
};

inline constexpr FieldDef kEditFields[] = {
    // コピー
    {"m_bAddCRLFWhenCopy",
        "折り返し行に改行を付けてコピー",
        "コピー時、ソフト改行位置に CRLF を入れる",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bEnableNoSelectCopy",
        "選択なしでコピーを有効化",
        "選択範囲がないときに行全体をコピー",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bCopyAndDisablSelection",
        "コピー後に選択解除",
        "コピー操作後、選択範囲を解除する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bEnableLineModePaste",
        "ラインモード貼り付けを有効化",
        "行コピーを行末に貼り付ける",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bConvertEOLPaste",
        "改行コードを変換して貼り付け",
        "貼り付け時、改行コードを編集中の文書に合わせる",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},

    // 上書き
    {"m_bNotOverWriteCRLF",
        "改行は上書きしない",
        "上書きモードでも改行コードは上書きせず挿入",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bOverWriteFixMode",
        "文字幅にスペース詰め",
        "上書き時、文字幅に合わせてスペースで詰める",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bOverWriteBoxDelete",
        "矩形入力で選択削除",
        "上書きモード矩形入力で選択範囲を削除する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},

    // ドラッグ&ドロップ
    {"m_bUseOLE_DragDrop",
        "OLE ドラッグ&ドロップを使う",
        "ドラッグ&ドロップに OLE を使用 (Win32 のみ意味あり)",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},

    // URL
    {"m_bSelectClickedURL",
        "URL クリックで選択",
        "クリッカブル URL がクリックされたら選択状態にする",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},

    // ファイルダイアログ
    {"m_eOpenDialogDir",
        "ファイルダイアログの初期位置",
        "[開く] ダイアログを開いたときの初期フォルダ",
        FieldKind::Choice, 0, 0, 2,
        kOpenDialogDirChoices, std::size(kOpenDialogDirChoices), true},

    // 拡張改行
    {"m_bEnableExtEol",
        "NEL/PS/LS を改行として扱う",
        "Unicode の追加改行コードを改行として認識",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},

    // 矩形選択
    {"m_bBoxSelectLock",
        "矩形選択を移動でロック",
        "矩形選択中、カーソル移動でロック維持",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
};

// ============================================================
// タブテーブル
//   Phase 11 で「全般」、Phase 12 で「ウィンドウ」「編集」をメタ化。
//   残り 15 タブは Phase 13+ で順次追加予定。
//   タブ並び順は upstream CPropCommon.cpp の AddPage 順 (18 タブ) と一致させる。
// ============================================================
inline constexpr TabDef kSettingsTabs[] = {
    {"general",   "全般",         kGeneralFields, std::size(kGeneralFields), true},
    {"window",    "ウィンドウ",   kWindowFields,  std::size(kWindowFields),  true},
    {"tabbar",    "タブバー",     nullptr, 0, false},
    {"edit",      "編集",         kEditFields,    std::size(kEditFields),    true},
    {"file",      "ファイル",     nullptr, 0, false},
    {"backup",    "バックアップ", nullptr, 0, false},
    {"format",    "書式",         nullptr, 0, false},
    {"filename",  "ファイル名表示", nullptr, 0, false},
    {"keyword",   "強調キーワード", nullptr, 0, false},
    {"customize", "カスタマイズ",   nullptr, 0, false},
    {"keybind",   "キー割り当て",   nullptr, 0, false},
    {"toolbar",   "ツールバー",     nullptr, 0, false},
    {"funckey",   "ファンクションキー", nullptr, 0, false},
    {"mainmenu",  "メインメニュー",  nullptr, 0, false},
    {"popupmenu", "ポップアップメニュー", nullptr, 0, false},
    {"plugin",    "プラグイン",      nullptr, 0, false},
    {"helper",    "支援",            nullptr, 0, false},
    {"others",    "その他",          nullptr, 0, false},
};

inline constexpr std::size_t kSettingsTabCount = std::size(kSettingsTabs);

} // namespace sakura::settings_meta

#endif /* SAKURA_SETTINGSMETA_E5C8B6E0_4A82_4A47_B4A7_98C0DE2A1F58_H_ */
