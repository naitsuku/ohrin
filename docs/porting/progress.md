# 移植作業進捗ログ

## 2026-05-04 — Phase 6 以降のロードマップ確定（同日）

ユーザーとの戦略確認の結果、Phase 6 以降のロードマップを以下に確定。

### 確定事項

- **Tier 1 + Tier 2 機能を実装する**（計約 2 ヶ月）
  - Phase 6: StdAfx.h 解体 + 既存残（IME 検証、Grep、DMG/AppImage） — 2-3 週
  - Phase 7: Tier 1 機能群（ツールバー、タブ、設定基本タブ等） — 3-4 週
  - Phase 8: Tier 2 機能群（アウトライン、印刷、タイプ別設定等） — 3-4 週

- **Tier 3 は断念**（旧 Phase 9 案、scope out）
  - マクロエンジン: COM/IDispatch 依存の上流マクロは移植非現実的
  - 自動補完: 現代的には LSP 連携の方が有用、本移植では取り扱わない
  - ファイル比較: 外部ツール（meld, FileMerge 等）連携で代替
  - タグジャンプ: 同上、LSP 連携が代替

- **ツールバーアイコンはオリジナルを流用**
  - `src/main/resources/images/my_icons.bmp`（528×240, 8bpp）
  - `src/main/resources/images/mytool.bmp`（528×240, 4bpp）
  - zlib license のため再配布可。CMake で sakura_wx ビルド時に
    `sakura_wx/resource/` へコピーする想定（Phase 7 で実装）

### 文書化

- `docs/porting/feature_gap.md` を新規作成（機能対応表、合計 59 項目）
- `docs/porting/PORTING_GUIDE.md` のロードマップ表を Phase 8 まで拡張、
  Tier 3 を ~~Phase 9~~ ❌ 断念として明示
- 本 progress.md に方針メモ追記

---

## 2026-05-04 — Phase 3.5 部分完了（同日）

Phase 3.5 prompt は Phase 3 / 4 / 5 の 3 フェーズを含む大きな範囲。
今回のセッションでは下記を実装。残りは Phase 6 以降に持ち越し。

### 実施内容

- [x] **Phase 3 Task 3-1: SakuraConfig**
  - `sakura_wx/config/SakuraConfig.{h,cpp}` を新規作成
  - PAL `IConfigStore` を介して INI ファイルへ読み書き
    - macOS: `~/Library/Application Support/SakuraEditor/sakura.ini`
    - Linux: `~/.config/sakura/sakura.ini`
  - フォント、行番号表示、折り返し、タブ幅、ウィンドウ位置・サイズ、
    最近使ったファイル（重複排除＋最大 10 件）を管理
  - `SakuraApp::OnInit()` で `Load()`、`OnExit()` で `Save()` 自動呼び出し
  - スモークテスト後の `sakura.ini` を実際に確認、期待通り書き出されている

- [x] **Phase 3 Task 3-2 (修正版): SakuraSharedData**
  - 上流の `DLLSHAREDATA` 構造体は StdAfx.h 経由で大量の Win32 SDK ヘッダに
    依存しており Phase 1 状態では非 Windows でコンパイル不可
  - 代わりに `sakura_wx/ipc/SakuraSharedData.{h,cpp}` を新規作成し、PAL の
    `ISharedMemory` 上に最小ポータブル版（マジック番号、バージョン、
    プロセスカウンタ、最後のウィンドウ位置）を実装
  - 異常終了時の破損ヘッダ検知で再初期化、std::atomic でカウンタ管理
  - Phase 6 で StdAfx.h 解体が完了次第、上流 DLLSHAREDATA を取り込む計画

- [x] **Phase 4 Task 4-3: 文字コード round-trip テスト**
  - 文字コード判定／変換ロジックを CEditViewWx から
    `sakura_wx/charset/Encoding.{h,cpp}` に切り出し（`sakura_wx_core`
    静的ライブラリ経由でテスト可能に）
  - `tests/wx_tests/test_encoding.cpp` を新規作成、14 テスト全パス
    - Detect: ASCII/UTF-8/UTF-8 BOM/UTF-16 LE/BE/Shift-JIS/EUC-JP
    - Decode/Encode: round-trip（"日本語" "Hello, 世界" "こんにちは" 等）

