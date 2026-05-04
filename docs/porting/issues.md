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

**回避策**: Phase 1 で既存ファイルの冒頭に `if(WIN32)` 分岐を入れて、
非 Windows 時のみ wxWidgets ターゲットを構成する形に拡張する。

**ステータス**: Phase 1 着手時に決定
