# サクラエディタ wxWidgets 移植 — コントリビューターガイド

最終更新: 2026-05-04

本ドキュメントは、サクラエディタの wxWidgets 移植にコントリビュートする
開発者向けの技術ガイドです。

> **対応プラットフォーム**: macOS 13+ (Apple Silicon/Intel)、
> Ubuntu 22.04+、Fedora 38+
> **元コード**: Win32 専用版（`src/main/`、`sakura.sln`）は変更禁止

---

## 1. アーキテクチャ全体像

```
┌──────────────────────────────────────────────────────────┐
│ sakura_wx/  (wxWidgets フロントエンド、新規)              │
│  ├── app/        SakuraApp                                │
│  ├── window/     CEditWndWx (wxFrame)                     │
│  ├── view/       CEditViewWx (wxStyledTextCtrl)           │
│  ├── dialog/     CDlgSearchWx (検索/置換)                 │
│  ├── config/     SakuraConfig (INI 設定)                  │
│  ├── ipc/        SakuraSharedData (共有メモリ)            │
│  └── charset/    Encoding (文字コード判定／変換)          │
└──────────────────────────────────────────────────────────┘
                       │ uses
                       ▼
┌──────────────────────────────────────────────────────────┐
│ sakura_core/  (上流の共有ロジック、変更最小限)            │
│  └── platform/ (Phase 0 で新規追加)                       │
│       ├── platform.h        — PAL 抽象 API                │
│       ├── platform_impl_win32.cpp  — Win32 実装           │
│       ├── platform_impl_wx.cpp     — wxWidgets/POSIX 実装 │
│       ├── win32_types_stub.h       — Win32 型 stub        │
│       └── shim_win32/Windows.h     — Windows.h シム       │
└──────────────────────────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────┐
│ src/  (Win32 エントリ、絶対変更禁止)                      │
│  └── main/cpp, msbuild, resources                         │
└──────────────────────────────────────────────────────────┘
```

### 中心思想 — Platform Abstraction Layer (PAL)

`sakura_core/platform/platform.h` で抽象化される対象:

| 機能 | Win32 実装 | 非 Windows 実装 |
|---|---|---|
| File I/O | `CreateFile`/`ReadFile` | `<fstream>` |
| 設定ストア | レジストリ | `wxFileConfig` (INI) |
| クリップボード | `OpenClipboard` | `wxTheClipboard` |
| ダイアログ | `MessageBoxW` 等 | `wxMessageBox` 等 |
| システム情報 | `SHGetSpecialFolderPath` | `XDG_CONFIG_HOME` 等 |
| 共有メモリ (IPC) | `CreateFileMapping` | `shm_open` + `mmap` |

`#ifdef _WIN32` で実装を選択。両実装が同じインターフェース `SakuraPlatform::*`
を提供する。

---

## 2. 開発環境のセットアップ

### macOS

```bash
brew install wxwidgets cmake ninja
```

要件: macOS 13+, Xcode Command Line Tools, wxWidgets 3.2.x または 3.3.x。

### Ubuntu 22.04

```bash
sudo apt update
sudo apt install -y \
  libwxgtk3.2-dev libwxgtk-media3.2-dev \
  cmake ninja-build pkg-config \
  libgtk-3-dev build-essential
```

### Fedora 38+

```bash
sudo dnf install -y \
  wxGTK-devel cmake ninja-build \
  gtk3-devel gcc-c++ git
```

---

## 3. ビルド方法

### 通常ビルド（macOS / Linux）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

成果物:
- `build/bin/sakura_wx` (Linux) または
  `build/bin/sakura_wx.app/Contents/MacOS/sakura_wx` (macOS)
- `build/bin/sakura_wx_tests` — GoogleTest 実行ファイル
- `build/lib/libsakura_core.a`, `libsakura_wx_core.a`

### Windows ビルド（既存）

`sakura.sln` を Visual Studio で開く。CMake は使わない。
ルート `CMakeLists.txt` は `if(NOT WIN32) return() endif()` で
非 Windows 時のみ wxWidgets 分岐を有効にしているため、Windows ビルドへの
影響はない。

---

## 4. テストの実行

```bash
# 全テスト
ctest --test-dir build --output-on-failure

# 単一テスト
ctest --test-dir build -R Encoding_Decode --output-on-failure

# テストバイナリ直接実行（GoogleTest CLI が使える）
build/bin/sakura_wx_tests --gtest_filter="SakuraConfig.*"
```

現在のテスト数: **30** (PAL 9, Encoding 14, SakuraConfig 4, SharedData 3)

