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

---

## mac ネイティブ方針（2026-11-04〜）で発生した Issue

### I-101: std::filesystem::path が mac では narrow（UTF-8）

**症状**: 上流は `path::c_str()` / `native()` を `const wchar_t*` / `std::wstring` として使う（Windows 前提）。
**対処**: コンパイルエラー箇所は `.wstring()` に置換済み（Week 1）。
**残課題**: libc++ の path ⇔ wstring 変換は wchar_t を UTF-32 とみなすため、
UTF-16 単位を格納する本プロジェクトでは**非 BMP 文字を含むファイル名が化ける**。
winport に UTF-16 対応の変換ヘルパーを用意し、ファイル系 API の境界で使う。

**ステータス**: 一部対応（Week 2 で残課題）

### I-102: MSVC 書式の wide printf（`%s` がワイド文字列）

**症状**: sakura_core は wide 版 printf 系で `%s`（33 箇所）と `%ls`（68 箇所）を混用。
MSVC では wide 版の `%s` はワイド文字列だが、macOS libc では char* と解釈されクラッシュする。
**対処方針**: winport CRT に MSVC 書式解釈の実装を置き、`swprintf` / `vswprintf` /
`_swprintf` 等をマクロで差し替える。

**ステータス**: ✅ Week 2 で対応（printf.cpp の自前書式化エンジン。標準名の swprintf / vswprintf も
winport_crt.h のマクロで差し替え）

### I-103: `-fexec-charset=cp932` 前提の narrow 文字列リテラル

**症状**: 上流 MinGW ビルドは narrow リテラルを Shift_JIS で埋め込む。clang は UTF-8 のみ。
`CPropComFormat.cpp` の日付書式など、非 ASCII の narrow リテラルが少数ある。
**対処方針**: CP_ACP を UTF-8 にしたことで「narrow リテラル = ACP」の関係は保たれる（Week 2）。
SJIS を前提にバイト数を数えている箇所が残っていないかは引き続き確認する。

**ステータス**: 方針決定済み（個別確認は継続）

### I-104: リソース（sakura_rc.rc）の扱い

**症状**: 文字列テーブル・メニュー・アクセラレータ・ビットマップ・ダイアログテンプレートは
`LoadString` / `LoadMenu` / `FindResource` 等で実行時に読まれる。mac にはリソース機構がない。
**対処方針**: ビルド時に .rc を解析してデータ化するツールを `tools/porting/` に作り、
winport の Load* / FindResource がそれを引く。ダイアログテンプレートは mac 版ダイアログを
作るまでの暫定表示にのみ使う。

**ステータス**: Week 3〜4 で対応

### I-105: コモンコントロールはリンク表に出ない

**症状**: ListView / TreeView / UpDown / Toolbar / StatusBar / Tab 等はウィンドウクラス名と
`SendMessage(LVM_* 等)` で操作されるため、`winport_api_inventory.md` の件数に含まれない。
**対処方針**: エディタ本体が使うもの（ツールバー、ステータスバー、タブ、ファンクションキー）を
優先して winport のウィンドウクラスとして実装。ダイアログ内のものは mac 版ダイアログで置換。

**ステータス**: Week 3 以降

### I-106: 非 BMP 文字のワイド文字列リテラル

**症状**: mac の clang は `L"\U0001F6B9"` を 1 要素（UTF-32）で埋め込む。Windows はサロゲートペア 2 要素。
**影響**: 上流テストのうち約 38 件がこの差で失敗する。sakura_core 本体には該当リテラルはない（Week 2 確認）。
**対処方針**: 本体側で非 BMP リテラルを使わない（必要なら `\xD83D\xDEB9` のようにサロゲートで書く）。

**ステータス**: 記録のみ

### I-107: 自動スタブの戻り値

**症状**: 未実装 API の自動スタブが 0 を返すと、HRESULT 関数では S_OK（成功）になり、上流が
出力ポインタ（null）を使ってクラッシュした（ResolveShortcutLink の CoCreateInstance）。
**対処**: gen_winport_stubs.py が mingw ヘッダから HRESULT 関数を判別し E_NOTIMPL を返す。
BOOL / ハンドルを返す関数は 0 = 失敗なので問題ない。

**ステータス**: ✅ 対応済み

### I-108: ルーラーのキャレット位置表示が反転したまま残る

**症状**: ルーラー上のキャレット位置の反転表示が、移動後も消えずに広がる。
**推定原因**: R2_NOTXORPEN / PATINVERT 等の XOR 描画を CoreGraphics の差の合成（Difference）で近似している。
同じ位置に 2 回描いても元に戻らないケース（色・アンチエイリアス・座標の丸め）があると思われる。
**原因**: PaintRgn / Rectangle が ROP2 を無視して塗りつぶしていた。
**対処**: gdi.cpp の RopRect で画素を直接演算する。

