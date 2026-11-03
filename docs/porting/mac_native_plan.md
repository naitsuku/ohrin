# macOS ネイティブ移植計画（2026-11-04 方針転換）

## なぜ転換したか

wxWidgets 版（Phase 0〜15b）は、sakura_core のうち PAL と `basis/` の 9 ファイルしか
ビルドしておらず、エディタ本体は wxStyledTextCtrl（Scintilla）による新規実装だった。
サクラ固有の挙動（CLayoutMgr の折り返し・禁則、CViewCommander の全コマンド、
色分け戦略、フリーカーソル、矩形選択、スマートインデント等）はすべて sakura_core にあり、
作り直しでは再現性の上限が低い。

新方針では sakura_core をフォークし、**本物の編集エンジンを mac で動かす**。

## 構成

- `winport/`: エディタ本体が使う Win32 API（概算 280 種、sakura_core 全体で約 400 種）を
  AppKit / CoreText 上に実装する互換層
- ダイアログ（51 個）は互換層で再現しない。mac ネイティブで必要なものから作り直す
- プロセス: 1 プロセス複数ウィンドウを第一候補（`TSingleInstance<CEditWnd>` を
  互換層のメッセージ配送時にコンテキスト切替で扱う案）。不成立なら 1 プロセス 1 ウィンドウ
- マクロ: キーマクロ（本体そのまま）+ JavaScriptCore に `CSMacroMgr::m_MacroFuncInfoArr`
  を公開して WSH JScript 互換の JS マクロ

## wchar_t の扱い

macOS の wchar_t は 4 バイト。sakura_core は WCHAR を UTF-16 単位として扱う。

- `-fshort-wchar` 単独は**不可**（実験済み）: システム libc++.dylib の
  `basic_string<wchar_t>` 実体が 4 バイト前提で、append / compare 等が壊れる。
  `_LIBCPP_DISABLE_EXTERN_TEMPLATE` でも dylib 側シンボルに解決されて改善しない。
- UTF-32 案（1 要素 = 1 コードポイント）は不採用: サロゲート判定
  `IsUtf16SurrogHi` が `static_cast<unsigned short>(wc)` で下位 16 ビットだけを見るため
  U+2D834 などを誤判定する。列数・インデックスも Windows とずれる。
- 採用案: **wchar_t 4 バイトのまま、各要素に UTF-16 コード単位を格納**する
  （サロゲートペアは 2 要素）。sakura_core のサロゲート処理・列計算・インデックスが
  Windows と同一になり、NSString（unichar = 16 bit）との変換も単純なコピーで済む。
- 修正が要るのは「CNativeW のメモリを UTF-16LE のバイト列として直接扱う」境界だけ:
  `charset/CUnicode.cpp`（UTF-16LE/BE の入出力。現状 `SetRawDataHoldBuffer` で
  バイト列をそのまま CNativeW に入れている）、`_os/CClipboard.cpp`、`_os/CDropTarget.cpp`、
  `charset/` の各コーデックの一部（reinterpret_cast 箇所、計 15 ファイル程度）。
  `sizeof(wchar_t)` を使った長さ計算はそのまま 4 バイトで正しく動く。

## 8 週間計画（MVP）

| 週 | 内容 | ゲート |
|---|---|---|
| 1 | wchar_t 方式決定。sakura_core 全体のコンパイル・リンク試行、必要 API 確定 | 判断 1: 非 GUI 層が mac でビルドでき、上流テストの一部が通る見込み |
| 2 | kernel32 系（ファイル、文字コード変換、ini、共有メモリ、時刻）。文書／レイアウト系を取り込み上流 GoogleTest を通す | 1 プロセス複数ウィンドウの成立可否 |
| 3–4 | user32 / gdi32 の中核（HWND⇔NSView、メッセージ、WM_PAINT、ExtTextOut→CoreText、キャレット、スクロール、マウス、タイマー） | 判断 2: 本物の CEditView で日本語ファイルを開いて編集できる |
| 5 | IME（NSTextInputClient → WM_IME_* / Imm*）、Cmd→Ctrl 変換、MainMenu.ini → mac メニューバー | |
| 6 | mac ネイティブダイアログ（検索・置換・Grep・ジャンプ・開く/保存・フォント）、クリップボード、ドロップ | |
| 7 | キーマクロ、JS マクロ（JavaScriptCore） | |
| 8 | タブ・複数ウィンドウ、.app / DMG、Windows 版との挙動比較、安定化 | |

MVP 後（+2〜4 ヶ月）: 共通設定・タイプ別設定画面、印刷、残りのダイアログ、プラグイン、
Python マクロ、見た目合わせ。対象外: PPA マクロ、VBScript マクロ。

## 進捗

- Week 1（2026-11-04）: 判断 1 通過。sakura_core 全 375 ファイルが winport ヘッダで
  コンパイルでき、リンク未解決は Win32 / CRT の 528 シンボルのみ。
  詳細は progress.md と `winport_api_inventory.md`。上流テスト実行は Week 2 冒頭に繰り越し。
