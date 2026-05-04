# Phase 3: 設定・IPC の移植

## 前提条件
- Phase 2 が完了していること
- `sakura_wx` がビルドできて日本語テキストが表示できること

## このフェーズのゴール
1. 設定の読み書きをプラットフォーム非依存にする（INI ファイル）
2. マルチプロセス対応の共有データを POSIX 共有メモリで実現する

---

## Task 3-1: 設定システムの統合

Phase 0 で作成した `IConfigStore` を実際のサクラエディタ設定に接続する。

### 設定クラスのラッパーを作成

`sakura_wx/config/SakuraConfig.h` を作成せよ：

```cpp
#pragma once
#include "platform/platform.h"
#include <wx/font.h>
#include <wx/colour.h>

/**
 * SakuraConfig - サクラエディタ設定の管理クラス（wxWidgets版）
 *
 * PAL の IConfigStore を使って設定を読み書きする。
 * 設定ファイルの場所：
 *   macOS: ~/Library/Application Support/SakuraEditor/sakura.ini
 *   Linux: ~/.config/sakura/sakura.ini
 */
class SakuraConfig {
public:
    SakuraConfig();
    ~SakuraConfig();

    // 設定の読み込み・保存
    void Load();
    void Save();

    // --- フォント設定 ---
    wxFont  GetEditorFont() const;
    void    SetEditorFont(const wxFont& font);

    // --- 表示設定 ---
    bool    GetShowLineNumbers() const   { return m_showLineNumbers; }
    void    SetShowLineNumbers(bool v)   { m_showLineNumbers = v; }

    bool    GetWordWrap() const          { return m_wordWrap; }
    void    SetWordWrap(bool v)          { m_wordWrap = v; }

    int     GetTabWidth() const          { return m_tabWidth; }
    void    SetTabWidth(int v)           { m_tabWidth = v; }

    // --- ウィンドウ位置 ---
    wxPoint GetWindowPosition() const    { return m_windowPos; }
    void    SetWindowPosition(wxPoint p) { m_windowPos = p; }

    wxSize  GetWindowSize() const        { return m_windowSize; }
    void    SetWindowSize(wxSize s)      { m_windowSize = s; }

    // --- 最近使ったファイル ---
    wxArrayString GetRecentFiles() const { return m_recentFiles; }
    void          AddRecentFile(const wxString& path);

private:
    std::unique_ptr<SakuraPlatform::IConfigStore> m_store;

    // 設定値
    wxString      m_fontFace   = wxT("Monospace");
    int           m_fontSize   = 12;
    bool          m_showLineNumbers = true;
    bool          m_wordWrap   = false;
    int           m_tabWidth   = 4;
    wxPoint       m_windowPos  = wxDefaultPosition;
    wxSize        m_windowSize = wxSize(900, 700);
    wxArrayString m_recentFiles;
    static const int MAX_RECENT_FILES = 10;
};
```

`SakuraConfig.cpp` の実装：
- `Load()`: IConfigStore::GetString/GetInt/GetBool を使って設定を読み込む
- `Save()`: IConfigStore::SetString/SetInt/SetBool を使って設定を保存する
- `AddRecentFile()`: 最近使ったファイルリストに追加（重複排除、MAX_RECENT_FILES 件まで）

`SakuraApp` にグローバル設定インスタンスを追加し、
アプリ起動時に Load()、終了時に Save() を呼ぶこと。

---

## Task 3-2: DLLSHAREDATA の POSIX 実装

`sakura_core/env/` の共有メモリ実装を Phase 0 の PAL を使って抽象化する。

`sakura_core/env/CSharedDataPosix.cpp` を新規作成せよ（既存ファイルは変更しない）：

