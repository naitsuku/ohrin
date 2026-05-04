#pragma once
#include <wx/stc/stc.h>
#include <wx/window.h>

namespace sakura_wx::ui {

/**
 * CSakuraRuler — エディタ最上部に表示する列番号ルーラー。
 *
 * Windows 版サクラエディタの「ルーラーを表示」相当。
 * - 背景は STR_COLOR_RULER 相当 (RGB 239,239,239)
 * - 文字幅単位で目盛りを描く（1 ごとに小、5 ごとに中、10 ごとに大＋数値）
 * - 観察対象 wxStyledTextCtrl の GetXOffset() / マージン幅 / 等幅文字幅を反映
 * - wxEVT_STC_PAINTED でポーリング再描画（横スクロール追従）
 *
 * 単一のルーラーを CEditWndWx の vertical sizer 上部に置き、タブ切替時に
 * Attach(view) で追従先を切り替える設計。
 */
class CSakuraRuler : public wxWindow {
public:
    CSakuraRuler(wxWindow* parent, wxWindowID id = wxID_ANY);
    ~CSakuraRuler() override;

    /// 観察対象エディタを設定。nullptr で解除。
    void Attach(wxStyledTextCtrl* editor);

    /// 観察解除（デストラクタからも呼ばれる）。
    void Detach();

    /// 表示の有無切替。Hide() / Show() の薄いラッパ。
    void SetVisibleByMenu(bool visible);

    /// ルーラーの推奨高さ
    static constexpr int kHeight = 18;

private:
    void OnPaint(wxPaintEvent& event);
    void OnEditorPainted(wxStyledTextEvent& event);
    void OnEditorUpdateUI(wxStyledTextEvent& event);

    wxStyledTextCtrl* m_editor = nullptr;
    int m_lastXOffset = -1;  // 直前の横スクロール位置（変化検出用）
};

} // namespace sakura_wx::ui
