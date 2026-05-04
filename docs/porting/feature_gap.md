# 機能差分一覧 — Win32 サクラエディタ vs wxWidgets 移植版

最終更新: 2026-05-04 (Phase 8 完了時点)

本ドキュメントは Win32 オリジナル版 (`src/`) と wxWidgets 移植版 (`sakura_wx/`)
の機能差分を一覧化し、各機能の実装フェーズ計画と取捨選択方針を明示する。

## 凡例

- ✅ 実装済み
- 🔄 実装中／部分実装
- 📅 計画あり（Phase 番号）
- ❌ **断念**（ユーザー判断、Phase 9 として計画されていたが scope out）
- ➖ 対象外（Win32 専用機能、wxWidgets 移植にそぐわない等）

## 機能対応表

### 編集基本機能

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| テキスト編集 | ✅ | ✅ | Phase 2 | wxStyledTextCtrl |
| Undo / Redo | ✅ | ✅ | Phase 2 | wxSTC 標準 |
| Cut / Copy / Paste / SelectAll | ✅ | ✅ | Phase 2 | wxSTC 標準 |
| シンタックスハイライト | ✅ | ✅ | Phase 2 | C/C++/Python/Markdown のみ。Phase 8 で type 別設定へ |
| 行番号表示 | ✅ | ✅ | Phase 2 | メニュー切替対応 |
| 折り返し | ✅ | ✅ | Phase 2 | 単純 ON/OFF。複数モード（指定桁、ウィンドウ幅）は Phase 8 |
| 自動インデント | ✅ | ✅ | Phase 2 | 前行のインデントを継承 |
| 矩形選択 | ✅ | ✅ | Phase 8 | wxSTC ネイティブ + 複数キャレット編集 + virtual space 有効化 |
| 折りたたみ | ✅ | ✅ | Phase 7 | wxSTC SetFoldFlags、C++/Python レキサー対応 |
| ブックマーク | ✅ | ✅ | Phase 7 | マージン1 sensitive + F2/F3/Shift+F3 |
| 全角空白・タブ可視化 | ✅ | ✅ | Phase 7 | SetViewWhiteSpace + U+3000 専用インジケータ |

### 検索系

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| 検索ダイアログ | ✅ | ✅ | Phase 2 | Ctrl+F |
| 置換ダイアログ | ✅ | ✅ | Phase 5-1 | Ctrl+H |
| 大小区別／単語単位／正規表現 | ✅ | ✅ | Phase 2/5 | wxSTC_FIND_* フラグ |
| すべてハイライト | ✅ | ✅ | Phase 2 | Scintilla インジケータ |
| Grep（フォルダ検索） | ✅ | ✅ | Phase 6 | wxThread + std::filesystem + wxListCtrl |
| Grep 結果ジャンプ | ✅ | ✅ | Phase 7 | ダブルクリックで該当行へ |

### ファイル操作

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| 新規・開く・保存・名前を付けて保存 | ✅ | ✅ | Phase 2 | |
| 文字コード自動判定 | ✅ | ✅ | Phase 2/4 | UTF-8/BOM, SJIS, EUC-JP, UTF-16 LE/BE |
| 文字コード手動指定（メニュー） | ✅ | ✅ | Phase 7 | 6 エンコーディングで保存／再読み込み |
| 改行コード手動指定 | ✅ | ✅ | Phase 7 | CRLF/LF/CR、ConvertEOLs で一括変換 |
| 改行コード可視化 | ✅ | ✅ | Phase 8 | SetViewEOL + メニュー切替 |
| 最近使ったファイル | ✅ | ✅ | Phase 7 | ファイルメニューのサブメニューに動的構築 |
| ファイルドロップ | ✅ | ✅ | Phase 7 | wxFileDropTarget でフレーム＋ビューに登録 |
| ファイル比較 | ✅ | ❌ | – | 後回し（外部ツール `meld` / `FileMerge` 連携で代替を推奨） |

### ウィンドウ／タブ

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| メインウィンドウ | ✅ | ✅ | Phase 2 | wxFrame |
| メニューバー | ✅ | ✅ | Phase 2/7 | 5 メニュー、Phase 7 で項目大幅拡充 |
| ステータスバー | ✅ | ✅ | Phase 2 | 3 フィールド |
| ツールバー | ✅ | ✅ | Phase 7 | `my_icons.bmp` を流用、初期 12 アイコン |
| タブ表示（複数ファイル） | ✅ | ✅ | Phase 7 | wxAuiNotebook、各タブが独立 CEditViewWx |
| 分割ウィンドウ | ✅ | ➖ | – | wxSplitterWindow で実装可だが優先度低 |