**ステータス**: ✅ Week 4 で対応

### I-109: 上流の std::thread ワーカーと thread_local シングルトン

**症状**: レイアウト・読み込みの並列化で上流が作る std::thread には仮想プロセスの文脈が無い。
**対処**: TSingleInstance は thread_local が空なら最後に生成されたインスタンスを返す（Week 3）。
**残課題**: 複数のエディタ（仮想プロセス）が同時にワーカーを走らせると、別のエディタのインスタンスを
引く可能性がある。共有データ（DLLSHAREDATA）は全プロセス共通なので実害は小さいが、
CEditWnd 等を参照する箇所が無いか要確認。Week 5 で 2 枚同時の編集・終了は確認したが、
同時に重いファイルを読み込む場合は未確認。

**ステータス**: 解決（2026-11-07）。シングルトンを仮想プロセス単位の表へ移し、ワーカーは親のプロセスに入る

### I-110: SCROLLBAR コントロール

**症状**: sakura の CEditView は WS_VSCROLL ではなく `SCROLLBAR` クラスの子ウィンドウを作って
SBM_* メッセージで操作する。winport ではこのクラスが「何もしないウィンドウ」なので表示も操作もできない。
**対処**: controls.cpp に SCROLLBAR クラスを実装（NSScroller ではなく GDI で描く）。

**ステータス**: ✅ Week 4 で対応

### I-111: エディタ領域の右に幅 150 の空白が出ることがある

**症状**: 一部の起動でスプリッタの幅がクライアント幅より 150 狭い（850 / 1000）。
150 は既定のミニマップ幅で、OnSize の時点で `m_cMiniMapView.GetHwnd()` が非 null だった可能性がある。
ウィンドウ木のダンプにミニマップは出ていない。Week 4 の最後の確認では再現していないが、原因は未特定。

Week 6 に 1 度再現（検索ダイアログの初回テスト）。その後 5 回は OnSize の値が cx=1000 / 機能一覧 0 / ミニマップ null で再現せず。
設定は bDispMiniMap=0 なのでミニマップが有効になっているわけではない。

**ステータス**: 未解決（再現したら WINPORT_DUMP=1 で確認）

### I-112: Cmd に読み替えると mac の標準ショートカットと衝突するキー

**症状**: Ctrl→Cmd の読み替えで、Cmd+H（隠す）・Cmd+M（最小化）・Cmd+W（閉じる）などの mac 標準と
サクラの Ctrl+H / M / W が衝突する。Cmd+Tab などシステムが取るキーはアプリに届かない。
**現状**: キー操作の再現を優先し、Cmd+Q 以外はサクラへ渡す。Cmd+Q だけはアプリの終了に使うため、
サクラの Ctrl+Q（既定のキー割り当て）は Cmd では呼べない。
**ステータス**: 方針はユーザー確認待ち（使ってみて困るキーがあれば個別に mac 側へ振る）

### I-113: 未確定文字列が後ろの文字を隠す

**症状**: 未確定文字列はキャレット位置に重ねて描くので、行の途中で変換すると後ろの文字が隠れる
（Windows の over-the-spot に近い。確定後の内容には影響しない）。
**ステータス**: 保留（見た目のみ。必要なら後で行内に割り込ませる）

### I-114: 未実装のコントロールと設定画面

ListView、TreeView、トラックバー、ホットキー、PropertySheet、ChooseColor、IFileOpenDialog は実装済み。共通設定・タイプ別設定は開く。
残り: Ctrl+Tab でのページ切り替えは未実装。単一行のタブ（履歴の管理）は左右の送りボタンが無く、選択で送るだけ。
一部のラベルは Hiragino の幅で右が切れる（例: マクロページのフォルダー欄の見出し）。
**ステータス**: 大部分解決

### I-115: EDIT コントロールの簡略化

**症状**: 複数行の EDIT は折り返さない（長い行はバージョン情報などで右が切れる）。Undo は 1 段階のみ。
バージョン情報は静的テキストの背景が白く残り、アイコンが出ない（SS_ICON 未実装）。
**ステータス**: 保留（見た目のみ。編集挙動はサクラ本体に影響しない）

### I-116: 開く／保存パネルの制限

**症状**: 補助ビューはパネルを開く時点の状態を写すだけなので、文字コードを変えても BOM の有効/無効は追従しない。
CP（コードページの追加）は出していない。「最近のファイル／フォルダー」はパネルの場所を移すだけ。
ファイルの種類のフィルターは無く全ファイルを表示する。
**ステータス**: 保留

### I-117: フォント選択の簡略化