```cpp
/**
 * CSharedDataPosix.cpp
 * 非Windows向け共有メモリ実装
 * 
 * DLLSHAREDATA 構造体は既存の定義をそのまま使用。
 * 共有メモリのバックエンドのみを POSIX に切り替える。
 */
#ifndef _WIN32

#include "env/DLLSHAREDATA.h"   // 既存の構造体定義
#include "platform/platform.h"
#include <memory>

// グローバルな共有メモリインスタンス
static std::unique_ptr<SakuraPlatform::ISharedMemory> g_sharedMem;
static DLLSHAREDATA* g_pShareData = nullptr;

/**
 * 共有メモリの初期化
 * アプリ起動時（最初のプロセス）に呼ぶ
 */
bool InitSharedData() {
    const std::string SHM_NAME = "sakura_editor_shared";
    
    // まず既存の共有メモリに接続を試みる
    g_sharedMem = SakuraPlatform::CreateSharedMemory(
        SHM_NAME, sizeof(DLLSHAREDATA), false);
    
    bool isFirst = !g_sharedMem || !g_sharedMem->IsValid();
    
    if (isFirst) {
        // 最初のプロセス: 新規作成
        g_sharedMem = SakuraPlatform::CreateSharedMemory(
            SHM_NAME, sizeof(DLLSHAREDATA), true);
        if (!g_sharedMem || !g_sharedMem->IsValid()) {
            return false;
        }
        // ゼロ初期化
        g_pShareData = static_cast<DLLSHAREDATA*>(g_sharedMem->GetPtr());
        memset(g_pShareData, 0, sizeof(DLLSHAREDATA));
    } else {
        g_pShareData = static_cast<DLLSHAREDATA*>(g_sharedMem->GetPtr());
    }
    
    return true;
}

/**
 * 共有データへのポインタを取得
 */
DLLSHAREDATA* GetShareData() {
    return g_pShareData;
}

/**
 * 共有メモリの解放
 */
void CleanupSharedData() {
    g_pShareData = nullptr;
    g_sharedMem.reset();
}

#endif // !_WIN32
```

`SakuraApp::OnInit()` に `InitSharedData()` の呼び出しを追加し、
`SakuraApp::OnExit()` に `CleanupSharedData()` の呼び出しを追加せよ。

---

# Phase 4: IME・日本語入力の品質保証

## 前提条件
- Phase 2 で CEditViewWx が動作していること

## このフェーズのゴール
- Linux (IBus/Fcitx5) と macOS (TSM) で日本語入力が正常に動作すること
- 変換候補ウィンドウがカーソル位置に正しく表示されること
- preedit（変換中テキスト）にアンダーラインが表示されること

---

## Task 4-1: IME 動作の現状確認と問題リスト作成

実際に日本語入力を試して問題点を `docs/porting/ime_issues.md` に記録せよ。

確認事項（各項目を「正常」「問題あり（詳細）」で記録）：

```markdown
# IME 動作確認レポート

## テスト環境
- OS: [Ubuntu 24.04 / macOS 14 など]
- IME: [IBus + Mozc / Fcitx5 / macOS 日本語入力]

## 確認項目

| 項目 | 結果 | 詳細 |
|------|------|------|
| ひらがな入力 | | |
| カタカナ変換（スペースキー） | | |
| 漢字変換確定（Enterキー） | | |
| 変換キャンセル（Escキー） | | |
| preedit テキストの表示 | | |
| 変換候補ウィンドウの位置 | | |
| Backspace で preedit 削除 | | |
| 全角英数切り替え | | |
| 長文変換（10文字以上） | | |
| 複数候補からの選択 | | |
```

---

## Task 4-2: IME 対応の実装

Task 4-1 で発見した問題を修正せよ。

### wxStyledTextCtrl の IME 設定

`CEditViewWx::InitializeEditor()` に以下を追加せよ：

```cpp
// IME 設定（wxWidgets 3.1.6 以降で利用可能）
// IMEが有効な場合、preedit テキストを STC 内部で処理させる
#if wxCHECK_VERSION(3, 1, 6)
    SetIMEInputStyle(wxSTC_IME_WINDOWED);
#endif

// Linux GTK での IME 有効化
// GTK バックエンドでは gtk_im_context を STC が管理する
```

### カーソル位置の IME への通知

`OnUpdateUI` イベントで、現在のキャレット位置を取得して
IME 候補ウィンドウの表示位置を更新せよ：

```cpp
void CEditViewWx::OnUpdateUI(wxStyledTextEvent& event) {
    // ... 既存コード ...
    
    // IME 候補ウィンドウ位置の更新
    // GetCurrentPos() でバイト位置、PointFromPosition() でスクリーン座標を取得
    int pos = GetCurrentPos();
    wxPoint pt = PointFromPosition(pos);
    // wxIME への通知（プラットフォーム固有）
    // Linux: GtkIMContext の gtk_im_context_set_cursor_location
    // macOS: NSTextInputClient の firstRectForCharacterRange
    // → wxWidgets が内部で処理するため、通常は追加実装不要
    
    event.Skip();
}
```

### Linux (GTK) での Fcitx5 対応

環境変数の設定を確認せよ。問題がある場合、起動スクリプトを作成：

```bash
#!/bin/bash
# sakura_wx_launch.sh
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx
exec "$(dirname "$0")/sakura_wx" "$@"
```

### macOS での preedit スタイル設定