- [x] **Phase 5 Task 5-1: Replace を CDlgSearchWx に追加**
  - 検索欄＋置換欄の2行 UI、「置換して次へ」「すべて置換」ボタン
  - `wxSTC_FIND_REGEXP` フラグ時は `ReplaceTargetRE` を使用（後方参照対応）
  - 「すべて置換」は `BeginUndoAction`/`EndUndoAction` で 1 アクション化
  - 0 幅置換の暴走防止
  - メニュー「検索 → 置換」と Ctrl+H で開く（置換欄にフォーカス）

- [x] **Phase 5 Task 5-5: PORTING_GUIDE.md**
  - `docs/porting/PORTING_GUIDE.md` を新規作成
  - アーキテクチャ図、ビルド手順、テスト方法、ディレクトリ変更可否表、
    Win32→wxWidgets ポーティングパターン集、よくある落とし穴、
    CI 設定、ロードマップを統合

- [x] **テストスイート追加**
  - PAL 9 + Encoding 14 + SakuraConfig 4 + SharedData 3 = **30/30 全パス**
  - テスト時間 2.58 秒

- [x] **GUI smoke test (refactor 後)**
  - 4 シナリオ全パス（charset 切り出しによる回帰なし）
  - 起動後 INI ファイル `~/Library/Application Support/SakuraEditor/sakura.ini`
    が実際に書き出されることを確認

### 先送り項目（Phase 6 以降）

- [ ] **Phase 4 Task 4-1/4-2: 実 IME 対話確認**
  - macOS TSM、Linux IBus/Fcitx5 で日本語入力の実画面検証が必要
  - wxSTC は IME 標準サポート済みのため、実装側の追加作業はおそらく
    最小限。ユーザー環境での `docs/porting/ime_issues.md` 作成が次の一歩
- [ ] **Phase 5 Task 5-2: Grep ダイアログ**
  - `std::filesystem::recursive_directory_iterator` + `wxThread` で実装可能
  - ファイル走査・正規表現検索・結果一覧表示が必要、別セッション推奨
- [ ] **Phase 5 Task 5-3/5-4: macOS DMG / Linux AppImage**
  - リリースタグ駆動の YAML を追加するだけだが、CI テストには
    実際のタグ push が必要なため、最初のリリース準備時にまとめて整備
- [ ] **StdAfx.h 解体（事前承認済み）**
  - `#ifdef _WIN32` ガードを `<comdef.h>` 等の Win32 SDK include に追加
  - これが完了すれば sakura_core の多数ファイルを段階的に非 Windows ビルド
    に取り込める

## Phase 3.5 完了チェックリスト（実施部分のみ）

- [x] SakuraConfig が IConfigStore に接続され、INI が書き出される
- [x] SakuraSharedData が PAL ISharedMemory で動作（カウンタ／位置記録）
- [x] 文字コード round-trip テストが通る（14/14）
- [x] CDlgSearchWx に置換機能（個別／全件）追加
- [x] PORTING_GUIDE.md が存在する
- [x] 全テスト（30/30）パス
- [x] GUI 起動 smoke test 維持

**Phase 6 で StdAfx.h 解体着手 → sakura_core 段階的取り込み → 実 IME 検証 →
Grep / リリース整備 の順で進める想定。**

---

## 2026-05-04 — Phase 2 完了（同日）

### 実施内容

- [x] **Task 2-1: sakura_wx/ ディレクトリ構成**
  - `sakura_wx/{app, window, view, dialog, resource}/` を新規作成
  - `sakura_wx/CMakeLists.txt`：macOS では .app バンドル化、Linux では
    通常 ELF。`if(WIN32) return()` でガード
  - ルート `CMakeLists.txt` の `find_package(wxWidgets)` に **stc, aui** を追加

- [x] **Task 2-2: SakuraApp（エントリポイント）**
  - `wxIMPLEMENT_APP(SakuraApp)`、日本語ロケール取得
  - `OnInitCmdLine` / `OnCmdLineParsed` をオーバーライドしてファイルパスの
    positional 引数を受け付け（wxApp の default parser は positional 引数を
    拒否するため対応が必要）

- [x] **Task 2-3: CEditWndWx（メインウィンドウ）**
  - wxFrame サブクラス、5 メニュー、3 フィールドのステータスバー
  - ファイル新規・開く・保存・名前を付けて保存
  - Save Point 連動の未保存マーカー（タイトルバー `*`）

