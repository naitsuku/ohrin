# IME 対話確認手順書

最終更新: 2026-05-04 (Phase 6)

本書は wxStyledTextCtrl ベースの `sakura_wx` で日本語 IME 入力が
正しく動作するかを **実画面で確認するための手順** をまとめたもの。
ヘッドレス／自動テストでカバーできない領域。

## 対象環境

| OS | IME |
|---|---|
| macOS 13+ (Apple Silicon / Intel) | macOS 内蔵「日本語入力」(TSM/InputMethodKit) |
| Ubuntu 22.04+ | IBus + Mozc |
| Ubuntu 22.04+ | Fcitx5 + Mozc |
| Fedora 38+ | IBus + Mozc |

## 起動準備

### macOS

```bash
cd /path/to/sakuramac
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/bin/sakura_wx.app/Contents/MacOS/sakura_wx
```

システム環境設定で「日本語入力」が有効になっていることを確認。

### Linux (IBus + Mozc)

```bash
# 起動前に環境変数を export
export GTK_IM_MODULE=ibus
export QT_IM_MODULE=ibus
export XMODIFIERS=@im=ibus

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/bin/sakura_wx
```

### Linux (Fcitx5 + Mozc)

```bash
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx

./build/bin/sakura_wx
```

## チェックリスト

エディタにフォーカスした状態で IME を ON にし、以下を 1 つずつ確認：

| # | 項目 | 期待する動作 | 結果欄 |
|---|---|---|---|
| 1 | ひらがな入力 | "konnichiha" タイプで「こんにちは」が preedit 表示される | |
| 2 | preedit のアンダーライン | 確定前のテキストに下線または別色背景が表示される | |
| 3 | スペースキーで変換 | 「こんにちは」→「今日は」候補へ切替（候補ウィンドウ表示） | |
| 4 | 候補選択 | 矢印 / Tab / 数字キーで候補を選択できる | |
| 5 | Enter 確定 | 候補確定で preedit が通常テキストに変わる | |
| 6 | Esc キャンセル | 変換中に Esc で preedit を破棄できる | |
| 7 | Backspace | preedit 中の Backspace で 1 文字削除（半確定文字も対象） | |
| 8 | 候補ウィンドウ位置 | キャレット直下／直上に表示され、画面端でも切れない | |
| 9 | スクロール追従 | エディタを上下スクロールしても候補位置が正しく追従 | |
| 10 | 長文変換 | 30 文字以上の連文を入力し、文節区切り変換ができる | |
| 11 | カタカナ変換 | F7 / Ctrl+I（macOS は Ctrl+K）でカタカナへ | |
| 12 | 全角英数 | 半角/全角切替で全角英数を入力できる | |
| 13 | 中点・記号 | 「・」「、」「。」「「」「」」が想定通り入力される | |
| 14 | フォーカス遷移 | 検索ダイアログ（Ctrl+F）にフォーカスを移しても IME 状態が壊れない | |
| 15 | コピー＆貼付 | preedit ではなく確定済み日本語のコピー／貼付が正しく動く | |
| 16 | Undo / Redo | 確定済み日本語入力の Undo / Redo が 1 確定単位で動く | |
| 17 | 自動インデント | 改行直後の preedit と既存インデントの干渉なし | |
| 18 | 大量入力 | 1000 文字以上の連続入力でハング・候補ズレが出ない | |

## 結果記録テンプレート

問題が見つかったら `docs/porting/ime_issues.md` を新規作成して下表に記入：

```markdown
# IME 動作確認レポート

## テスト環境
- OS: macOS 14.5 (Apple Silicon) / Ubuntu 24.04 (Wayland) / ...
- IME: macOS 内蔵 / IBus 1.5.x + Mozc 2.x / ...
- wxWidgets: 3.3.2

## チェック結果

| # | 項目 | 結果 | 詳細 |
|---|---|---|---|
| 1 | ひらがな入力 | ✅ / ❌ / ⚠️ | （症状や再現手順） |
| 2 | preedit 下線 | | |
| ... | | | |

## 発見した問題

### Issue #1: <要約>
- 再現手順:
- 期待動作:
- 実際の動作:
- 推測される原因:
```

## 既知の wxWidgets / Scintilla 上の制約

1. **wxStyledTextCtrl は IME 標準サポート**: macOS では `IMEInputStyle` が
   自動で `wxSTC_IME_INLINE` または `wxSTC_IME_WINDOWED` を選択。
   Linux/GTK では `gtk_im_context_*` を内部で利用。
2. **候補ウィンドウ位置**: wxSTC は `Notify` 内で `cursor_location` を
   IME に通知するため、通常は追加実装不要。問題があれば
   `CEditViewWx::OnUpdateUI` で `PointFromPosition()` を計算して
   IME に明示通知する手段がある。
3. **macOS のみの注意**: wxFrame に SetIMEInputStyle を呼ぶには
   wxWidgets 3.1.6 以降が必要。本プロジェクトは 3.3.2 を使用。

## 問題発生時の調整ポイント

### preedit のスタイル
```cpp
// CEditViewWx::InitializeEditor() 末尾あたりに追加可能
SetIMEInteraction(wxSTC_IME_INLINE);
IndicatorSetStyle(wxSTC_INDIC_IME, wxSTC_INDIC_PLAIN);
IndicatorSetForeground(wxSTC_INDIC_IME, *wxBLACK);
```

### 候補ウィンドウ位置の明示通知
```cpp
void CEditViewWx::OnUpdateUI(wxStyledTextEvent& event) {
    int pos = GetCurrentPos();
    wxPoint pt = PointFromPosition(pos);
    // Linux: GTK4 以降で gtk_im_context_set_cursor_location 相当を呼ぶ
    // 通常は wxSTC が自動処理するため明示は不要
    event.Skip();
}
```

### Linux (Fcitx5) で候補窓が出ない場合
- `~/.config/fcitx5/profile` の Default IME に Mozc が含まれているか確認
- `XDG_RUNTIME_DIR` が設定されているか確認
- `dbus-launch fcitx5` でデーモンが動いているか
- 起動スクリプトで環境変数を設定する `tools/porting/sakura_wx_linux.sh`
  ラッパースクリプトの作成を Phase 7 で予定

## 報告窓口

問題が見つかったら以下のいずれかに記録：
1. `docs/porting/ime_issues.md`（このプロジェクト固有）
2. GitHub Issues（リポジトリの issue tracker）
