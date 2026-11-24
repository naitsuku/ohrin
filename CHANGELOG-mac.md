# 変更履歴 — Ohrin（サクラエディタ非公式 macOS 移植版）

本家の変更履歴は [CHANGELOG.md](CHANGELOG.md) にあります。版番号は「本家の版-mac.通し番号」です（例: v2.4.3-mac.1）。

## v2.4.3-mac.3（2026-11-24）

- アプリの名前を「サクラエディタ for Mac」から **Ohrin（桜林）** に変更しました。リポジトリは https://github.com/naitsuku/ohrin です（旧 URL からも転送されます）
- アプリのファイル名が `Ohrin.app`、バンドル ID が `io.github.naitsuku.ohrin` になりました。macOS からは別のアプリとして扱われるため、次の点にご注意ください
  - 古い `SakuraEditor.app` は「アプリケーション」フォルダーから削除してください
  - 設定（`~/Library/Application Support/sakura/SakuraEditor.ini`）は、初回起動時に `Ohrin.ini` へコピーして引き継ぎます。元のファイルは残ります
  - 初回起動時の確認、「このアプリケーションで開く」の既定の設定、Dock への登録はやり直しが必要です
  - 書類フォルダーなどへのアクセスと、印刷時のローカルネットワークの許可を、もう一度求められます
- アプリのアイコンを Ohrin のもの（ノートに桜とりんご）に変更しました
- ウィンドウタイトルは「Ohrin」だけになりました（非公式の移植版であることはバージョン情報に表示します）

## v2.4.3-mac.2（2026-11-23）

- macOS 13・14 で起動できなかった問題を修正（v2.4.3-mac.1 は macOS 15 以降でしか動かなかった）。
  対応 OS の下限は macOS 13.3 になりました

## v2.4.3-mac.1（2026-11-22）

最初の公開版。本家 v2.4.3 を土台にしています。

- 本家の編集エンジン（sakura_core）を Win32 互換層（AppKit / CoreText）の上で動かす
- キー操作は Ctrl を Cmd に置き換え。日本語入力（IME）に対応
- 1 プロセスで複数ウィンドウ、タブ表示
- 共通設定・タイプ別設定、検索・置換・Grep、アウトライン解析、タグジャンプ
- 印刷と印刷プレビュー（macOS の印刷パネルを使う）
- キーボードマクロと JavaScript マクロ（JavaScriptCore）
- 開く・保存・フォント・色の選択は macOS 標準のパネル

未対応・制限は [README.md](README.md#未対応制限) と [docs/porting/issues.md](docs/porting/issues.md) を参照してください。