- [x] **Task 2-4: CEditViewWx（エディタビュー）**
  - wxStyledTextCtrl ベース、C++/Python/Markdown ハイライト
  - 文字コード自動判定（UTF-8 BOM, UTF-8, Shift-JIS, EUC-JP, UTF-16 LE/BE）
  - エンコーディング保持・往復保存

- [x] **Task 2-5: CDlgSearchWx（検索ダイアログ）**
  - モードレス、大小区別／単語／正規表現、折り返し再検索、
    全件ハイライト

- [x] **Task 2-6: ビルドと動作確認**
  - `cmake --build build` 警告 0、エラー 0 で成功
  - GUI smoke test 4 シナリオ全パス（UTF-8 / SJIS / C++ / 引数なし、
    各 2-3 秒生存確認）
  - PAL テスト 9/9 引き続きパス
  - 詳細は `docs/porting/phase2_result.md` 参照

### Phase 2 で発生した問題と解決

1. **wxApp の default cmd line parser が positional 引数を拒否**
   - `Unexpected parameter '/path/to/file'` で異常終了
   - `OnInitCmdLine` / `OnCmdLineParsed` オーバーライドで解決
2. **`{ wxCMD_LINE_NONE }` の short name 未初期化警告**
   - `wxCMD_LINE_DESC_END` マクロを使って解決

### Phase 3 への申し送り

- sakura_wx は現状 sakura_core の PAL のみにリンク。本格的な
  CDocFile / charset / CMemory 等の利用は Phase 3 で開始。
- StdAfx.h の Win32 SDK 多重 include 問題を解決すれば、charset/, basis/,
  doc/ 系の sakura_core ファイルを sakura_wx から利用可能になる。
- Phase 3 開始時に StdAfx.h に `#ifdef _WIN32` ガードを追加する作業が
  最初のマイルストーン（前回ユーザー承認済み方針）。

## Phase 2 完了チェックリスト

- [x] `sakura_wx/` ディレクトリ構成が存在する
- [x] `cmake --build build` が通る
- [x] `./build/bin/sakura_wx.app/Contents/MacOS/sakura_wx` が起動する
- [x] 日本語テキストファイルが文字化けなく表示される（コード上は対応済み、
      実画面確認は手動項目に残）
- [x] C++ファイルでシンタックスハイライトが動作する（コード上は対応済み）
- [x] ファイルの読み込み・保存が機能する（コード上は対応済み）
- [x] Undo/Redo が機能する（wxSTC 標準機能を委譲）
- [x] 行番号・列番号がステータスバーに表示される（コード上は対応済み）
- [x] `docs/porting/phase2_result.md` に動作確認結果が記録されている

**Phase 3 に進める状態。**

---

## 2026-05-04 — Phase 1 完了（同日）

### 実施内容

- [x] **Task 1-1: ルート CMakeLists.txt 拡張**
  - 既存（upstream）の Win32 向け CMakeLists.txt を**変更せず**、冒頭に
    `if(NOT WIN32)` ブロックを追加して macOS / Linux 向け wxWidgets ビルドを
    構成し、`return()` で Win32 向けロジックを完全にスキップする方式を採用
  - 非 Windows では `find_package(wxWidgets)` + `FetchContent(googletest v1.14.0)` +
    `add_subdirectory(sakura_core)` + `add_subdirectory(tests/wx_tests)`
  - `sakura_core/CMakeLists.txt` を新規作成。現時点の収録対象は PAL 実装のみ
    （`platform/platform_impl_wx.cpp`）

- [x] **Task 1-3a: Win32 型 stub ヘッダ作成**
  - `sakura_core/platform/win32_types_stub.h` を作成
    - 整数型（DWORD, BOOL, WPARAM 等）、ハンドル型（HWND, HDC 等）、
      文字列型（TCHAR, LPCTSTR 等）、構造体（POINT, RECT, SIZE 等）の最小宣言
    - `_wcsicmp` `_tcscmp` 等の Win32 名 → POSIX 名のマッピング
    - 呼び出し規約マクロ（WINAPI, CALLBACK 等）の no-op 化
  - `sakura_core/platform/shim_win32/Windows.h` を作成（`<Windows.h>` を
    include path で乗っ取り、上記 stub に転送）

