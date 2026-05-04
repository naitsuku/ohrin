# CLAUDE.md — サクラエディタ wxWidgets 移植プロジェクト

## プロジェクト概要

サクラエディタ（Windows専用テキストエディタ）を wxWidgets を使って macOS / Linux に移植するプロジェクト。
zlib ライセンスのオープンソース。コミュニティへの貢献として開発。

- **元リポジトリ**: https://github.com/sakura-editor/sakura
- **移植方針**: Win32版（sakura.exe）と wxWidgets版（sakura_wx）を同一リポジトリで並行開発
- **共有コア**: `sakura_core/` を両バージョンが共有ライブラリとして使用
- **目標プラットフォーム**: macOS 13+ (Apple Silicon), Ubuntu 22.04+, Fedora 38+

## リポジトリの現在状態（2026-05-04 時点）

このリポジトリにはまだソースコードは存在しない。以下のファイルのみ：
- `CLAUDE.md` — 本ファイル
- `phase0_prompt.md` 〜 `phase3_5_prompt.md` — 移植作業の段階別プロンプト計画

`sakura/` `sakura_core/` `sakura_wx/` は **Phase 0 で初めて作られる**：
- `sakura/` と `sakura_core/` は `git clone https://github.com/sakura-editor/sakura.git` で取得
- `sakura_wx/` `tests/wx_tests/` `docs/porting/` `CMakeLists.txt` は Phase 0 〜 Phase 1 で新規作成

作業に入る前に必ずどのフェーズまで進んでいるか `docs/porting/progress.md` を確認すること
（存在しなければ Phase 0 の Task 0-1 から開始）。

## 作業計画（フェーズ別プロンプト）

実作業の手順は以下の4ファイルに段階的に詳細化されている。本 CLAUDE.md は方針、
phase_*.md は実装手順を記述したもの。**矛盾がある場合は本 CLAUDE.md を優先**。

- `phase0_prompt.md` — 環境構築、Win32依存箇所の自動抽出、PAL 設計
- `phase1_prompt.md` — CMake ビルドシステム、sakura_core の段階的ポータブル化
- `phase2_prompt.md` — wxWidgets フロントエンド本体（sakura_wx）の実装
- `phase3_5_prompt.md` — 後期統合、テスト、リリース準備

新しいタスクを始めるときは、まず該当フェーズのプロンプトを読むこと。

## アーキテクチャの核

中心設計は **プラットフォーム抽象化レイヤー（PAL）**：`sakura_core/platform/platform.h`
で宣言された抽象 API を、`platform_impl_win32.cpp`（`#ifdef _WIN32`）と
`platform_impl_wx.cpp`（`#ifndef _WIN32`）の2実装で切り替える。

抽象化対象：File I/O、ConfigStore（レジストリ ⇔ INI）、Clipboard、Dialog、
System（パス・OS情報）、SharedMemory（IPC）。

`sakura_core` から Win32 API を直接呼ぶ既存箇所を、PAL 経由の呼び出しに
段階的に置き換えていくのが移植の本質。詳細は `phase0_prompt.md` Task 0-4。

## 絶対に守るルール（変更禁止）

1. **`src/` `sakura_lang/` `sakura.sln` `tools/`（upstream 由来）`externals/` `help/` `installer/` 内のファイルを一切変更しない**（Win32 版エントリ／上流資産）
2. **`sakura_core/` の既存 `.cpp/.h` ファイルの既存ロジックを変更しない**
   - インクルード追加は OK
   - `#ifdef SAKURA_PLATFORM_WX` による条件分岐追加は OK
   - 既存関数の実装変更は NG
3. **Windows ビルドを壊さない**: 変更後は必ず `#ifdef _WIN32` で分岐させる
4. **コミットは小さく**: 1タスク = 1コミット。巨大なコミットは NG

## ディレクトリ構成（移植後）

```
sakuramac/
├── src/                 # Win32版（変更禁止、upstream のまま）
│   ├── main/            # sakura.exe 本体（cmake/cpp/msbuild/resources）
│   └── test/cpp/tests1/ # GoogleTest 既存ユニットテスト
├── sakura_core/         # 共有コアライブラリ（最小限の変更のみ）
│   └── platform/        # Phase 0 で追加: プラットフォーム抽象化レイヤー (PAL)
├── sakura_lang/         # ローカライズ DLL（Win32専用、変更禁止）
├── sakura_wx/           # Phase 2 で追加: wxWidgets フロントエンド
│   ├── app/
│   ├── window/
│   ├── view/
│   └── dialog/
├── tests/wx_tests/      # Phase 1 以降で追加: wxWidgets版のテスト
├── tools/               # upstream のビルドツール（変更禁止）
│   └── porting/         # Phase 0 で追加: 移植用解析スクリプト・検証用 mini プロジェクト
├── externals/           # upstream のサードパーティ依存（変更禁止）
├── help/, installer/    # upstream のドキュメント・インストーラ（変更禁止）
├── docs/porting/        # Phase 0 で追加: 移植作業ドキュメント
├── CMakeLists.txt       # 既存（Win32向け）— Phase 1 で非Windows分岐を追加
└── sakura.sln           # Visual Studio ソリューション（変更禁止）
```

> **注意**: 当初 CLAUDE.md は `sakura/` ディレクトリを Win32 エントリと記述していたが、
> 現 upstream master では Win32 エントリは `src/main/` に置かれている。
> 「変更禁止」の対象は実態として `src/`、`sakura_lang/`、`sakura.sln`、
> `tools/`（upstream 由来部分）、`externals/`、`help/`、`installer/`、ルート `*.bat`、
> ルート `CMakeLists.txt` の **既存 Windows 向けロジック部分**。

## 技術スタック

- **フレームワーク**: wxWidgets 3.2.x または 3.3.x（macOS Homebrew 提供版は現在 3.3.2）
- **ビルド**: CMake 3.20+, Ninja
- **C++標準**: C++17
- **テキスト制御**: wxStyledTextCtrl (Scintilla)
- **設定**: wxFileConfig (非Windows) / レジストリ (Windows)
- **IPC**: POSIX 共有メモリ (非Windows) / CreateFileMapping (Windows)

## ビルド・テストコマンド

Phase 1 以降、リポジトリルートで以下が使える想定：

```bash
# 構成（macOS / Linux）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# ビルド
cmake --build build

# 全テスト実行
ctest --test-dir build --output-on-failure

# 単一テスト実行（テスト名で絞り込み）
ctest --test-dir build -R <テスト名> --output-on-failure

# wxWidgets 環境確認（Phase 0 セットアップ後）
wx-config --version && wx-config --cxxflags | head -c 200
```

Windows ビルドは既存の `*.sln` を Visual Studio で開く（CMake は使わない）。

## 作業進捗の記録方法

タスク完了後は必ず `docs/porting/progress.md` を更新すること：

```markdown
## YYYY-MM-DD
- [x] タスク名
  - 実施内容の要約
  - 発見した問題と対処
  - 次のタスクへの申し送り
```

## エラー発生時の対処方針

1. エラーメッセージを `docs/porting/issues.md` に記録
2. 回避策が見つからない場合は TODO コメントを残して次のタスクへ進む
3. Win32依存で移植困難な機能は stub 実装（空実装）にして後回しにする

## コンパイルエラーの許容範囲

- `sakura_wx/` 内のエラー: 積極的に修正
- `sakura_core/platform/` 内のエラー: 積極的に修正
- `sakura_core/` 内の `#ifdef SAKURA_PLATFORM_WX` 分岐のエラー: 修正 OK
- `src/` `sakura_lang/` 内のエラー: **絶対に触らない**。報告のみ。
