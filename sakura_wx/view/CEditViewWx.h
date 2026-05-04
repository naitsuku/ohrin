#pragma once
#include "charset/Encoding.h"

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include <vector>

class CEditWndWx;

/**
 * CEditViewWx - テキスト編集ビュー（wxWidgets版）
 *
 * wxStyledTextCtrl (Scintilla) ベース。
 * - シンタックスハイライト（C/C++, Python, Markdown）
 * - 行番号表示／折り返し切替
 * - 日本語テキスト対応（UTF-8, Shift-JIS, EUC-JP, UTF-16 BOM）
 * - IME 入力は wxSTC の標準サポートに委譲（Phase 4 で詳細調整）
 */
class CEditViewWx : public wxStyledTextCtrl {
public:
    CEditViewWx(wxWindow* parent, wxWindowID id);

    // --- ファイル操作 ---
    bool LoadFile(const wxString& path);
    bool SaveFile(const wxString& path);

    // --- 表示設定 ---
    void SetLineNumbersVisible(bool visible);
    void SetWordWrapEnabled(bool enabled);
    void SetEditorFont(const wxFont& font);
    wxFont GetCurrentEditorFont() const { return m_currentFont; }

    // --- 表示の高度な設定（Phase 7） ---
    void SetWhitespaceVisible(bool visible);  // タブ・半角空白の可視化
    void SetIdeographicSpaceHighlight(bool visible); // 全角空白を黄色背景でハイライト
    void SetCodeFoldingEnabled(bool enabled); // 折りたたみ用マージンを表示
    bool IsWhitespaceVisible() const { return m_showWhitespace; }
    bool IsIdeographicSpaceHighlighted() const { return m_highlightIdeographic; }
    bool IsCodeFoldingEnabled() const { return m_foldingEnabled; }

    // --- 文字コード ---
    using Encoding = sakura_wx::charset::Encoding;
    Encoding GetCurrentEncoding() const { return m_encoding; }
    void SetEncoding(Encoding e) { m_encoding = e; }

    // --- 改行コードの手動指定（Phase 7） ---
    enum class EolMode { CRLF, LF, CR };
    EolMode GetCurrentEolMode() const { return m_eolMode; }
    static const char* EolName(EolMode m);
    void SetEolMode(EolMode m);
    void ConvertAllLineEndingsTo(EolMode m);

    // --- ブックマーク（Phase 7） ---
    static constexpr int kBookmarkMargin = 1;
    static constexpr int kBookmarkMarker  = 0;
    static constexpr int kCurrentLineMarker = 2;  // 現在行アンダーライン
    void ToggleBookmarkAtCurrent();
    void GoToNextBookmark();
    void GoToPrevBookmark();
    void ClearAllBookmarks();

    // --- 現在行アンダーライン (Phase 7 追加) ---
    void SetCurrentLineUnderline(bool visible);
    bool IsCurrentLineUnderlined() const { return m_currentLineUnderline; }

    // --- [EOF] 表示 (Phase 7 追加) ---
    void SetEofMarkVisible(bool visible);
    bool IsEofMarkVisible() const { return m_showEofMark; }

private:
    void InitializeEditor();
    void SetupSyntaxHighlighting(const wxString& filePath);
    void ApplyColorScheme();

    // イベントハンドラ
    void OnModified(wxStyledTextEvent& event);
    void OnUpdateUI(wxStyledTextEvent& event);
    void OnCharAdded(wxStyledTextEvent& event);
    void OnSavePointReached(wxStyledTextEvent& event);
    void OnSavePointLeft(wxStyledTextEvent& event);

    CEditWndWx* GetParentFrame() const;
    void UpdateStatusBarPos();

    Encoding  m_encoding = Encoding::UTF8;
    wxString  m_currentPath;
    wxFont    m_currentFont;
    EolMode   m_eolMode = EolMode::LF;

    bool      m_showWhitespace      = false;
    bool      m_highlightIdeographic = false;
    bool      m_foldingEnabled      = false;
    bool      m_currentLineUnderline = false;
    bool      m_showEofMark         = false;

    wxDECLARE_EVENT_TABLE();
};
