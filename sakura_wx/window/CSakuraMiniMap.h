#pragma once
#include <wx/stc/stc.h>

class CEditViewWx;

namespace sakura_wx::ui {

/**
 * CSakuraMiniMap — メインビューの縮小表示パネル (Phase 15b)。
 *
 * 別の wxStyledTextCtrl インスタンスを小さなフォントで表示し、
 * メインビューと同じテキストをシェアする。スクロール位置を同期し、
 * クリックで対応行へジャンプする。
 *
 * メインビューとのテキスト同期方針:
 *   - SC_MOD_INSERTTEXT / SC_MOD_DELETETEXT を OnModified で受信し
 *     ミニマップ側に同じ操作を再生する (SetText 全体置換は重いため避ける)
 *   - スクロール: メインビューの SCN_UPDATEUI で FirstVisibleLine を取得し
 *     ミニマップ側を ScrollToLine する (再帰防止フラグあり)
 *   - クリック: ミニマップ上のクリック位置をメインビューにジャンプさせる
 *
 * パフォーマンス考慮:
 *   - ミニマップは編集不可 (SetReadOnly(true))
 *   - 折り返し OFF / 行番号 OFF / マージン無し
 *   - 再描画頻度を抑えるため SetCaretPeriod(0)
 */
class CSakuraMiniMap : public wxStyledTextCtrl {
public:
    CSakuraMiniMap(wxWindow* parent);

    /// メインビューと同期を開始する (Detach 不要、view が破棄されたら呼ぶ側が責任)
    void Attach(CEditViewWx* view);
    void Detach();

private:
    void OnMainModified(wxStyledTextEvent& event);
    void OnMainUpdateUI(wxStyledTextEvent& event);
    void OnSelfClick(wxMouseEvent& event);

    void SyncTextFromMain();   // 全文同期 (Attach 時)
    void SyncFirstVisibleLine();

    CEditViewWx* m_main = nullptr;
    bool         m_suppressSync = false;
};

} // namespace sakura_wx::ui
