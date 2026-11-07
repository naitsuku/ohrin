# macOS ネイティブ移植計画（2026-11-04 方針転換）

## なぜ転換したか

wxWidgets 版（Phase 0〜15b）は、sakura_core のうち PAL と `basis/` の 9 ファイルしか
ビルドしておらず、エディタ本体は wxStyledTextCtrl（Scintilla）による新規実装だった。
サクラ固有の挙動（CLayoutMgr の折り返し・禁則、CViewCommander の全コマンド、
色分け戦略、フリーカーソル、矩形選択、スマートインデント等）はすべて sakura_core にあり、
作り直しでは再現性の上限が低い。

新方針では sakura_core をフォークし、**本物の編集エンジンを mac で動かす**。

## 構成

- `winport/`: エディタ本体が使う Win32 API（概算 280 種、sakura_core 全体で約 400 種）を
  AppKit / CoreText 上に実装する互換層
- ダイアログ（61 個）は互換層で動かす。当初は mac ネイティブで作り直す計画だったが、Week 6 の調査で
  ほぼ 6 種類のコントロールで構成されていると分かり、ユーザー確認のうえ変更した。上流の CDlg*（約 3.2 万行）を
  そのまま使えるので挙動差が出ない。コントロールは Win32 のウィンドウプロシージャとして実装する
  （上流がサブクラス化するため、NSControl には置き換えない）。開く／保存／フォントは mac 標準パネル
- プロセス: 1 プロセス複数ウィンドウを第一候補（`TSingleInstance<CEditWnd>` を
  互換層のメッセージ配送時にコンテキスト切替で扱う案）。不成立なら 1 プロセス 1 ウィンドウ
- マクロ: キーマクロ（本体そのまま）+ IActiveScript を JavaScriptCore で実装した JScript 互換エンジン。
  上流の CWSHClient からは Windows の JScript と同じに見える（`winport/src/jscript.cpp`）

## 仮想プロセス（1 スレッド = 1 sakura プロセス）— Week 3 で決定

上流はコントロールプロセス（`-NOWIN`、常駐）とエディタプロセスに分かれ、自分自身を
`CreateProcess` で起動し、プロセス間でウィンドウメッセージ・名前付きミューテックス／イベント・
共有メモリをやり取りする。winport ではこれを 1 つの mac プロセスの中で再現する。

- 各 sakura プロセスは**スレッド**として動かす（`winport/src/process.cpp`）。自分自身を起動する
  `CreateProcessW` は新しいスレッドで `wWinMain` を呼ぶ
- プロセス単位のシングルトン（`TSingleton` / `TSingleInstance` / `TInstanceHolder`）は
  `thread_local` にする（`sakura_core/util/design_template.h`）。上流が `std::thread` で作る
  ワーカースレッドには文脈が無いので、`TSingleInstance` は最後に作られたインスタンスを返す
- Win32 のメッセージキューはスレッドごと。別スレッドのウィンドウへの `SendMessage` は相手スレッドで
  処理されるまで待つ（Win32 と同じ）。名前付きオブジェクトと共有メモリはプロセス内で共有する
- **macOS のメインスレッドは Cocoa のイベントループ専用**（表示サーバ役）。Win32 のコードは動かさない。
  入力は所有スレッドのキューへ投函し、描画は各ウィンドウの描画面（CGBitmapContext）に行って
  メインスレッドが画面へ転送する（Wine の macdrv と同じ発想）
- Cocoa と windows.h は同じ翻訳単位に入れられないため、表示系（`cocoa_backend.mm`）は
  Windows 型を使わない `backend.h` だけを見る。Win32 側との受け渡しは `bridge.cpp`

結果として、1 プロセス・複数ウィンドウ（mac 流）と、上流のプロセス構成・タブのグループ化の
仕組みを両立できる。

## wchar_t の扱い

macOS の wchar_t は 4 バイト。sakura_core は WCHAR を UTF-16 単位として扱う。

- `-fshort-wchar` 単独は**不可**（実験済み）: システム libc++.dylib の
  `basic_string<wchar_t>` 実体が 4 バイト前提で、append / compare 等が壊れる。
  `_LIBCPP_DISABLE_EXTERN_TEMPLATE` でも dylib 側シンボルに解決されて改善しない。
- UTF-32 案（1 要素 = 1 コードポイント）は不採用: サロゲート判定
  `IsUtf16SurrogHi` が `static_cast<unsigned short>(wc)` で下位 16 ビットだけを見るため
  U+2D834 などを誤判定する。列数・インデックスも Windows とずれる。
- 採用案: **wchar_t 4 バイトのまま、各要素に UTF-16 コード単位を格納**する
  （サロゲートペアは 2 要素）。sakura_core のサロゲート処理・列計算・インデックスが
  Windows と同一になり、NSString（unichar = 16 bit）との変換も単純なコピーで済む。