```cpp
// macOS: preedit テキストのスタイル設定
// STC では SetCaretStyle で preedit 中のカーソル表示を調整可能
void CEditViewWx::SetupMacOSIME() {
#ifdef __APPLE__
    // macOS では自動的に TSM が処理されるが、
    // preedit の下線スタイルを明示的に設定
    IndicatorSetStyle(wxSTC_INDIC_IME, wxSTC_INDIC_PLAIN);
    IndicatorSetForeground(wxSTC_INDIC_IME, *wxBLACK);
#endif
}
```

---

## Task 4-3: IME テストの自動化（可能な範囲で）

`tests/wx_tests/test_ime_basics.cpp` を作成せよ。
GUIテストは自動化が難しいため、単体テスト可能な部分のみ：

```cpp
#include <gtest/gtest.h>
#include "view/CEditViewWx.h"

// 文字コード変換の単体テスト
TEST(CharEncoding, ShiftJIS_RoundTrip) {
    // "日本語テスト" を Shift-JIS → wxString → Shift-JIS で変換
    // バイト列が一致することを確認
}

TEST(CharEncoding, UTF8_WithBOM_Detection) {
    // UTF-8 BOM ありファイルの検出テスト
}

TEST(CharEncoding, EUCJP_Detection) {
    // EUC-JP ファイルの検出テスト
}
```

---

# Phase 5: 機能完成・リリース準備

## 前提条件
- Phase 1〜4 が完了していること
- IME 入力が基本的に動作していること

## このフェーズのゴール
1. 主要ダイアログの実装を完成させる
2. macOS (.app バンドル + DMG) / Linux (AppImage) のパッケージを作成する
3. GitHub Releases への自動配布を設定する

---

## Task 5-1: 検索・置換ダイアログの完全実装

Phase 2 で作成した CDlgSearchWx を完成させよ。

要件：
- 検索文字列（wxTextCtrl）
- 置換文字列（wxTextCtrl）
- オプション：大文字小文字区別（wxCheckBox）
- オプション：正規表現（wxCheckBox）
- オプション：単語単位（wxCheckBox）
- ボタン：次を検索、前を検索、置換、すべて置換、閉じる
- 検索ヒット時にハイライト表示
- ヒット件数をステータスに表示
- Ctrl+H で開くとき置換欄にフォーカス

実装のポイント：
```cpp
void CDlgSearchWx::DoSearch(bool forward) {
    int flags = 0;
    if (m_caseSensitive->IsChecked()) flags |= wxSTC_FIND_MATCHCASE;
    if (m_wholeWord->IsChecked())     flags |= wxSTC_FIND_WHOLEWORD;
    if (m_regex->IsChecked())         flags |= wxSTC_FIND_REGEXP;
    
    wxString searchText = m_searchCtrl->GetValue();
    
    // CEditViewWx の SearchNext/SearchPrev を呼ぶ
    m_view->SearchAnchor();
    int found = forward
        ? m_view->SearchNext(flags, searchText)
        : m_view->SearchPrev(flags, searchText);
    
    if (found != -1) {
        m_view->EnsureCaretVisible();
        SetStatusText(wxString::Format(wxT("見つかりました（位置 %d）"), found));
    } else {
        SetStatusText(wxT("見つかりませんでした"));
        wxBell();
    }
}
```

---

## Task 5-2: Grep ダイアログ

`sakura_wx/dialog/CDlgGrepWx.h/.cpp` を実装せよ。

要件：
- 検索文字列
- 検索フォルダ（フォルダ選択ボタン付き）
- ファイルパターン（*.txt;*.cpp 形式）
- 再帰検索チェックボックス
- 実行ボタン
- 結果表示エリア（wxListCtrl）：ファイル名・行番号・マッチ行
- 結果行をダブルクリックでファイルを開いてその行へ移動

実装のポイント（wxWidgets のスレッド利用）：
```cpp
// Grep は時間がかかるため wxThread で非同期実行
class GrepThread : public wxThread {
    // std::filesystem::recursive_directory_iterator でファイルを列挙
    // 各ファイルを開いて正規表現検索
    // 結果を wxPostEvent でメインスレッドに送信
};
```

---

## Task 5-3: macOS パッケージング（.app + DMG）

`.github/workflows/release.yml` にmacOSリリースジョブを追加せよ：

```yaml
  package-macos:
    name: Package (macOS)
    runs-on: macos-14
    if: startsWith(github.ref, 'refs/tags/v')
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: brew install wxwidgets cmake ninja create-dmg

      - name: Build
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
          cmake --build build --parallel

      - name: Bundle libraries
        run: |
          # wx ライブラリを .app に同梱
          cd build/bin
          # dylib を Frameworks に コピーして install_name_tool で修正
          python3 ../../tools/bundle_macos.py sakura_wx.app

      - name: Create DMG
        run: |
          create-dmg \
            --volname "SakuraEditor" \
            --window-pos 200 120 \
            --window-size 600 300 \
            --icon-size 100 \
            --icon "sakura_wx.app" 175 120 \
            --hide-extension "sakura_wx.app" \
            --app-drop-link 425 120 \
            "SakuraEditor-${{ github.ref_name }}-macOS.dmg" \
            "build/bin/sakura_wx.app"

      - name: Upload Release Asset
        uses: softprops/action-gh-release@v1
        with:
          files: SakuraEditor-*.dmg
```

