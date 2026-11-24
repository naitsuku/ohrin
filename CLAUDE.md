# CLAUDE.md — サクラエディタ macOS ネイティブ移植プロジェクト

## プロジェクト概要

サクラエディタ（Windows 専用テキストエディタ）を macOS ネイティブアプリとして移植する。
zlib ライセンスのオープンソース。開発者 1 名 + Claude。

- **元リポジトリ**: https://github.com/sakura-editor/sakura（`upstream` リモート登録済み）
- **方針（2026-11-04 転換）**: `sakura_core/` を**フォーク**し、upstream の編集エンジンを
  そのまま動かす。下に **Win32 互換層 `winport/`**（AppKit / CoreText 実装）を敷く。
  ダイアログも互換層で動かす（2026-11-04 Week 6 で変更。Win32 のダイアログ機構とコントロールを
  winport に実装し、上流の CDlg* をそのまま使う）。開く／保存／フォントは mac 標準パネルを使う。
- **目標プラットフォーム**: macOS 13+（Apple Silicon 優先）。Linux は当面対象外。
- 旧方針の wxWidgets 版 `sakura_wx/` は 2026-11-08 に削除した（履歴には残っている）。
- **公開名は「Ohrin（桜林）」**、リポジトリは github.com/naitsuku/ohrin、
  バンドル ID は `io.github.naitsuku.ohrin`、アプリは `Ohrin.app`。独自名 Ohrin に変更済み（2026-11）。非公式の移植版であることは引き続き明記する。
  設定ファイル名は実行ファイル名から決まるため、旧名の `SakuraEditor.ini` は初回起動時に `Ohrin.ini` へコピーする（`sakura_mac/legacy_settings.h`）。

計画の詳細は `docs/porting/mac_native_plan.md`、進捗は `docs/porting/progress.md`。
作業に入る前に必ず両方を確認すること。

## 要求仕様（ユーザー確定事項）

1. 再現性の優先順位: **編集挙動 > キー操作 > 設定互換（sakura.ini） > 見た目**
2. macOS 優先
3. ダイアログ: Windows と同じが望ましいが、機能が同じなら mac 流で可
4. キーバインド: Windows の Ctrl を **Cmd に置き換える**
5. プロセス構成: Windows と同じが望ましいが、mac 流（1 プロセス複数ウィンドウ）で可
6. **マクロは対応する**（キーマクロ + JavaScriptCore による JS マクロ。PPA / VBScript は不可）
7. `sakura_core/` はフォークする（変更可）
8. 1 人・約 2 ヶ月で MVP

## アーキテクチャ

```
sakura_mac/   … mac アプリのエントリ・.app バンドル（Week 3 以降）
sakura_core/  … upstream フォーク。エディタ本体（CEditWnd/CEditView/コマンド/マクロ）
winport/      … Win32 互換層
  include/    … Windows.h 等のヘッダ（mingw-w64 ヘッダ + 上書き）
  src/        … kernel32 / user32 / gdi32 / imm32 … の実装（Objective-C++ 可）
```

- 非 Windows ビルドでは `SAKURA_WINPORT` を定義する。
- 文字型: **wchar_t は 4 バイトのまま、中身は UTF-16 コード単位**（サロゲートペアは 2 要素）。
  Windows と同じ単位で数えるので編集挙動が一致する。バイト列⇔CNativeW の境界
  （UTF-16 コーデック、クリップボード、D&D）だけ幅変換が要る。決定経緯は `mac_native_plan.md`。

## 守るルール

1. `src/` `sakura_lang/` `sakura.sln` `tools/`（`tools/porting/` 以外）`externals/` `help/` `installer/`
   は upstream 資産として変更しない。
2. `sakura_core/` は変更可。ただし upstream マージを可能に保つため:
   - 変更は最小限にし、可能なら `#ifdef SAKURA_WINPORT` で囲む
   - 大きなロジック変更は避け、互換層側で吸収できるならそちらで吸収する
3. Windows ビルドの維持は必須ではないが、わざわざ壊さない。
4. コミットは小さく: 1 タスク = 1 コミット。

## 技術スタック

- C++17（clang / Apple SDK）、Objective-C++（互換層の AppKit 部分）
- CMake 3.20+、Ninja
- Win32 ヘッダの土台: Homebrew `mingw-w64` のヘッダ（public domain / ZPL）を `-idirafter` で参照
- 正規表現: externals の bregonig + Onigmo（`winport/src/bregonig_bridge.cpp` が組み込みの bregonig.dll として渡す）
- マクロ: JavaScriptCore

## ビルド・テストコマンド

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
tools/porting/make_dmg.sh   # Release ビルドと DMG（build-release/）
```

## 作業進捗の記録方法

タスク完了後は `docs/porting/progress.md` の先頭に追記する：

```markdown
## YYYY-MM-DD — 見出し
- [x] タスク名
  - 実施内容 / 発見した問題と対処 / 次への申し送り
```

エラーや保留事項は `docs/porting/issues.md` に記録する。
