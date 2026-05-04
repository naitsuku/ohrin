# 移植作業中の Issue ログ

エラー・回避策・TODO のメモ。CLAUDE.md の方針：

> エラーメッセージを `docs/porting/issues.md` に記録
> 回避策が見つからない場合は TODO コメントを残して次のタスクへ進む
> Win32依存で移植困難な機能は stub 実装（空実装）にして後回しにする

## Phase 0 で発生した Issue

### I-001: clangd が `<filesystem>` / wxWidgets ヘッダを認識しない

**症状**: VSCode の clangd が `sakura_core/platform/*.h|cpp` で
`No member named 'filesystem' in namespace 'std'` や `'wx/wx.h' file not found` を表示。

**原因**: プロジェクトルートに `compile_commands.json` がなく、clangd が
コンパイラフラグ（`-std=c++17`、wxWidgets include path）を取得できない。

**回避策**:
- 実害なし（CMake ビルドは通る）
- Phase 1 で `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` を設定し、`build/compile_commands.json`
  をプロジェクトルートにシンボリックリンクする計画。

**ステータス**: Phase 1 で解消予定

---

### I-002: wxWidgets 3.3.2 と CLAUDE.md 想定（3.2.x）の差異

**症状**: macOS Homebrew が提供する wxwidgets は 3.3.2 系。CLAUDE.md は 3.2.x を想定。

**回避策**: 現時点で問題は発生していない。Hello World は 3.3.2 で正常ビルド。
3.2 固有 API を使った場合に問題が出る可能性があるが、本移植では使わない予定。

**ステータス**: 監視継続

---

### I-003: ルート CMakeLists.txt が既に存在（upstream 由来）

**症状**: Phase 1 prompt は「ルートに新規作成」と書かれているが、upstream に既に
`CMakeLists.txt`（CMake 3.16+, Windows 前提）がある。

**対応**: 既存ファイルの冒頭に `if(NOT WIN32) ... return() endif()` ブロックを挿入。
非 Windows ではこのブロックで wxWidgets ビルドを構成し、Win32 向けロジックは
`return()` で完全にスキップ。Win32 ビルドの挙動は変化なし。

**ステータス**: ✅ Phase 1 で解決

---

## Phase 1 で発生した Issue

### I-004: StdAfx.h が大量の Win32 SDK ヘッダを直接 include

**症状**: `sakura_core/StdAfx.h` が `<Windows.h>` 以外にも `<windowsx.h>`,
`<comdef.h>`, `<CommCtrl.h>`, `<HtmlHelp.h>`, `<imm.h>`, `<oaidl.h>`,
`<shellapi.h>`, `<ShlDisp.h>`, `<ShlObj.h>`, `<Uxtheme.h>`, `<vsstyle.h>`,
`<wrl.h>`, `<wrl/client.h>` を直接 include している。
sakura_core のほぼ全 .cpp は冒頭で StdAfx.h を include しているため、これらすべての
shim を用意しない限り非 Windows でコンパイルできない。

**回避策**: Phase 1 では PAL 実装のみを sakura_core/CMakeLists.txt に収録。
Phase 2 で対応方針を決定する：
  - (a) StdAfx.h 自体に `#ifdef _WIN32` ガードを追加（既存ロジック変更に該当する
    可能性があり、CLAUDE.md ルール 2 への抵触をユーザー確認が必要）
  - (b) 各 .cpp 個別に StdAfx.h の代わりに別 PCH を使う条件分岐を入れる
  - (c) 主要な Win32 SDK ヘッダすべての shim を sakura_core/platform/shim_win32/
    に整備する（COM/WRL を含むため大規模）

**ステータス**: Phase 2 で対応

### I-006: 上流 DLLSHAREDATA の取り込みは StdAfx.h 解体待ち

**症状**: `sakura_core/env/DLLSHAREDATA.h` が `debug/Debug2.h`,
`config/maxdata.h`, `env/CAppNodeManager.h` 等を経由して StdAfx.h に依存し、
非 Windows ビルドで使用不可。

**回避策**: Phase 3 では sakura_wx 独自の最小共有データ構造
`sakura_wx::ipc::SakuraSharedData`（マジック番号、カウンタ、ウィンドウ位置）
を PAL ISharedMemory 上に実装。フル機能の DLLSHAREDATA 統合は Phase 6 で
StdAfx.h ガード追加後に着手する。

**ステータス**: Phase 6 で対応

---

### I-007: 実 IME 対話確認は環境依存テスト

**症状**: macOS TSM、Linux IBus/Fcitx5 での日本語入力動作は実画面確認が
必要で、ヘッドレスでは検証不可能。

**回避策**: コード上は wxStyledTextCtrl の標準 IME サポートを利用。
実環境テストはユーザー側で実施し、`docs/porting/ime_issues.md` に問題が
あれば記録してから Phase 4 Task 4-2 を再開する流れ。

**ステータス**: Phase 6 で対応（要ユーザー操作）

---

### I-005: シム Windows.h の case sensitivity

**症状**: macOS の APFS は通常 case-insensitive だが、Linux ext4 は case-sensitive。
`#include <Windows.h>` と `#include <windows.h>` の混在に対応するため、Linux で
両方を提供する必要がある。

**回避策**: 現在は `Windows.h` のみ作成。Linux ビルド時に CMake で
シンボリックリンクを生成するステップを Phase 2 で追加する予定。

**ステータス**: Phase 2 で対応
