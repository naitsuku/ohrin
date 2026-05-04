# 移植作業進捗ログ

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