---

## 5. ディレクトリの「触ってよい／触ってはいけない」

| パス | 変更可否 | メモ |
|---|---|---|
| `sakura_wx/` | ✅ 自由 | 本移植の主舞台 |
| `sakura_core/platform/` | ✅ 自由 | 新規追加領域 |
| `tests/wx_tests/` | ✅ 自由 | テスト追加歓迎 |
| `tools/porting/` | ✅ 自由 | 移植用ツール |
| `docs/porting/` | ✅ 自由 | ドキュメント |
| `sakura_core/` その他 | ⚠️ 限定 | `#ifdef _WIN32`/`#ifdef SAKURA_PLATFORM_WX` ガード追加のみ。既存ロジック変更は NG |
| `CMakeLists.txt` (ルート) | ⚠️ 限定 | 既存 Win32 分岐は無変更、追加は `if(NOT WIN32)` 内に |
| `src/`, `sakura_lang/`, `sakura.sln` | ❌ 禁止 | Win32 エントリ、上流のまま |
| `tools/`（`porting/` 除く）, `externals/`, `help/`, `installer/` | ❌ 禁止 | 上流資産 |

---

## 6. Win32 → wxWidgets ポーティング パターン集

### パターン A: TCHAR / wchar_t

```cpp
// Before
TCHAR szPath[MAX_PATH];
_tcscpy(szPath, text);

// After (sakura_core/platform/win32_types_stub.h で TCHAR=wchar_t に統一済み)
TCHAR szPath[MAX_PATH];          // そのまま使える
_tcscpy(szPath, text);            // win32_types_stub.h でインライン関数提供
```

### パターン B: Windows API → PAL

```cpp
// Before
HANDLE h = CreateFileW(path, GENERIC_READ, ...);
ReadFile(h, buf, size, &read, nullptr);

// After
std::vector<char> buf;
SakuraPlatform::File::ReadBytes(path, buf);
```

### パターン C: レジストリ → IConfigStore

```cpp
// Before
HKEY hKey;
RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\sakura", 0, KEY_READ, &hKey);
RegQueryValueExW(hKey, L"FontFace", nullptr, nullptr, ...);

// After
auto store = SakuraPlatform::CreateConfigStore(L"general");
SakuraPlatform::NativeString face;
store->GetString(L"font_face", face);
```

### パターン D: GUI 部分は wxWidgets で書き直す

GDI 描画／ウィンドウ管理は意味のある shim を書くのが大規模なため、
**該当機能は sakura_wx/ で wxWidgets ネイティブ実装に置き換え**る方針。

例: `sakura_core/window/CEditWnd.cpp`（HWND 直接操作多数）は除外し、
`sakura_wx/window/CEditWndWx.cpp` に wxFrame ベースの新実装を置く。

### パターン E: COM/WRL の代替

`<comdef.h>` `<wrl.h>` は shim 不可。COM を使う機能（マクロエンジン等）は
非 Windows では機能制限する／別実装に置き換える。

---

## 7. よくある落とし穴

### 7-1. StdAfx.h の Win32 SDK 多重 include

**症状**: sakura_core の任意の .cpp を非 Windows ビルドに含めると
`'comdef.h' file not found` 等の連鎖エラー。

**原因**: `sakura_core/StdAfx.h` が `<comdef.h>` `<CommCtrl.h>` `<imm.h>`
`<wrl.h>` 等を直接 include している。

**対処**: 詳細は [excluded_files.md](excluded_files.md) と
[issues.md](issues.md) I-004 を参照。Phase 6 以降で StdAfx.h に
`#ifdef _WIN32` ガードを追加する作業が予定されている。

### 7-2. Linux の Windows.h 大文字小文字

ext4 は case-sensitive。`#include <Windows.h>` と `#include <windows.h>`
の混在に対応するため、Linux ビルドでは `Windows.h` のシンボリックリンクを
`windows.h` として作成する CMake ステップが必要（[issues.md](issues.md)
I-005、Phase 2 で macOS 側のみ確認済み）。

### 7-3. wxWidgets 3.3 vs 3.2 の差異

CLAUDE.md は当初 3.2.x を想定していたが、Homebrew 提供版は 3.3.2 系。
両者で API は概ね互換だが、3.3 で追加された API（例: `wxStringView`）を
うっかり使うと Linux ビルドで失敗する可能性あり。
Phase 1 〜 3 で実装した範囲では問題なし。

---

## 8. テスト追加の方法