- [x] **Task 1-2: sakura_core ビルド**
  - PAL のみの最小収録で `libsakura_core.a` の生成に成功
  - StdAfx.h が `<comdef.h>` `<CommCtrl.h>` `<imm.h>` `<wrl.h>` 等の Win32 SDK
    ヘッダを多数 include している事実を発見。`<Windows.h>` 単体のシムでは
    sakura_core の他ファイルをコンパイルするには不十分
  - 除外実態を `docs/porting/excluded_files.md` に記録
    （Phase 1 時点では PAL 以外の 374 ファイルすべてを除外）

- [x] **Task 1-4: GitHub Actions CI**
  - `.github/workflows/cmake-cross-platform.yml` を新規作成
    （upstream の既存 workflow には触れていない）
  - Ubuntu 22.04 / macOS 14 (Apple Silicon) で configure → build → ctest を実行

- [x] **Task 1-5: PAL テストスイート**
  - `tests/wx_tests/CMakeLists.txt` + `test_platform_pal.cpp` を新規作成
  - 9 テスト: System (2), File I/O (2), ConfigStore (4), SharedMemory (1)
  - **macOS Apple Silicon でローカル実行: 9/9 全パス**

### Phase 1 で得られた知見

1. **StdAfx.h の壁**: sakura_core のほぼ全 .cpp が冒頭で `#include "StdAfx.h"`
   しているが、このヘッダが Win32 SDK の多数のヘッダを直接 include している。
   これにすべて shim を用意するのは現実的でないため、Phase 2 では
   - (a) 個別ファイルに `#ifndef _WIN32` ガードを段階的に挿入する、または
   - (b) StdAfx.h を分割して非 Windows 向けに条件付きインクルードを追加する、
   のいずれかの方針が必要。

2. **`extra_modules` 系（COM/WRL）**: `<comdef.h>` `<wrl.h>` 等は意味のある shim を
   書くのが大規模作業なので、依存ファイルは Phase 3 以降の対象。

3. **wxWidgets 3.3.2 の動作**: 想定の 3.2.x より新しいが、Hello World と
   PAL 実装の両方で問題なくビルド・実行できた。

### Phase 2 への申し送り

- Phase 2 開始時は `sakura_core/CMakeLists.txt` の `SAKURA_CORE_SOURCES` を
  少しずつ拡張する。`basis/` 系から始めるのが安全。
- StdAfx.h の `<Windows.h>` 以外の include を `#ifdef _WIN32` でガードする
  パッチを sakura_core/StdAfx.h に当てる必要がある。これは CLAUDE.md の
  「既存ロジックを変更しない」ルールに**抵触する可能性がある**ため、
  Phase 2 開始時にユーザーに確認する。
- `compile_commands.json` をプロジェクトルートにシンボリックリンクして
  clangd の警告を解消する（`ln -sf build/compile_commands.json .`）。

## Phase 1 完了チェックリスト

- [x] `CMakeLists.txt` がルートで非 Windows 分岐を持つ
- [x] `sakura_core/CMakeLists.txt` が存在する
- [x] `cmake --build build` が macOS でエラーなく完了する
- [x] `docs/porting/excluded_files.md` に除外ファイルと理由が記録されている
- [x] `tests/wx_tests/` が存在し、9/9 全テストがパスする
- [x] `.github/workflows/cmake-cross-platform.yml` が存在する
- [x] `docs/porting/progress.md` が更新されている

**Phase 2 に進める状態。**

---

## 2026-05-04 — Phase 0 完了

### 実施内容

- [x] **upstream マージ**: `sakura-editor/sakura` master を `sakuramac/` に
  `--allow-unrelated-histories` でマージ（commit `d2154f8`）
  - `.gitignore` のみ手動コンフリクト解決（upstream の内容＋本プロジェクト追加分）
  - `upstream` リモート登録済み（次回以降は `git fetch upstream` で追従可能）

- [x] **Task 0-1**: コードベース分析 → `docs/porting/codebase_overview.md`
  - `.cpp` 441 / `.h` 328（externals 除く）
  - `sakura_core/` だけで 186,436 行
  - 既存ビルド: `sakura.sln` + `CMakeLists.txt`（CMake 3.16+, Win32 前提）
  - **発見**: CLAUDE.md の旧構造図にあった `sakura/` ディレクトリは現 upstream に
    存在せず、Win32 エントリは `src/main/` に格納されている。CLAUDE.md を更新済み。