---

## Task 5-4: Linux パッケージング（AppImage）

```yaml
  package-linux:
    name: Package (Linux AppImage)
    runs-on: ubuntu-22.04
    if: startsWith(github.ref, 'refs/tags/v')
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y libwxgtk3.2-dev cmake ninja-build \
            libgtk-3-dev fuse

      - name: Build
        run: |
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
          cmake --build build --parallel
          cmake --install build --prefix AppDir/usr

      - name: Create AppImage
        run: |
          wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
          wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-gtk/releases/download/continuous/linuxdeploy-plugin-gtk-x86_64.AppImage
          chmod +x linuxdeploy*.AppImage
          
          # .desktop ファイルを作成
          cat > AppDir/sakura_wx.desktop << EOF
          [Desktop Entry]
          Type=Application
          Name=SakuraEditor
          Comment=Japanese Text Editor
          Exec=sakura_wx %F
          Icon=sakura_wx
          Categories=Utility;TextEditor;
          MimeType=text/plain;
          EOF
          
          DEPLOY_GTK_VERSION=3 \
          ./linuxdeploy-x86_64.AppImage \
            --appdir AppDir \
            --plugin gtk \
            --output appimage \
            --desktop-file AppDir/sakura_wx.desktop
          
          mv SakuraEditor*.AppImage "SakuraEditor-${{ github.ref_name }}-linux-x86_64.AppImage"

      - name: Upload Release Asset
        uses: softprops/action-gh-release@v1
        with:
          files: SakuraEditor-*.AppImage
```

---

## Task 5-5: ドキュメントの整備

`docs/porting/PORTING_GUIDE.md` を作成せよ（コントリビューター向け）：

```markdown
# サクラエディタ wxWidgets 移植 — コントリビューターガイド

## 概要
本ドキュメントは、サクラエディタの wxWidgets 移植に
コントリビュートするための技術ガイドです。

## アーキテクチャ

### コンポーネント間の関係
[Phase 0-5 で実装した構造を図示]

## 開発環境のセットアップ
### macOS
### Linux (Ubuntu)
### Linux (Fedora)

## ビルド方法
[cmake コマンドの説明]

## テストの実行
[ctest コマンドの説明]

## 未実装・既知の問題
[excluded_files.md と issues.md の内容を統合]

## コントリビュートのやり方
1. Issue を確認する
2. ブランチを作る: git checkout -b porting/your-feature
3. 変更を加える（絶対に守るルール: CLAUDE.md 参照）
4. PR を出す

## Win32依存箇所のポーティングパターン集
[Phase 0-4 で使ったパターンを整理]
```

---

## Phase 5 完了チェックリスト

- [ ] 検索・置換ダイアログが機能する
- [ ] Grep ダイアログが機能する
- [ ] macOS で .dmg が生成される
- [ ] Linux で AppImage が生成される
- [ ] GitHub Actions release ワークフローが動作する
- [ ] `docs/porting/PORTING_GUIDE.md` が存在する
- [ ] 全テスト (ctest) がパスする

---

# まとめ: Claude Code への渡し方

## 推奨実行順序

```bash
# Phase 0: 依存分析・環境構築（1セッション）
cat phase0_prompt.md | claude-code

# Phase 1: ビルドシステム（1〜2セッション）
cat phase1_prompt.md | claude-code

# Phase 2: wxWidgets フロントエンド（2〜3セッション）
cat phase2_prompt.md | claude-code

# Phase 3: 設定・IPC（1セッション）
cat phase3_5_prompt.md | claude-code --section phase3

# Phase 4: IME（1〜2セッション、環境依存）
cat phase3_5_prompt.md | claude-code --section phase4

# Phase 5: パッケージング（1セッション）
cat phase3_5_prompt.md | claude-code --section phase5
```

## セッション間の引き継ぎ方

各フェーズ完了後、次のセッションの冒頭に以下を追加せよ：

```
CLAUDE.md を読んで、docs/porting/progress.md で前回の進捗を確認してから
[Phase N] の作業を開始してください。
```

## 問題が起きたとき

```
docs/porting/issues.md に記録された問題を確認し、
[具体的な問題] を解決してください。
アプローチ: [試したいアプローチ]
```