非 GUI ロジック（`sakura_wx/{config,ipc,charset}/`）は
`sakura_wx_core` 静的ライブラリにまとめられており、`tests/wx_tests/` から
リンク可能。GUI 依存 (wxFrame, wxStyledTextCtrl 等) のテストは現状未対応。

```cmake
# tests/wx_tests/CMakeLists.txt
add_executable(sakura_wx_tests
    test_platform_pal.cpp
    test_encoding.cpp
    test_sakura_config.cpp
    test_shared_data.cpp
    test_my_new_feature.cpp   # ← 追加
)
```

wxString 等を使うテストでは `wxInitializer` を 1 度初期化:

```cpp
#include <wx/init.h>
namespace {
struct WxInitGuard { wxInitializer g_init; };
WxInitGuard g_wx;
}
```

---

## 9. CI

`.github/workflows/cmake-cross-platform.yml` が Ubuntu 22.04 と
macOS 14 (Apple Silicon) で configure / build / ctest を実行する。

`docs/`, `*.md` のみの変更ではトリガーされない（`paths-ignore`）。

---

## 10. コントリビュートのやり方

1. CLAUDE.md と本ガイドを読む
2. [issues.md](issues.md) と [excluded_files.md](excluded_files.md)
   で既知の課題を確認
3. ブランチを作る: `git checkout -b porting/your-feature`
4. 変更を加える（**絶対に守るルール**は CLAUDE.md セクション「絶対に守るルール」を参照）
5. ローカルで `cmake --build build && ctest --test-dir build` を通す
6. PR を出す（CI で macOS/Ubuntu の自動ビルドが走る）

---

## 11. ロードマップ

| フェーズ | 状況 | 内容 | 工数目安 |
|---|---|---|---|
| Phase 0 | ✅ 完了 | 環境構築、Win32 依存解析、PAL 設計 | – |
| Phase 1 | ✅ 完了 | CMake 構築、PAL 実装、CI、テスト基盤 | – |
| Phase 2 | ✅ 完了 | sakura_wx GUI 骨格、wxStyledTextCtrl、検索 | – |
| Phase 3 | ✅ 完了 | SakuraConfig、SakuraSharedData（portable 版） | – |
| Phase 4 | ✅ 部分完了 | 文字コード round-trip テスト（実 IME 検証は Phase 6 で） | – |
| Phase 5 | ✅ 部分完了 | 検索置換ダイアログ、PORTING_GUIDE.md（Grep は Phase 6 へ） | – |
| **Phase 6** | 🔜 次期 | StdAfx.h 解体（`#ifdef _WIN32` ガード追加）、sakura_core 段階的取り込み、実 IME 対話確認、Grep ダイアログ、macOS .dmg / Linux AppImage CI | **2-3 週** |
| **Phase 7** | 📅 | **Tier 1 GUI 機能群**: ツールバー（オリジナル `my_icons.bmp` 流用）、タブ表示（wxAuiNotebook）、文字コード／改行コード手動指定 UI、ブックマーク UI、全角空白／タブ可視化、コード折りたたみ、最近使ったファイルメニュー、ファイルドロップ、設定ダイアログ（基本タブ） | **3-4 週** |
| **Phase 8** | 📅 | **Tier 2 GUI 機能群**: アウトライン（簡易版）、印刷／印刷プレビュー、矩形選択整備、設定ダイアログ（タイプ別シンタックス・キーワード・コメント・インデント）、メニュー項目の充実 | **3-4 週** |
| ~~Phase 9~~ | ❌ **断念** | マクロエンジン、自動補完、ファイル比較、タグジャンプ — ユーザー判断で scope out。詳細と代替案は [feature_gap.md](feature_gap.md) | – |

**合計工数: 約 2 ヶ月（専従換算）**

機能差分の詳細は [feature_gap.md](feature_gap.md) を参照。
進捗は [progress.md](progress.md) 参照。

### Phase 6 以降の戦略判断（確定済み）

| 判断項目 | 決定 |
|---|---|
| マクロ機能 | **断念**（COM/IDispatch 依存の上流マクロエンジンは移植非現実的。エディタ自動化は外部スクリプトで） |
| ファイル比較 | **断念**（meld / FileMerge / BeyondCompare 等の外部ツール連携を推奨） |
| タグジャンプ | **断念**（現代的には LSP 連携の方が有用、本移植では scope out） |
| 自動補完 | **断念**（LSP 別途起動の方針） |
| ツールバーアイコン | **オリジナル `src/main/resources/images/my_icons.bmp` を流用**（zlib license で再配布可） |
