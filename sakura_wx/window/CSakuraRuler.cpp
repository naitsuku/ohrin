#include "CSakuraRuler.h"

#include <wx/dcbuffer.h>
#include <wx/pen.h>
#include <wx/settings.h>

namespace sakura_wx::ui {

CSakuraRuler::CSakuraRuler(wxWindow* parent, wxWindowID id)
    : wxWindow(parent, id, wxDefaultPosition, wxSize(-1, kHeight),
               wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);  // wxBufferedPaintDC 用
    SetMinSize(wxSize(-1, kHeight));
    Bind(wxEVT_PAINT, &CSakuraRuler::OnPaint, this);
}

CSakuraRuler::~CSakuraRuler() {
    Detach();
}

void CSakuraRuler::Attach(wxStyledTextCtrl* editor) {
    Detach();
    m_editor = editor;
    if (m_editor) {
        m_editor->Bind(wxEVT_STC_PAINTED, &CSakuraRuler::OnEditorPainted, this);
        m_editor->Bind(wxEVT_STC_UPDATEUI, &CSakuraRuler::OnEditorUpdateUI, this);
    }
    Refresh();
}

void CSakuraRuler::Detach() {
    if (m_editor) {
        m_editor->Unbind(wxEVT_STC_PAINTED, &CSakuraRuler::OnEditorPainted, this);
        m_editor->Unbind(wxEVT_STC_UPDATEUI, &CSakuraRuler::OnEditorUpdateUI, this);
        m_editor = nullptr;
    }
    m_lastXOffset = -1;
}

void CSakuraRuler::SetVisibleByMenu(bool visible) {
    Show(visible);
    if (auto* p = GetParent()) p->Layout();
}

void CSakuraRuler::OnEditorPainted(wxStyledTextEvent& event) {
    if (m_editor) {
        int xo = m_editor->GetXOffset();
        if (xo != m_lastXOffset) {
            m_lastXOffset = xo;
            Refresh(false);
        }
    }
    event.Skip();
}

void CSakuraRuler::OnEditorUpdateUI(wxStyledTextEvent& event) {
    // カーソル位置の変化など軽量イベント。横スクロールやマージン変更に追従するため
    // 念のため Refresh する（ちらつき防止のため eraseBackground=false）。
    if (m_editor) {
        int xo = m_editor->GetXOffset();
        if (xo != m_lastXOffset) {
            m_lastXOffset = xo;
            Refresh(false);
        }
    }
    event.Skip();
}

void CSakuraRuler::OnPaint(wxPaintEvent& /*event*/) {
    wxBufferedPaintDC dc(this);

    // 背景: STR_COLOR_RULER = RGB(239, 239, 239)
    static const wxColour kBg(239, 239, 239);
    static const wxColour kFg(  0,   0,   0);

    dc.SetBackground(wxBrush(kBg));
    dc.Clear();

    if (!m_editor) return;

    int width, height;
    GetClientSize(&width, &height);

    // エディタの 1 文字幅（等幅前提）。"M" の幅で代用。
    int charW = m_editor->TextWidth(wxSTC_STYLE_DEFAULT, wxT("M"));
    if (charW < 1) charW = 8;

    // エディタ左端からの全マージン幅（行番号 + ブックマーク + 折りたたみ）
    int marginW = m_editor->GetMarginWidth(0)
                + m_editor->GetMarginWidth(1)
                + m_editor->GetMarginWidth(2);

    int xOffset = m_editor->GetXOffset();

    // フォント
    wxFont font(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(font);
    dc.SetTextForeground(kFg);
    dc.SetPen(wxPen(kFg));

    // 上端と下端に薄い枠線
    dc.SetPen(wxPen(wxColour(180, 180, 180)));
    dc.DrawLine(0, height - 1, width, height - 1);
    dc.SetPen(wxPen(kFg));

    // テキスト列の x = marginW + col * charW - xOffset
    // 表示する列の範囲を計算
    int firstCol = (xOffset > 0) ? (xOffset / charW) : 0;
    int lastCol  = ((width - marginW + xOffset) / charW) + 2;

    for (int col = firstCol; col <= lastCol; ++col) {
        int x = marginW + col * charW - xOffset;
        if (x < marginW - 1) continue;
        if (x > width) break;

        if (col == 0) {
            // 列 0 はラベル無し（マージン直後）
            continue;
        }

        if (col % 10 == 0) {
            // 大目盛り + 数字
            dc.DrawLine(x, height - 7, x, height - 1);
            wxString label = wxString::Format(wxT("%d"), col);
            int tw, th;
            dc.GetTextExtent(label, &tw, &th);
            int labelX = x - tw / 2;
            if (labelX < marginW) labelX = marginW;
            dc.DrawText(label, labelX, 1);
        } else if (col % 5 == 0) {
            // 中目盛り
            dc.DrawLine(x, height - 5, x, height - 1);
        } else {
            // 小目盛り
            dc.DrawLine(x, height - 3, x, height - 1);
        }
    }
}

} // namespace sakura_wx::ui
