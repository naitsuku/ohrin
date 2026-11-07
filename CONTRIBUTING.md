# 貢献の手引き

サクラエディタ for Mac（非公式）への Issue・Pull Request を歓迎します。

## 報告先

- **Mac 版の不具合・要望**: このリポジトリの Issues へ
- **Windows 版でも起きる不具合**: 本家 [sakura-editor/sakura](https://github.com/sakura-editor/sakura) が直す場所です。
  Mac 版で見つけた場合も、本家で再現するなら本家へ報告してください

## 方針

- 再現性の優先順位は **編集挙動 > キー操作 > 設定の互換（ini） > 見た目** です
- キー操作は Windows 版の Ctrl を Cmd に置き換えます
- `sakura_core/` は本家のフォークです。本家を取り込み続けられるよう、変更は最小にして `#ifdef SAKURA_WINPORT` で囲みます。
  互換層（`winport/`）で吸収できるものはそちらで吸収します
- `src/` `sakura_lang/` `externals/` `help/` `installer/` など本家の資産は変更しません
- コミットは小さく、1 つの作業ごとに分けます

## ビルドとテスト

[README.md](README.md#ソースからのビルド) を参照してください。メモリの問題を調べるときは AddressSanitizer のビルドが役に立ちます
（[docs/porting/upstream_sync.md](docs/porting/upstream_sync.md)）。

## 資料

- [docs/porting/mac_native_plan.md](docs/porting/mac_native_plan.md) — 設計と方針
- [docs/porting/issues.md](docs/porting/issues.md) — 既知の問題
- [docs/porting/upstream_sync.md](docs/porting/upstream_sync.md) — 本家の取り込み手順

## ライセンス

送っていただいた変更は、このリポジトリと同じ [zlib License](LICENSE) で配布されます。
