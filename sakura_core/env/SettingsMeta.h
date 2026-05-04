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
// タブテーブル
//   現在は「全般」のみメタ化。他タブは Phase 12+ で順次追加。
//   タブ並び順は upstream CPropCommon.cpp の AddPage 順 (18 タブ) と一致させる。
// ============================================================
inline constexpr TabDef kSettingsTabs[] = {
    {"general",   "全般",         kGeneralFields, std::size(kGeneralFields), true},
    {"window",    "ウィンドウ",   nullptr, 0, false},
    {"tabbar",    "タブバー",     nullptr, 0, false},
    {"edit",      "編集",         nullptr, 0, false},
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
