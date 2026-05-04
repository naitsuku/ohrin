# 移植作業進捗ログ

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
