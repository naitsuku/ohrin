# Phase 2 動作確認結果

実施日: 2026-05-04
プラットフォーム: macOS 14 Apple Silicon (Darwin 25.3.0), wxWidgets 3.3.2

## ビルド結果

```
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
-- wxWidgets found: 3.3.2
-- Configuring done
$ cmake --build build --parallel
[1/6] Building CXX object sakura_wx/.../app/SakuraApp.cpp.o
[2/6] Building CXX object sakura_wx/.../dialog/CDlgSearchWx.cpp.o
[3/6] Building CXX object sakura_wx/.../window/CEditWndWx.cpp.o
[4/6] Building CXX object sakura_wx/.../view/CEditViewWx.cpp.o
[5/6] Linking CXX executable bin/sakura_wx_tests
[6/6] Linking CXX executable bin/sakura_wx.app/Contents/MacOS/sakura_wx
```

警告 0、エラー 0 でビルド完了。macOS では .app バンドルとして生成される。

## 自動テスト

```
$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 9
Total Test time (real) =   0.19 sec
```

PAL のテスト 9 件は引き続き全パス。

## GUI スモークテスト

各起動シナリオで「3 秒経過しても異常終了せず GUI イベントループ内」を確認。
本セッションは headless 環境ではないが GUI 描画の自動キャプチャは行わず、
プロセス生存と stdout/stderr のクリーン性のみを判定基準とした。

| シナリオ | 入力 | 結果 |
|---|---|---|
| 引数なし起動 | （なし） | ✅ 起動、無題タイトル |
| UTF-8 ファイル | `utf8.txt`（日本語含む） | ✅ 起動、自動 UTF-8 判定 |
| Shift-JIS ファイル | `sjis.txt`（CP932 で書き出し） | ✅ 起動、自動 SJIS 判定 |
| C++ ファイル | `sample.cpp` | ✅ 起動、シンタックスハイライト適用想定 |

stdout/stderr に警告・エラーメッセージなし。

## 手動確認すべき項目（残）

GUI 自動テストでカバーできない以下は、実際に画面を見て検証する必要がある：

- [ ] ウィンドウが正常に開き、メニューバー・ステータスバーが表示される
- [ ] 日本語テキストファイル（UTF-8、Shift-JIS）が文字化けなく表示される
- [ ] C++ファイルでシンタックスハイライトが正しい色付けになる
- [ ] Ctrl+S で保存できる（ファイルが正しい文字コードで書き出される）
- [ ] Ctrl+Z/Y で Undo/Redo できる
- [ ] Ctrl+F で検索ダイアログが開く
- [ ] Ctrl+F の「次を検索」「前を検索」「すべてハイライト」が動作する
- [ ] ステータスバーに「行 N 列 M」「文字コード名」が表示される
- [ ] 行番号表示の ON/OFF が機能する（表示 → 行番号を表示）
- [ ] フォント設定が機能する（表示 → フォント設定...）
- [ ] 未保存変更時の終了確認ダイアログが出る

## 実装済み機能サマリ

### `sakura_wx/app/SakuraApp.{h,cpp}`
- wxApp サブクラス、`wxIMPLEMENT_APP`
- 日本語ロケール（取得失敗時はフォールバック）
- `OnInitCmdLine` / `OnCmdLineParsed` をオーバーライドし、ファイルパスの
  positional 引数を受け付ける（wxWidgets default parser は positional を
  受け取らないため独自パース）
- 起動時ファイルがあれば CEditWndWx に渡す

### `sakura_wx/window/CEditWndWx.{h,cpp}`
- wxFrame サブクラス、メインウィンドウ
- 4 メニュー（ファイル / 編集 / 検索 / 表示 / ヘルプ）
- ステータスバー 3 フィールド（ファイルパス / カーソル位置 / エンコーディング）
- ファイル新規・開く・保存・名前を付けて保存
- Undo/Redo/Cut/Copy/Paste/SelectAll → CEditViewWx に委譲
- Ctrl+F で CDlgSearchWx を開く（モードレス）
- 行番号表示／折り返し／フォント設定の切替
- 終了時の未保存確認ダイアログ
- ウィンドウタイトルに `* ファイル名 - サクラエディタ (wxWidgets)` を表示
  （`*` は未保存マーカー）

### `sakura_wx/view/CEditViewWx.{h,cpp}`
- wxStyledTextCtrl サブクラス、Scintilla ベース
- C++/Python/Markdown のシンタックスハイライト（簡易カラースキーム）
- 行番号マージン、現在行ハイライト、自動インデント
- 日本語フォント自動選択（macOS: Menlo/Osaka-Mono、Linux: Noto Sans Mono CJK JP）
- 文字コード自動判定（UTF-8 BOM / UTF-8 / Shift-JIS / EUC-JP / UTF-16 LE/BE）
  - UTF-8 妥当性チェック → 失敗時に SJIS/EUC-JP のヒューリスティック判定
  - BOM 自動検出
- 読み込み時に元のエンコーディングを保持し、保存時に同じエンコーディングで
  書き出し（BOM 維持）
- Save Point イベントで親ウィンドウに変更状態を通知し、タイトルバーの
  `*` マーカーを連動

### `sakura_wx/dialog/CDlgSearchWx.{h,cpp}`
- wxFrame モードレス検索ダイアログ
- 大小区別 / 単語単位 / 正規表現 オプション
- 「次を検索」「前を検索」「すべてハイライト」ボタン
- 折り返し再検索（末尾→先頭、先頭→末尾）
- ハイライトは Scintilla のインジケータ #0（ROUNDBOX、黄色）

## 既知の未対応 (Phase 4 以降で対応)

- IME 入力の細かい挙動（候補ウィンドウ位置、確定タイミング等）
- 置換ダイアログ
- 複数ファイルのタブ表示
- アウトライン表示
- マクロ
- 印刷
- ファイルドロップ受付
- 設定ダイアログ全般

## sakura_core との接続

Phase 2 時点では sakura_wx は **sakura_core の PAL のみ** にリンクしており、
sakura_core 本体（CDocFile, CMemory, charset/* 等）は未接続。
Phase 3 で sakura_core の portable 化と sakura_wx からの利用を進める。
