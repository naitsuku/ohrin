# サクラエディタ for Mac（非公式）

Windows 用テキストエディタ [サクラエディタ](https://github.com/sakura-editor/sakura) を macOS で動かす、**非公式**の移植版です。

> [!IMPORTANT]
> これはサクラエディタの公式版ではありません。Sakura Editor Organization とは関係がなく、承認も受けていません。
> **Mac 版の不具合や要望は、このリポジトリの [Issues](https://github.com/naitsuku/sakura-editor-mac/issues) へお寄せください。**
> サクラエディタ本家へは報告しないでください。

*English: An unofficial macOS port of [Sakura Editor](https://github.com/sakura-editor/sakura), a Japanese text editor for Windows.
It is not affiliated with or endorsed by the Sakura Editor Organization. Please report issues of the Mac version here, not upstream.*

## 特徴

- **本家の編集エンジンをそのまま動かします。** `sakura_core/` は本家のフォークで、Win32 API を macOS の AppKit / CoreText で
  実装した互換層（`winport/`）の上で動きます。折り返し、禁則、矩形選択、検索・置換、Grep、アウトライン解析、
  タグジャンプなどは本家と同じコードです
- キー操作は Windows 版の **Ctrl を Cmd に置き換えた**ものです
- 設定画面（共通設定・タイプ別設定）、印刷と印刷プレビュー、キーボードマクロと JavaScript マクロ（JavaScriptCore）に対応します
- 開く・保存・フォント選択・印刷パネルは macOS 標準のものを使います

## 動作環境

- macOS 13 以降、Apple Silicon（arm64）
- 開発と確認は macOS 26 で行っています。それより前の macOS では確認していません

## インストール

1. [Releases](https://github.com/naitsuku/sakura-editor-mac/releases) から `SakuraEditor-<版>.dmg` をダウンロードします
2. DMG を開き、`SakuraEditor.app` を「アプリケーション」フォルダーへコピーします
3. 初回起動時に「開発元を確認できない」旨の警告が出ます（Apple の公証を受けていないため）。
   「システム設定」→「プライバシーとセキュリティ」の下にある「このまま開く」を押してください

設定は `~/Library/Application Support/sakura/SakuraEditor.ini` に保存されます。

## 未対応・制限

- PPA マクロ、VBScript マクロ、WSH は使えません（JavaScript マクロは JavaScriptCore で動きます）
- Migemo（ローマ字検索）、英語・中国語の UI、HTML ヘルプ（.chm）は未対応です
- 外部コマンドの実行（ctags によるタグファイル作成、DIFF など）は未対応です
- 初めて印刷パネルを開くとき、ネットワークプリンターを探すために macOS がローカルネットワークの許可を求めます
- 既知の問題は [docs/porting/issues.md](docs/porting/issues.md) にまとめています

## ソースからのビルド

必要なもの: Xcode Command Line Tools、CMake 3.20 以降、Ninja、Python 3、Homebrew の `mingw-w64`
（Win32 のヘッダーだけを使います。Windows 向けのビルドはしません）

```bash
brew install cmake ninja mingw-w64
git submodule update --init externals/Onigmo externals/bregonig externals/cmigemo
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
open build/bin/SakuraEditor.app
```

配布用の DMG は `tools/porting/make_dmg.sh` で作ります（Release ビルドの後、`build-release/` に出力）。

## 構成

```
sakura_core/   本家のフォーク。Mac 用の変更は #ifdef SAKURA_WINPORT で囲んでいます
winport/       Win32 互換層（kernel32 / user32 / gdi32 / comctl32 ... を AppKit・CoreText で実装）
sakura_mac/    アプリの入口と .app バンドル
tests/mac_tests/  互換層のテストと、本家の単体テストを Mac で走らせる設定
docs/porting/  移植の計画・進捗・既知の問題・本家の取り込み手順
```

本家の新しい版は [docs/porting/upstream_sync.md](docs/porting/upstream_sync.md) の手順で取り込みます。
現在の土台は本家の **v2.4.3** です。

## ライセンス

[zlib License](LICENSE) です。サクラエディタ本体の著作権は Sakura Editor Organization にあり、
macOS 移植部分の著作権は naitsuku にあります。アプリに組み込んでいる第三者のソフトウェア（Onigmo、bregonig）の
ライセンスは [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) にあります。

## 謝辞

サクラエディタを長年開発・公開してきた Sakura Editor Organization と、すべてのコントリビューターに感謝します。
本家のリポジトリは <https://github.com/sakura-editor/sakura> です。

この移植は開発者 1 名が AI（Anthropic の Claude）の支援を受けて作りました。
