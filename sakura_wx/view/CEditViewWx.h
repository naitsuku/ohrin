#pragma once
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

    // --- 文字コード ---
    enum class Encoding { UTF8, UTF8_BOM, SJIS, EUCJP, UTF16LE, UTF16BE, Unknown };
    Encoding GetCurrentEncoding() const { return m_encoding; }
    static const char* EncodingName(Encoding e);

private:
    void InitializeEditor();
    void SetupSyntaxHighlighting(const wxString& filePath);
    void ApplyColorScheme();

    // 文字コード判定／変換
    Encoding DetectEncoding(const std::vector<char>& bytes) const;
    wxString ConvertToWxString(const std::vector<char>& bytes, Encoding enc) const;
    std::vector<char> ConvertFromWxString(const wxString& text, Encoding enc) const;

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

    wxDECLARE_EVENT_TABLE();
};