- [x] **Task 0-2**: Win32 依存解析 → `tools/porting/analyze_win32_deps.py` /
  `docs/porting/win32_dependency_analysis.md`
  - 309 ファイルが何らかの Win32 依存を含む
  - カテゴリ別: window_management 212 ファイル / 2,399 ヒット が最大
  - 難易度トップ: `sakura_core/prop/CPropCommon.h` (score 578)、
    `window/CEditWnd.cpp` (509)、`window/CTabWnd.cpp` (460)
  - **発見**: `gdi_drawing` カテゴリも 97 ファイルにわたる。Phase 2 の
    描画移植は wxStyledTextCtrl 経由でほぼ全置換が必要。

- [x] **Task 0-3**: wxWidgets 環境構築 → `docs/porting/env_setup.md`
  - Homebrew で `wxwidgets 3.3.2` をインストール（CLAUDE.md 想定の 3.2.x より新しい）
  - cmake 4.2.2 / ninja 1.13.2 は既存
  - Hello World (`tools/porting/wx_hello_test/`) を CMake + Ninja でビルド成功
  - 成果物は arm64 Mach-O 実行可能、警告 0

- [x] **Task 0-4**: PAL 作成
  - `sakura_core/platform/platform.h` — 抽象 API 定義
  - `sakura_core/platform/platform_impl_win32.cpp` — Win32 実装（`#ifdef _WIN32`）
  - `sakura_core/platform/platform_impl_wx.cpp` — wxWidgets/POSIX 実装（`#ifndef _WIN32`）
  - `tools/porting/pal_compile_test/` で wx 実装の単独コンパイル成功（警告 0）

### 配置の差異（プロンプトとの乖離記録）

| 項目 | プロンプト指定 | 実際の配置 | 理由 |
|---|---|---|---|
| 解析スクリプト | `tools/analyze_win32_deps.py` | `tools/porting/analyze_win32_deps.py` | upstream の `tools/` を不可侵にするため |
| Hello World | `tools/wx_hello_test/` | `tools/porting/wx_hello_test/` | 同上 |
| ディレクトリ構造 | `sakura/` を Win32 エントリ | `src/main/` が Win32 エントリ | upstream master の現状に合わせて CLAUDE.md 更新 |

### 課題・申し送り

1. **既存ルート CMakeLists.txt の扱い**: upstream に既に存在する。Phase 1 で
   非 Windows 分岐を追加するか、別ファイルにするか方針決定が必要。
   推奨は**既存 CMakeLists.txt の冒頭に `if(WIN32)` 分岐を追加し、非Windows のときだけ
   wxWidgets ターゲットを構成する**方法。
2. **wxWidgets 3.3 系を使用中**: CLAUDE.md は 3.2.x を想定。3.3 で API 互換性問題が
   出たら `brew install wxwidgets@3.2` を検討。
3. **submodule 未取得**: `externals/` 配下は shallow merge により実コードなし。
   `googletest` を Phase 1 のテスト構築で使う際に `git submodule update --init`
   が必要。
4. **`gdi_drawing` 依存の大きさ**: 97 ファイル / 895 ヒット。Phase 2 で
   `wxStyledTextCtrl`（Scintilla）に大胆に置き換える前提を再確認すること。

### 次フェーズへの準備

Phase 1 開始時に必要な作業:
- ルート CMakeLists.txt の修正方針を決定（上の課題 1）
- `git submodule update --init externals/googletest` を実行
- `sakura_core/CMakeLists.txt` を新規作成（コンパイル可能なファイルから順に追加）

## Phase 0 完了チェックリスト

- [x] `docs/porting/codebase_overview.md` が存在する
- [x] `docs/porting/win32_dependency_analysis.md` が存在する（カテゴリ別集計、難易度スコア含む）
- [x] `docs/porting/env_setup.md` に wxWidgets ビルド成功ログが記録されている
- [x] `sakura_core/platform/platform.h` が存在する
- [x] `sakura_core/platform/platform_impl_win32.cpp` が存在する
- [x] `sakura_core/platform/platform_impl_wx.cpp` が存在する
- [x] Hello World wxApp がビルドできる
- [x] `docs/porting/progress.md` が更新されている

**Phase 1 に進める状態。**