- 修正が要るのは「CNativeW のメモリを UTF-16LE のバイト列として直接扱う」境界だけ:
  `charset/CUnicode.cpp`（UTF-16LE/BE の入出力。現状 `SetRawDataHoldBuffer` で
  バイト列をそのまま CNativeW に入れている）、`_os/CClipboard.cpp`、`_os/CDropTarget.cpp`、
  `charset/` の各コーデックの一部（reinterpret_cast 箇所、計 15 ファイル程度）。
  `sizeof(wchar_t)` を使った長さ計算はそのまま 4 バイトで正しく動く。

## 8 週間計画（MVP）

| 週 | 内容 | ゲート |
|---|---|---|
| 1 | wchar_t 方式決定。sakura_core 全体のコンパイル・リンク試行、必要 API 確定 | 判断 1: 非 GUI 層が mac でビルドでき、上流テストの一部が通る見込み |
| 2 | kernel32 系（ファイル、文字コード変換、ini、共有メモリ、時刻）。文書／レイアウト系を取り込み上流 GoogleTest を通す | 1 プロセス複数ウィンドウの成立可否 |
| 3–4 | user32 / gdi32 の中核（HWND⇔NSView、メッセージ、WM_PAINT、ExtTextOut→CoreText、キャレット、スクロール、マウス、タイマー） | 判断 2: 本物の CEditView で日本語ファイルを開いて編集できる |
| 5 | IME（NSTextInputClient → WM_IME_* / Imm*）、Cmd→Ctrl 変換、MainMenu.ini → mac メニューバー | |
| 6 | Win32 ダイアログ機構とコントロール（検索・置換・Grep・ジャンプ）、開く/保存/フォントの mac パネル、クリップボード、ドロップ | |
| 7 | キーマクロ、JS マクロ（JavaScriptCore） | |
| 8 | タブ・複数ウィンドウ、.app / DMG、Windows 版との挙動比較、安定化 | |

MVP 後（+2〜4 ヶ月）: 印刷、残りのダイアログ、プラグイン、
Python マクロ、見た目合わせ。対象外: PPA マクロ、VBScript マクロ。

## 進捗

- Week 1（2026-11-04）: 判断 1 通過。sakura_core 全 375 ファイルが winport ヘッダで
  コンパイルでき、リンク未解決は Win32 / CRT の 528 シンボルのみ。
  詳細は progress.md と `winport_api_inventory.md`。上流テスト実行は Week 2 冒頭に繰り越し。
- Week 2（2026-11-04）: CMake 化、CRT・printf・コードページ・kernel32・リソースを実装。
  上流 GoogleTest 757 件中 707 件が mac でパス。残りは非 BMP リテラル・GDI 未実装・ACP 差で原因判明。
  ACP は UTF-8 に決定。
- Week 3（2026-11-04）: **判断 2 通過**。user32（ウィンドウ・メッセージ・タイマー・キャレット・
  スクロールバー状態・メニューのデータ・アクセラレータ・CBT フック）、gdi32（DC・描画・CoreText の文字描画）、
  Cocoa 表示系、仮想プロセスを実装。上流の CEditWnd / CEditView が mac のウィンドウで
  日本語ファイルを表示し、キー入力・マウスでの移動・保存（Cmd+S）ができる。
- Week 4（2026-11-04）: メッセージボックス、SCROLLBAR / ステータスバー / ツールバー、本物の ROP、
  ビットマップとイメージリストを実装。選択範囲・スクロール・ツールバーのアイコンが動く。
- Week 5（2026-11-04）: IME（NSTextInputClient → WM_IME_COMPOSITION）、mac メニューバー
  （HMENU から生成、MainMenu の内容と有効/無効はサクラ本体の判定のまま）、コンテキストメニュー、
  Cmd+Q の保存確認を実装。ウィンドウクラスの所有をプロセス単位にして 2 枚目のエディタが開くようになった。
- Week 6（2026-11-04）: Win32 のダイアログ機構とコントロールを実装し、上流の検索・置換・Grep・ジャンプが
  そのまま動く。開く／保存は mac のパネルで、文字コード・改行・BOM の指定は上流のフックのまま。
  クリップボード（NSPasteboard）、ファイルのドロップ、フォント選択も実装。
- Week 7（2026-11-07）: oleaut32 と JavaScriptCore の JScript エンジンを実装。上流のマクロコードは変更なしで、
  キーマクロの記録・実行と JS マクロが動く。エラーの行番号表示と、無限ループの中止ダイアログも上流のまま動作する。
- Week 8（2026-11-07）: 計画に無かった正規表現を、上流と同じ bregonig + Onigmo で実装。タブ、設定の保存先、Finder 連携、
  DMG を実装。大きなファイルで落ちる問題を、シングルトンの仮想プロセス単位化で解決した。MVP の範囲を完了。