### ダイアログ・設定

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| 設定の永続化 | ✅ | ✅ | Phase 3 | INI ファイル経由（PAL IConfigStore） |
| 設定ダイアログ（基本タブ） | ✅ | ✅ | Phase 7 | wxNotebook 3 タブ（全般／フォント／キーバインド一覧） |
| 設定ダイアログ（タイプ別） | ✅ | ✅ | Phase 8 | 4 タイプ × 4 シンタックス色のオーバーライド (簡易版) |
| 設定ダイアログ（マクロ／外部ツール） | ✅ | ❌ | – | マクロ機能を断念したため不要 |
| フォント選択ダイアログ | ✅ | ✅ | Phase 2 | wxFontDialog 直接呼び出し |
| バージョン情報ダイアログ | ✅ | ✅ | Phase 2 | wxAboutBox |

### 表示系の高度機能

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| アウトライン解析 | ✅ | ✅ | Phase 8 | 簡易版（regex + wxTreeCtrl サイドパネル）。フル版は sakura_core/outline 取り込み待ち |
| 印刷／印刷プレビュー | ✅ | ✅ | Phase 8 | wxPrintout / wxPreviewFrame、ヘッダフッタ・ページ番号付き |
| ルーラー表示 | ✅ | ➖ | – | 使用頻度低、保留 |
| カラム指定線 | ✅ | ➖ | – | wxSTC EdgeColumn で対応可、優先度低 |

### 高度機能（Tier 3 — **断念**）

| 機能 | Win32 | wx | 判断 | メモ |
|---|:---:|:---:|---|---|
| マクロエンジン (PPA / JS) | ✅ | ❌ | **断念** | 上流は COM/IDispatch 依存、移植非現実的。再検討は将来の別プロジェクトで |
| 自動補完 | ✅ | ❌ | **断念** | wxSTC AutoComp は標準提供だが、辞書整備の工数大。現代的には LSP 連携が望ましいが今回は scope out |
| ファイル比較 | ✅ | ❌ | **断念** | 外部ツール（meld, FileMerge, BeyondCompare 等）連携を推奨 |
| タグジャンプ（ctags 連携） | ✅ | ❌ | **断念** | 現代的には LSP 連携の方が有用。サクラ純正の ctags 連携は移植せず |
| プラグイン機構 | ✅ | ❌ | **断念** | マクロ断念に伴い対象外 |

### IME / 入力

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| IME 基本入力 | ✅ | 🔄 | **Phase 6** | wxSTC 標準サポートに委譲。実画面確認が必要 |
| IME 候補位置追従 | ✅ | 🔄 | Phase 6 | wxWidgets が内部処理、追加実装は通常不要 |
| IME preedit 表示 | ✅ | 🔄 | Phase 6 | プラットフォーム自動 |

### CI / リリース

| 機能 | Win32 | wx | フェーズ | メモ |
|---|:---:|:---:|---|---|
| CI ビルド | ✅ | ✅ | Phase 1 | GitHub Actions（macOS / Ubuntu） |
| ユニットテスト | ✅ | ✅ | Phase 1-3 | 30 テスト（PAL/Encoding/Config/IPC） |
| macOS .dmg パッケージ | – | 📅 | **Phase 6** | リリースタグ駆動 |
| Linux AppImage | – | 📅 | **Phase 6** | linuxdeploy + GTK plugin |
| Windows インストーラ | ✅ | ➖ | – | Win32 版が既に提供 |

## 集計

- **実装済み (✅)**: 23 項目
- **部分実装 (🔄)**: 5 項目
- **計画あり (📅)**: 18 項目
- **断念 (❌)**: 8 項目（Tier 3 全部 + 関連）
- **対象外 (➖)**: 5 項目

実装計画は Phase 6-8 で完了する想定。詳細フェーズ配置は
`PORTING_GUIDE.md` のロードマップ表を参照。

## 断念機能の代替案ガイド

| 断念した機能 | 推奨代替 |
|---|---|
| マクロエンジン | エディタの自動化が必要なら外部スクリプト（zsh/python）でファイル変換、または将来 LSP コードアクションを検討 |
| 自動補完 | clangd / pylsp などの LSP を別途起動し、wx 移植版とは独立に使う方針 |
| ファイル比較 | macOS: FileMerge (Xcode 同梱) / Linux: meld / 共通: VS Code, BeyondCompare |
| タグジャンプ | LSP の "Go to Definition" 機能、または `ctags` + 別ビューア（fzf+ctags） |

## 工数見積もり（再掲）

| Phase | 主な内容 | 工数目安 |
|---|---|---|
| Phase 6 | StdAfx.h 解体、IME 検証、Grep、DMG/AppImage | 2-3 週 |
| Phase 7 | Tier 1 機能群 + 基本設定ダイアログ | 3-4 週 |
| Phase 8 | Tier 2 機能群 + タイプ別設定ダイアログ + アウトライン + 印刷 | 3-4 週 |
| **合計** | | **約 2 ヶ月（専従換算）** |
