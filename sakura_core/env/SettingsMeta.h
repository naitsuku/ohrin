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
// 「ファイル」タブ — Phase 13b メタ化対象
//   CommonSetting_File に対応。EShareMode は Choice 化、
//   それ以外の bool/int フィールドをメタ化。
//   String widget 対応後 (Phase 13b) に追加: なし (パス系は無し)。
// ============================================================
inline constexpr ChoiceItem kShareModeChoices[] = {
    {0, "排他制御しない"},
    {1, "他プロセスからの上書きを禁止"},
    {2, "他プロセスからの読み書きを禁止"},
};

inline constexpr FieldDef kFileFields[] = {
    // 排他制御
    {"m_nFileShareMode",
        "ファイルの排他制御",
        "ファイルを開いている間の排他モード",
        FieldKind::Choice, 0, 0, 2,
        kShareModeChoices, std::size(kShareModeChoices), true},
    {"m_bCheckFileTimeStamp",
        "更新の監視",
        "他プロセスでの更新を検出する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_nAutoloadDelay",
        "自動読込の遅延 (ms)",
        "更新検出後、自動再読込までの遅延ミリ秒",
        FieldKind::Int, 1000, 0, 60000, nullptr, 0, true},
    {"m_bUneditableIfUnwritable",
        "上書き禁止時は編集禁止",
        "ファイルが書込不可なら編集をロックする",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},

    // 保存
    {"m_bEnableUnmodifiedOverwrite",
        "無変更でも上書き保存",
        "編集していないファイルでも上書き保存する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bNoFilterSaveNew",
        "新規保存はフィルタ無視",
        "[名前を付けて保存] (新規) は全ファイル表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bNoFilterSaveFile",
        "既存保存はフィルタ無視",
        "[名前を付けて保存] (既存) は全ファイル表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},

    // オープン
    {"m_bDropFileAndClose",
        "ドロップで閉じて開く",
        "ファイルをドロップしたとき現タブを閉じて開く",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_nDropFileNumMax",
        "同時ドロップ上限",
        "一度にドロップ可能なファイル数",
        FieldKind::Int, 8, 1, 100, nullptr, 0, true},
    {"m_bRestoreCurPosition",
        "カーソル位置を復元",
        "ファイルを開いたとき前回のカーソル位置を復元",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bRestoreBookmarks",
        "ブックマークを復元",
        "ファイルを開いたとき前回のブックマークを復元",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bAutoMIMEdecode",
        "MIME 自動デコード",
        "ファイル読込時に MIME のデコードを行う",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bQueryIfCodeChange",
        "文字コード変化時に確認",
        "前回と文字コードが異なるとき問い合わせる",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bAlertIfFileNotExist",
        "ファイル不在時に警告",
        "開こうとしたファイルが存在しないとき警告",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bAlertIfLargeFile",
        "大ファイル時に警告",
        "サイズが大きいファイルを開く前に警告",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_nAlertFileSize",
        "警告ファイルサイズ (MB)",
        "警告を始めるファイルサイズ (MB)",
        FieldKind::Int, 10, 1, 10000, nullptr, 0, true},
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
    {"m_nMiniMapWidth",
        "ミニマップ幅 (px)",
        "ミニマップパネルの幅 (40-400px)",
        FieldKind::Int, 110, 40, 400, nullptr, 0, true},
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

    // タイトルバーキャプション (Phase 13b: String widget 対応)
    {"m_szWindowCaptionActive",
        "アクティブ時タイトル",
        "ウィンドウがアクティブな時のタイトルバー (空: デフォルト)",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_szWindowCaptionInactive",
        "非アクティブ時タイトル",
        "ウィンドウが非アクティブな時のタイトルバー (空: デフォルト)",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
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
// 「タブバー」タブ — Phase 13a メタ化対象
//   CommonSetting_TabBar に対応。LOGFONT / wide 文字列はメタ化対象外
//   (Color/String widget 実装後に再評価)。
// ============================================================
inline constexpr ChoiceItem kTabPositionChoices[] = {
    {0, "上"},
    {1, "下"},
    {2, "左"},
    {3, "右"},
};

inline constexpr ChoiceItem kDispTabCloseChoices[] = {
    {0, "なし"},
    {1, "常に表示"},
    {2, "自動表示"},
};

inline constexpr FieldDef kTabBarFields[] = {
    {"m_bDispTabWnd",
        "タブを表示",
        "タブウィンドウを表示する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bDispTabWndMultiWin",
        "タブをまとめない",
        "ウィンドウごとにタブを別管理する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bTab_RetainEmptyWin",
        "空ウィンドウを残す",
        "最後の文書を閉じても (無題) を残す",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bTab_CloseOneWin",
        "閉じるボタンで現タブのみ閉じる",
        "ウィンドウの閉じるボタンで現在のタブのみ閉じる",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bNewWindow",
        "外部起動は新ウィンドウ",
        "外部から起動するときは新しいウィンドウで開く",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bTabMultiLine",
        "タブ多段表示",
        "タブを多段で表示する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_eTabPosition",
        "タブ位置",
        "タブの表示位置",
        FieldKind::Choice, 0, 0, 3,
        kTabPositionChoices, std::size(kTabPositionChoices), true},
    {"m_bSameTabWidth",
        "タブを等幅にする",
        "全てのタブを同じ幅で表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bDispTabIcon",
        "タブにアイコンを表示",
        "ファイルタイプのアイコンをタブに表示",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bDispTabClose",
        "タブの閉じるボタン",
        "個々のタブに閉じるボタンを表示",
        FieldKind::Choice, 1, 0, 2,
        kDispTabCloseChoices, std::size(kDispTabCloseChoices), true},
    {"m_bSortTabList",
        "タブ一覧をソート",
        "タブ一覧ダイアログでソート表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bTab_ListFull",
        "タブ一覧をフルパス表示",
        "タブ一覧でファイルのフルパスを表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bChgWndByWheel",
        "ホイールでウィンドウ切替",
        "マウスホイールでタブ間を切り替える",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_nPointSize",
        "タブフォントサイズ (1/10 pt)",
        "タブのフォントサイズ (90 = 9.0pt)",
        FieldKind::Int, 90, 60, 240, nullptr, 0, true},
    {"m_nTabMaxWidth",
        "タブ幅の最大値",
        "個々のタブの最大幅 (px)",
        FieldKind::Int, 200, 50, 1000, nullptr, 0, true},
    {"m_nTabMinWidth",
        "タブ幅の最小値",
        "個々のタブの最小幅 (px)",
        FieldKind::Int, 50, 20, 500, nullptr, 0, true},
    {"m_nTabMinWidthOnMulti",
        "タブ幅最小値 (多段時)",
        "多段時のタブ最小幅 (px)",
        FieldKind::Int, 50, 20, 500, nullptr, 0, true},
};

// ============================================================
// 「バックアップ」タブ — Phase 13a メタ化対象
//   CommonSetting_Backup に対応。SFilePath (バックアップフォルダ指定) は
//   String widget 実装後に追加 (Phase 13b)。
//   *_Opt1〜6 のビット詰め込みフィールドは GUI 専用ロジックのため除外。
// ============================================================
inline constexpr ChoiceItem kBackUpTypeChoices[] = {
    {1, "ファイル名 + .bak"},
    {2, "ファイル名 + 日付"},
    {3, "ファイル名 + 連番"},
};

inline constexpr FieldDef kBackupFields[] = {
    {"m_bBackUp",
        "バックアップを作成",
        "保存時にバックアップを作成する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bBackUpDialog",
        "作成前に確認",
        "バックアップの作成前に確認ダイアログを出す",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bBackUpFolder",
        "指定フォルダに作成",
        "指定したフォルダにバックアップを作成する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bBackUpFolderRM",
        "リムーバブル時のみ指定フォルダ",
        "リムーバブルメディア編集時のみ指定フォルダにバックアップ",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_nBackUpType",
        "バックアップファイル名形式",
        "バックアップファイルの命名規則",
        FieldKind::Choice, 1, 1, 3,
        kBackUpTypeChoices, std::size(kBackUpTypeChoices), true},
    {"m_bBackUpDustBox",
        "ごみ箱に放り込む",
        "バックアップファイルをごみ箱に移動",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bBackUpPathAdvanced",
        "バックアップパスの詳細設定",
        "バックアップ先フォルダを詳細設定する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_szBackUpFolder",
        "バックアップフォルダ",
        "バックアップを作成するフォルダのパス",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_szBackUpPathAdvanced",
        "バックアップパス詳細",
        "バックアップ詳細設定のフォルダパス",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
};

// ============================================================
// 「書式」タブ — Phase 13a メタ化対象
//   CommonSetting_Format に対応。WCHAR 配列 (日付書式, 引用符等) は
//   String widget 実装後に追加 (Phase 13b)。
// ============================================================
inline constexpr ChoiceItem kDateFormatTypeChoices[] = {
    {0, "システム標準"},
    {1, "ユーザ指定 (Phase 13b 以降)"},
};

inline constexpr ChoiceItem kTimeFormatTypeChoices[] = {
    {0, "システム標準"},
    {1, "ユーザ指定 (Phase 13b 以降)"},
};

inline constexpr FieldDef kFormatFields[] = {
    {"m_nDateFormatType",
        "日付書式タイプ",
        "日付の挿入時に使う書式",
        FieldKind::Choice, 0, 0, 1,
        kDateFormatTypeChoices, std::size(kDateFormatTypeChoices), true},
    {"m_szDateFormat",
        "日付書式 (ユーザ指定)",
        "日付書式タイプが「ユーザ指定」のときの書式 (例: yyyy-MM-dd)",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_nTimeFormatType",
        "時刻書式タイプ",
        "時刻の挿入時に使う書式",
        FieldKind::Choice, 0, 0, 1,
        kTimeFormatTypeChoices, std::size(kTimeFormatTypeChoices), true},
    {"m_szTimeFormat",
        "時刻書式 (ユーザ指定)",
        "時刻書式タイプが「ユーザ指定」のときの書式 (例: HH:mm:ss)",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_szMidashiKigou",
        "見出し記号",
        "見出しとして扱う記号 (アウトライン解析用)",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_szInyouKigou",
        "引用符",
        "引用符として扱う記号 (例: > | ）",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
};

// ============================================================
// 「ファイル名表示」タブ — Phase 13a メタ化対象
//   CommonSetting_FileName に対応。ファイル名簡易表示変換テーブル
//   (m_szTransformFileNameFrom/To 配列) は専用 GUI editor 必要のため除外。
// ============================================================
inline constexpr FieldDef kFileNameFields[] = {
    {"m_bTransformShortPath",
        "ファイル名の省略表記",
        "長いパスを省略形で表示する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_nTransformShortMaxWidth",
        "省略表記の最大長",
        "省略表記時の最大文字数",
        FieldKind::Int, 80, 20, 200, nullptr, 0, true},
};

// ============================================================
// 「ステータスバー」タブ — Phase 14a メタ化対象
//   CommonSetting_Statusbar に対応。すべて表示項目の Bool 集合。
// ============================================================
inline constexpr FieldDef kStatusbarFields[] = {
    {"m_bDispUniInSjis",
        "SJIS でも Unicode 値を表示",
        "SJIS テキストでも文字コード値を Unicode で表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bDispUniInJis",
        "JIS でも Unicode 値を表示",
        "JIS テキストでも文字コード値を Unicode で表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bDispUniInEuc",
        "EUC でも Unicode 値を表示",
        "EUC テキストでも文字コード値を Unicode で表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bDispUtf8Codepoint",
        "UTF-8 をコードポイント表示",
        "UTF-8 をコードポイントで表示する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bDispSPCodepoint",
        "サロゲートペアをコードポイント表示",
        "サロゲートペアをコードポイントで表示する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bDispSelCountByByte",
        "選択文字数をバイト単位で表示",
        "選択文字数を文字単位ではなくバイト単位で表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bDispColByChar",
        "現在桁を文字単位で表示",
        "現在桁をルーラー単位ではなく文字単位で表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
};

// ============================================================
// 「検索」タブ — Phase 14a メタ化対象
//   CommonSetting_Search に対応。
//   SSearchOption 構造体 (検索条件のビット詰め) と m_szRegexpLib (DLL 名) は除外。
// ============================================================
inline constexpr ChoiceItem kGrepOutputLineTypeChoices[] = {
    {0, "該当行を出力"},
    {1, "該当部分のみ出力"},
    {2, "否マッチ行を出力"},
};

inline constexpr ChoiceItem kGrepOutputStyleChoices[] = {
    {0, "標準形式"},
    {1, "結果ファイル形式"},
    {2, "WZ 互換形式"},
};

inline constexpr ChoiceItem kTagJumpModeChoices[] = {
    {0, "標準"},
    {1, "GNU 形式"},
    {2, "詳細"},
    {3, "高度"},
};

inline constexpr FieldDef kSearchFields[] = {
    // 検索ダイアログ
    {"m_bConsecutiveAll",
        "「すべて置換」は繰返し",
        "「すべて置換」を置換の繰返しで実行",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bNOTIFYNOTFOUND",
        "見つからないときメッセージ",
        "検索／置換で見つからないときメッセージを表示",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bSelectedArea",
        "選択範囲内置換",
        "選択範囲内のみ置換する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bCaretTextForSearch",
        "カーソル位置の単語を初期検索文字列に",
        "検索ダイアログを開いたとき、カーソル位置の単語を初期値にする",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bInheritKeyOtherView",
        "他ビューの検索条件を継承",
        "次・前検索で他のビューの検索条件を引き継ぐ",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bAutoCloseDlgFind",
        "検索ダイアログを自動で閉じる",
        "検索後、検索ダイアログを自動的に閉じる",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bAutoCloseDlgReplace",
        "置換ダイアログを自動で閉じる",
        "置換後、置換ダイアログを自動的に閉じる",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bSearchAll",
        "先頭(末尾)から再検索",
        "末端まで検索後、先頭(末尾)から再検索する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},

    // Grep
    {"m_bGrepSubFolder",
        "Grep: サブフォルダ検索",
        "Grep でサブフォルダも検索する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_nGrepOutputLineType",
        "Grep 出力種別",
        "Grep 結果の出力形式",
        FieldKind::Choice, 0, 0, 2,
        kGrepOutputLineTypeChoices, std::size(kGrepOutputLineTypeChoices), true},
    {"m_nGrepOutputStyle",
        "Grep 出力スタイル",
        "Grep 結果の出力スタイル",
        FieldKind::Choice, 0, 0, 2,
        kGrepOutputStyleChoices, std::size(kGrepOutputStyleChoices), true},
    {"m_bGrepDefaultFolder",
        "Grep: 初期フォルダはカレント",
        "Grep ダイアログのフォルダ初期値をカレントフォルダにする",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bGrepOutputFileOnly",
        "Grep: ファイル毎最初のみ",
        "Grep でファイル毎に最初のヒットだけ出力",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bGrepOutputBaseFolder",
        "Grep: ベースフォルダ表示",
        "Grep 結果でベースフォルダを表示",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bGrepSeparateFolder",
        "Grep: フォルダ毎に表示",
        "Grep 結果をフォルダ毎にグループ化",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bGrepBackup",
        "Grep: バックアップ作成",
        "置換 Grep でバックアップを作成する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bGrepExitConfirm",
        "Grep モードで保存確認",
        "Grep モードで終了時に保存確認する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bGrepRealTimeView",
        "Grep 結果リアルタイム表示",
        "Grep 結果をリアルタイムで表示する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},

    // タグジャンプ
    {"m_bGTJW_RETURN",
        "Enter でタグジャンプ",
        "Grep 結果で Enter キーでタグジャンプ",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bGTJW_LDBLCLK",
        "ダブルクリックでタグジャンプ",
        "Grep 結果でダブルクリックでタグジャンプ",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_nTagJumpMode",
        "タグジャンプモード",
        "タグジャンプの動作モード",
        FieldKind::Choice, 0, 0, 3,
        kTagJumpModeChoices, std::size(kTagJumpModeChoices), true},

    // 正規表現 DLL (Phase 13b: String 対応)
    {"m_szRegexpLib",
        "正規表現 DLL 名",
        "使用する正規表現ライブラリの DLL 名",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    // INI 内設定
    {"m_bUseCaretKeyWord",
        "キャレット位置の単語を辞書検索",
        "キャレット位置の単語を辞書検索する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
};

// ============================================================
// 「アウトライン」タブ — Phase 14a メタ化対象
//   CommonSetting_OutLine に対応。
//   SFileTree (ツリー設定) と SFilePath は除外 (専用 GUI 必要)。
// ============================================================
inline constexpr ChoiceItem kDockSideChoices[] = {
    {-1, "ドッキング禁止"},
    {0,  "フローティング"},
    {1,  "左ドッキング"},
    {2,  "上ドッキング"},
    {3,  "右ドッキング"},
    {4,  "下ドッキング"},
};

inline constexpr ChoiceItem kOutlineDockSetChoices[] = {
    {0, "共通設定"},
    {1, "タイプ別設定"},
};

inline constexpr FieldDef kOutlineFields[] = {
    {"m_bRememberOutlineWindowPos",
        "アウトライン位置とサイズを記憶",
        "アウトライン/トピックリストの位置・サイズを次回起動時に復元",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_widthOutlineWindow",
        "アウトライン幅 (px)",
        "アウトラインウィンドウの幅",
        FieldKind::Int, 220, 100, 2000, nullptr, 0, true},
    {"m_heightOutlineWindow",
        "アウトライン高さ (px)",
        "アウトラインウィンドウの高さ",
        FieldKind::Int, 400, 100, 2000, nullptr, 0, true},
    {"m_nOutlineDockSet",
        "ドッキング位置の継承方法",
        "アウトライン解析のドッキング位置を共通／タイプ別から取得",
        FieldKind::Choice, 0, 0, 1,
        kOutlineDockSetChoices, std::size(kOutlineDockSetChoices), true},
    {"m_bOutlineDockSync",
        "ドッキング位置を同期",
        "アウトライン解析のドッキング位置を同期する",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bOutlineDockDisp",
        "アウトライン解析を表示",
        "アウトライン解析の有無",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_eOutlineDockSide",
        "アウトラインのドッキング配置",
        "アウトラインの初期配置位置",
        FieldKind::Choice, 1, -1, 4,
        kDockSideChoices, std::size(kDockSideChoices), true},
    {"m_cxOutlineDockLeft",
        "左ドッキング幅 (px)",
        "左にドッキング時のアウトライン幅",
        FieldKind::Int, 220, 100, 1000, nullptr, 0, true},
    {"m_cxOutlineDockRight",
        "右ドッキング幅 (px)",
        "右にドッキング時のアウトライン幅",
        FieldKind::Int, 220, 100, 1000, nullptr, 0, true},
    {"m_cyOutlineDockTop",
        "上ドッキング高さ (px)",
        "上にドッキング時のアウトライン高さ",
        FieldKind::Int, 200, 50, 1000, nullptr, 0, true},
    {"m_cyOutlineDockBottom",
        "下ドッキング高さ (px)",
        "下にドッキング時のアウトライン高さ",
        FieldKind::Int, 200, 50, 1000, nullptr, 0, true},
    {"m_bAutoCloseDlgFuncList",
        "アウトラインダイアログを自動で閉じる",
        "ジャンプ後にアウトラインダイアログを自動的に閉じる",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
    {"m_bFunclistSetFocusOnJump",
        "ジャンプ後フォーカスを移す",
        "アウトラインからジャンプした後、エディタにフォーカスを移す",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bMarkUpBlankLineEnable",
        "空行を無視",
        "アウトライン解析で空行を無視する",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
};

// ============================================================
// 「その他」タブ — Phase 14a メタ化対象
//   CommonSetting_Others に対応。
//   ダイアログ位置の RECT は GUI 専用ロジックのため除外。
// ============================================================
inline constexpr FieldDef kOthersFields[] = {
    {"m_bIniReadOnly",
        "sakura.ini を読み取り専用",
        "INI ファイルを読み取り専用にする (上書き禁止)",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},
};

// ============================================================
// 「支援」タブ — Phase 14a メタ化対象
//   CommonSetting_Helper に対応。
//   LOGFONT (キーワードヘルプフォント) は除外。
// ============================================================
inline constexpr FieldDef kHelperFields[] = {
    // 入力補完キー
    {"m_bHokanKey_RETURN",
        "Enter で補完決定",
        "VK_RETURN で入力補完を決定",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bHokanKey_TAB",
        "Tab で補完決定",
        "VK_TAB で入力補完を決定",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},
    {"m_bHokanKey_RIGHT",
        "→ で補完決定",
        "VK_RIGHT で入力補完を決定",
        FieldKind::Bool, 0, 0, 1, nullptr, 0, true},

    // 外部ヘルプ (Phase 13b: String 対応)
    {"m_szExtHelp",
        "外部ヘルプ",
        "外部ヘルプ実行ファイルのパス",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_szExtHtmlHelp",
        "外部 HTML ヘルプ",
        "外部 HTML ヘルプ実行ファイルのパス",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_bHtmlHelpIsSingle",
        "HTML ヘルプは単一ビューア",
        "HTML ヘルプビューアを複数起動しない",
        FieldKind::Bool, 1, 0, 1, nullptr, 0, true},

    // migemo (Phase 13b: String 対応)
    {"m_szMigemoDll",
        "migemo DLL",
        "migemo DLL のパス",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
    {"m_szMigemoDict",
        "migemo 辞書",
        "migemo 辞書ファイルのパス",
        FieldKind::String, 0, 0, 0, nullptr, 0, true},
};

// ============================================================
// タブテーブル
//   Phase 11: 「全般」 (1 タブ)
//   Phase 12: 「ウィンドウ」「編集」 (3 タブ)
//   Phase 13a: 「タブバー」「バックアップ」「書式」「ファイル名表示」 (7 タブ)
//   Phase 13b: 「ファイル」 (8 タブ) + 既存タブに String フィールド追加
//   Phase 14a: 「ステータスバー」「検索」「アウトライン」「その他」「支援」 (13 タブ)
//   タブ並び順は upstream CPropCommon.cpp の AddPage 順 (18 タブ) と一致させる。
//   残り 5 タブ (キー割り当て / カスタマイズ / ツールバー / F キー / メイン・ポップアップ)
//   は専用 GUI が必要なため Phase 16+ で対応。
// ============================================================
inline constexpr TabDef kSettingsTabs[] = {
    {"general",   "全般",         kGeneralFields,  std::size(kGeneralFields),  true},
    {"window",    "ウィンドウ",   kWindowFields,   std::size(kWindowFields),   true},
    {"tabbar",    "タブバー",     kTabBarFields,   std::size(kTabBarFields),   true},
    {"edit",      "編集",         kEditFields,     std::size(kEditFields),     true},
    {"file",      "ファイル",     kFileFields,     std::size(kFileFields),     true},
    {"backup",    "バックアップ", kBackupFields,   std::size(kBackupFields),   true},
    {"format",    "書式",         kFormatFields,   std::size(kFormatFields),   true},
    {"filename",  "ファイル名表示", kFileNameFields, std::size(kFileNameFields), true},
    {"statusbar", "ステータスバー", kStatusbarFields, std::size(kStatusbarFields), true},
    {"search",    "検索",           kSearchFields,    std::size(kSearchFields),    true},
    {"outline",   "アウトライン",   kOutlineFields,   std::size(kOutlineFields),   true},
    {"others",    "その他",         kOthersFields,    std::size(kOthersFields),    true},
    {"helper",    "支援",           kHelperFields,    std::size(kHelperFields),    true},
    // 専用 GUI 必要 (Phase 16+ で対応):
    {"keyword",   "強調キーワード", nullptr, 0, false},
    {"customize", "カスタマイズ",   nullptr, 0, false},
    {"keybind",   "キー割り当て",   nullptr, 0, false},
    {"toolbar",   "ツールバー",     nullptr, 0, false},
    {"funckey",   "ファンクションキー", nullptr, 0, false},
    {"mainmenu",  "メインメニュー",  nullptr, 0, false},
    {"popupmenu", "ポップアップメニュー", nullptr, 0, false},
    {"plugin",    "プラグイン",      nullptr, 0, false},
};

inline constexpr std::size_t kSettingsTabCount = std::size(kSettingsTabs);

} // namespace sakura::settings_meta

#endif /* SAKURA_SETTINGSMETA_E5C8B6E0_4A82_4A47_B4A7_98C0DE2A1F58_H_ */
