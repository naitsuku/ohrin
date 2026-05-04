# コードベース概要

生成日: 2026-05-04
対象: sakura-editor/sakura master (commit 71514c3) を sakuramac/ にマージ後の状態

## ディレクトリ構成（2階層、externals 除く）

```
sakuramac/
├── docs/porting/          # 本プロジェクトの移植ドキュメント（新規）
├── externals/             # サードパーティ依存（Onigmo, ctags, googletest など）
├── help/                  # ヘルプドキュメントとマクロ
├── installer/             # Windows インストーラ
├── sakura_core/           # 共有コアライブラリ（移植の主対象）
│   ├── _main, _os, agent, apiwrap, basis, charset, cmd, config,
│   ├── convert, debug, dlg, doc, docplus, env, extmodule, func,
│   ├── grep, io, macro, mem, outline, parse, plugin, print, prop,
│   ├── recent, typeprop, types, uiparts, util, view, window
├── sakura_lang/           # ローカライズ DLL（en_US, zh_CN）
├── src/                   # Win32 エントリポイント＋テスト
│   ├── main/              # sakura.exe 本体（cmake/cpp/msbuild/resources）
│   └── test/cpp/tests1/   # GoogleTest 既存ユニットテスト
├── tools/                 # ビルドツール、Doxygen、SonarQube 設定など
├── CLAUDE.md              # プロジェクト方針（本移植プロジェクト固有）
├── CMakeLists.txt         # 既存の Windows 向け CMake（CMake 3.16+）
├── sakura.sln             # Visual Studio ソリューション
└── phase{0,1,2,3_5}_prompt.md  # 段階別作業プロンプト
```

> **注意**: CLAUDE.md の「移植後ディレクトリ構成」に書かれた `sakura/` ディレクトリは
> 現 upstream には存在しない。Win32 エントリは `src/main/` に格納されている。
> CLAUDE.md の rule 1「`sakura/` を変更禁止」は実態としては
> 「`src/`（main/ test/ 以下すべて）と `sakura.sln` を変更禁止」と読み替える必要がある。

## コード規模

### 全体（externals 除く）
- `.cpp` ファイル数: **441**
- `.h` ファイル数: **328**
- `.cc/.cxx/.hpp` ファイル数: **18**

### sakura_core/（移植主対象）
- `.cpp`: **374**
- `.h`: **322**
- 総コード行数（コメント含む）: **186,436**

### src/（Win32 エントリ＋既存テスト）
- `.cpp`: **63**（うち `src/test` が 62、`src/main` は 1）
- `.h`: **4**
- 総コード行数: **21,964**

## sakura_core/ サブディレクトリ別サイズ

| ディレクトリ | ファイル数 | 行数 | 役割の推定 |
|---|---:|---:|---|
| `view/`     | 73 | 22,379 | エディタビュー描画（GDI 依存大） |
| `dlg/`      | 52 | 16,131 | 各種ダイアログ |
| `cmd/`      | 30 | 14,297 | コマンド実装 |
| `env/`      | 48 | 12,928 | 環境設定・パス管理 |
| `window/`   | 18 | 12,281 | メインウィンドウ・タブ |
| `prop/`     | 20 | 10,427 | プロパティシート |
| `charset/`  | 38 |  8,876 | 文字コード変換 |
| `macro/`    | 28 |  8,857 | マクロエンジン |
| `typeprop/` | 18 |  8,478 | タイプ別設定 |
| `types/`    | 24 |  8,590 | 構文ハイライト |
| `doc/`      | 40 |  8,085 | ドキュメント本体 |
| `util/`     | 35 |  7,353 | ユーティリティ |
| `outline/`  |  8 |  5,726 | アウトライン解析 |
| `agent/`    | 14 |  5,058 | エージェント機構 |
| `_main/`    | 18 |  4,537 | プロセス起動・終了 |
| `func/`     |  8 |  3,374 | 関数管理 |
| `print/`    |  4 |  3,378 | 印刷 |
| `uiparts/`  | 13 |  3,061 | UI部品 |
| `recent/`   | 37 |  3,018 | 最近使ったファイル |
| その他      | – | 約14,000 | basis, convert, plugin, etc. |
| **合計**    | **374+322** | **186,436** | |

移植難易度の感覚（後で Task 0-2 で定量評価）：
- 大きさと推定 Win32 依存度から、`view/`, `window/`, `dlg/`, `_os/`, `prop/`,
  `extmodule/` あたりが優先課題と予想される。

## ビルドシステム

### 既存（Windows）
- **`sakura.sln`**: Visual Studio 2019/2022 用ソリューション
- **`*.vcxproj`**:
  - `sakura_core/sakura.vcxproj` — メインビルド
  - `sakura_core/tests1.vcxproj` — テスト
  - `sakura_lang/sakura_lang_en_US.vcxproj`, `..._zh_CN.vcxproj` — 言語DLL
- **`CMakeLists.txt`**（ルート、CMake 3.16+）: 既存。Windows 用 CMake ビルドも提供している
  - `add_executable(sakura WIN32 ...)` — Win32 リンクを前提
  - `src/main/cmake/sakura.cmake` 経由で sub-target を構成
  - `bregonig.cmake` で正規表現 DLL を取得

### バッチスクリプト
ルート直下に `build-all.bat`, `build-sln.bat`, `build-installer.bat`, `build-chm.bat`,
`build-bmp-tools.bat`, `build-gnu.bat`, `checkEncoding.bat` などが多数。Windows 専用。

### ⚠️ Phase 1 への申し送り
**ルート CMakeLists.txt は既に存在する（Win32 前提）**。Phase 1 prompt は
「ルートに新規作成」と書かれているが、上書きすると Windows ビルドを壊すおそれ。
方針候補：
- (a) 既存 CMakeLists.txt の冒頭で `if(WIN32)` 分岐を追加し、非 Windows のときだけ
  wxWidgets ターゲットを構成する（Win32 既存ロジックを保護）
- (b) 別名（例: `CMakeLists_wx.txt`）で並走させ、`-C` で切り替え
- (a) の方が CMake idiom として素直で推奨。

## 既存テスト

- 場所: `src/test/cpp/tests1/`（62 個の `.cpp`）
- フレームワーク: GoogleTest（`externals/googletest` を submodule で取得）
- vcxproj: `sakura_core/tests1.vcxproj`
- CMake: `src/test/cmake/` 配下に存在（要確認）

これらの既存テストが非 Windows 環境で何個 pass するかは Phase 1 で測定する。

## externals（サードパーティ）

`externals/` 配下に 11 件の依存。submodule 参照と思われる：
- `Onigmo` — 正規表現エンジン
- `bregonig` — 正規表現 DLL
- `cmigemo`, `cmigemo-dict` — Migemo 検索
- `ctags` — タグ生成
- `darkmodelib` — Windows ダークモード
- `diffutils` — 差分計算
- `googletest` — テストフレームワーク
- `LocaleEmulator`, `miniz-cpp` — その他

非 Windows ビルド時にどれをリンクするかは Phase 1 で決定。
`darkmodelib` と `LocaleEmulator` は Windows 専用なので除外候補。

## サブモジュールの状態

`.gitmodules` あり。シャロークローンした影響で submodule 自体はまだ取得していない。
externals 配下の実コードが必要になった時点で `git submodule update --init --recursive`
を実行する。