**症状**: NSAlert の簡易画面で、プレビューが無い。CF_FIXEDPITCHONLY（等幅のみ）の絞り込みをしていない。
**ステータス**: 保留

### メモ: osascript の keystroke と日本語入力

日本語入力ソースが有効なとき、`osascript -e 'tell application "System Events" to keystroke " "'` の
スペースはアプリに届かない（入力システム側で消費される）。自動テストでは `key code 49` を使う。

### I-118: ユーザー別設定フォルダー

解決（2026-11-07）。SHGetKnownFolderPath と GetPrivateProfileStringW を実装し、常にこの経路を使う。

### I-119: `\\` だけを区切りと見るコードが残っている

`GetFileTitlePointer` と `CDocTypeManager` は直したが、`util/file.cpp` の長いパス処理（870 行以降）、`CBackupAgent`、
`GetExedir` 周りなど `L'\\'` を直接見るコードが多い。ファイルを開く基本の流れでは問題が出ていない。動作確認しながら順に見る。

### I-120: JS マクロの制限

`ActiveXObject` は無い（`Scripting.FileSystemObject` を使うマクロは動かない）。JScript の `@cc_on` も無い。
`WScript` はもともとサクラの環境には無い。VBScript / PPA は対象外なので `.vbs` `.ppa` は読み込みエラーになる。

### I-121: 警告が別のディスプレイに出ることがある

アプリが非アクティブのとき、NSAlert が所有ウィンドウと別の画面（x=2950）に出た。内容は正しい。
ウィンドウを持つ画面の中央に出すよう backend::ShowMessageBox を直す余地がある。
I-111 の手がかり: 警告の表示中はエディタ右に白い帯が出た。起動直後の WM_SIZE の処理前に描画している可能性がある。

### I-122: 配布に残っている作業

- 公証していない（アドホック署名のみ）。他の Mac では Gatekeeper の確認が出る。Developer ID と notarytool が必要
- arm64 のみ。Intel 向けには universal ビルドが必要
- アイコンは上流の 32x32 を拡大したもので粗い
- 開発中に .app 内の SakuraEditor.ini へ保存していた設定は引き継がない
- CMigemo（ローマ字検索）は未対応。LoadLibrary が失敗するので、機能が無効になるだけ

### I-123: 印刷の制限

**症状**: 実際の紙への出力は未確認。ページ設定のフォント欄は、mac に無い「ＭＳ 明朝」のとき空に見える（空のまま OK しても等幅の代替で印刷できる）。
プリンターの余白は考慮せず用紙全体を描画範囲にしている。印刷パネルで用紙や向きを変えても、ページ設定には戻らない。
**ステータス**: 保留

### I-124: シングルトンの破棄順と記憶域

**症状**: 上流はデストラクタの中で、先に破棄された関数内 static のシングルトンを触る（~CEditDoc → CDiffManager）。
Windows では記憶域が残るので問題にならないが、互換層が delete していたので終了時にメモリを壊し、Cmd+Q がときどき落ちた。
**対処**: ProcessSingleton は finalize（デストラクタ）を作成の逆順に呼び、記憶域は最後にまとめて解放する。作成前に 0 で埋める。
破棄済みの実体を触る上流の処理そのものは残っている（Windows と同じ未定義動作）。
**ステータス**: 解決（ASan で確認。`cmake -B build-asan -DCMAKE_CXX_FLAGS=-fsanitize=address ...`）

### I-125: 上流の小さな不具合（mac でだけ ifdef で直したもの）

- CMenuDrawer: `tbd[i]` で配列の外を読む → `funcCode` を使う
- CSortedTagJumpList::AddParamA: 備考などが空だと `_wcsdup(nullptr)` → 空文字列にする
- 直していないもの: CDlgSameColor::OnSelChangeListColors（文字色）が `g_ColorAttributeArr[i]` でフラグを見るので、
  カーソル行の背景色など文字色の無い番号の色を選ぶと項目名が出ない
**ステータス**: 上流へ報告する候補

### I-126: 上流の「プロセスに 1 つ」と仮想プロセス

**症状**: v2.4.3 の `TSakuraSingleton`（static の unique_ptr）は、互換層の 1 プロセス複数スレッドでは全エディターで共有される。
あるエディターの終了時の resetInstance が他のエディターの CEditApp を壊し、メニューが空になった。
**対処**: `#ifdef SAKURA_WINPORT` で仮想プロセスの表に持つ（TSingleton と同じ）。生成前に 0 で埋めるのも TSingleton と同じ。
**今後**: 上流が新しく「プロセスに 1 つ」の static を足すと同じことが起きる。取り込みのたびに確認する（upstream_sync.md）。
**ステータス**: 解決

