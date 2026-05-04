#include "CEditViewWx.h"
#include "window/CEditWndWx.h"

#include <wx/filename.h>

#include <fstream>

namespace charset = sakura_wx::charset;

wxBEGIN_EVENT_TABLE(CEditViewWx, wxStyledTextCtrl)
    EVT_STC_MODIFIED(wxID_ANY,    CEditViewWx::OnModified)
    EVT_STC_UPDATEUI(wxID_ANY,    CEditViewWx::OnUpdateUI)
    EVT_STC_CHARADDED(wxID_ANY,   CEditViewWx::OnCharAdded)
    EVT_STC_SAVEPOINTREACHED(wxID_ANY, CEditViewWx::OnSavePointReached)
    EVT_STC_SAVEPOINTLEFT(wxID_ANY,    CEditViewWx::OnSavePointLeft)
wxEND_EVENT_TABLE()

CEditViewWx::CEditViewWx(wxWindow* parent, wxWindowID id)
    : wxStyledTextCtrl(parent, id)
{
    InitializeEditor();
}

void CEditViewWx::InitializeEditor() {
    // 行番号マージン
    SetMarginType(0, wxSTC_MARGIN_NUMBER);
    SetMarginWidth(0, 50);

    // シンボルマージン（折りたたみ等で使う、初期非表示）
    SetMarginType(1, wxSTC_MARGIN_SYMBOL);
    SetMarginWidth(1, 0);

    // タブ・インデント
    SetTabWidth(4);
    SetUseTabs(false);
    SetIndent(4);
    SetTabIndents(true);
    SetBackSpaceUnIndents(true);

    // 折り返し（初期 OFF）
    SetWrapMode(wxSTC_WRAP_NONE);

    // 現在行ハイライト
    SetCaretLineVisible(true);

    // Scintilla を UTF-8 で動かす（外部入出力時に変換する）
    SetCodePage(wxSTC_CP_UTF8);

    // デフォルトフォントを選択
    wxFont font;
#if defined(__APPLE__)
    font = wxFont(13, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL, false, wxT("Menlo"));
    if (!font.IsOk()) {
        font = wxFont(13, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                      wxFONTWEIGHT_NORMAL, false, wxT("Osaka-Mono"));
    }
#elif defined(__WXGTK__)
    font = wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL, false, wxT("Noto Sans Mono CJK JP"));
    if (!font.IsOk()) {
        font = wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                      wxFONTWEIGHT_NORMAL, false, wxT("DejaVu Sans Mono"));
    }
#else
    font = wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL);
#endif
    if (font.IsOk()) {
        SetEditorFont(font);
    } else {
        ApplyColorScheme();
    }
}

void CEditViewWx::ApplyColorScheme() {
    StyleSetBackground(wxSTC_STYLE_DEFAULT, *wxWHITE);
    StyleSetForeground(wxSTC_STYLE_DEFAULT, *wxBLACK);
    StyleClearAll();

    StyleSetBackground(wxSTC_STYLE_LINENUMBER, wxColour(240, 240, 240));
    StyleSetForeground(wxSTC_STYLE_LINENUMBER, wxColour(100, 100, 100));

    SetCaretLineBackground(wxColour(232, 242, 254));
    SetCaretForeground(*wxBLACK);
}

bool CEditViewWx::LoadFile(const wxString& path) {
    std::ifstream ifs(std::string(path.ToUTF8()), std::ios::binary | std::ios::ate);
    if (!ifs) return false;

    auto fileSize = ifs.tellg();
    if (fileSize < 0) return false;
    ifs.seekg(0);
    std::vector<char> bytes(static_cast<size_t>(fileSize));
    if (fileSize > 0 && !ifs.read(bytes.data(), fileSize)) return false;

    m_encoding = charset::Detect(bytes);
    wxString text = charset::DecodeToWxString(bytes, m_encoding);

    SetText(text);
    EmptyUndoBuffer();
    SetSavePoint();

    m_currentPath = path;
    SetupSyntaxHighlighting(path);
    return true;
}

bool CEditViewWx::SaveFile(const wxString& path) {
    wxString text = GetText();
    auto bytes = charset::EncodeFromWxString(text, m_encoding);

    std::ofstream ofs(std::string(path.ToUTF8()), std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    if (!bytes.empty()) {
        ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!ofs) return false;
    }
    SetSavePoint();
    m_currentPath = path;
    return true;
}

void CEditViewWx::SetLineNumbersVisible(bool visible) {
    SetMarginWidth(0, visible ? 50 : 0);
}

void CEditViewWx::SetWordWrapEnabled(bool enabled) {
    SetWrapMode(enabled ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE);
}

void CEditViewWx::SetEditorFont(const wxFont& font) {
    m_currentFont = font;
    StyleSetFont(wxSTC_STYLE_DEFAULT, font);
    StyleClearAll();
    ApplyColorScheme();
    if (!m_currentPath.IsEmpty()) {
        SetupSyntaxHighlighting(m_currentPath);
    }
}

void CEditViewWx::SetupSyntaxHighlighting(const wxString& filePath) {
    wxString ext = wxFileName(filePath).GetExt().Lower();

    if (ext == wxT("cpp") || ext == wxT("cxx") || ext == wxT("cc") ||
        ext == wxT("c")   || ext == wxT("h")   || ext == wxT("hpp") ||
        ext == wxT("hh")  || ext == wxT("ipp"))
    {
        SetLexer(wxSTC_LEX_CPP);
        SetKeyWords(0, wxT(
            "alignas alignof and and_eq asm auto bitand bitor bool break "
            "case catch char char8_t char16_t char32_t class compl concept "
            "const consteval constexpr constinit const_cast continue co_await "
            "co_return co_yield decltype default delete do double dynamic_cast "
            "else enum explicit export extern false float for friend goto if "
            "inline int long mutable namespace new noexcept not not_eq nullptr "
            "operator or or_eq private protected public register reinterpret_cast "
            "requires return short signed sizeof static static_assert "
            "static_cast struct switch template this thread_local throw true "
            "try typedef typeid typename union unsigned using virtual void "
            "volatile wchar_t while xor xor_eq"));

        StyleSetForeground(wxSTC_C_COMMENT,      wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_C_COMMENTLINE,  wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_C_COMMENTDOC,   wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_C_STRING,       wxColour(  0,   0, 255));
        StyleSetForeground(wxSTC_C_CHARACTER,    wxColour(  0,   0, 255));
        StyleSetForeground(wxSTC_C_WORD,         wxColour(  0,   0, 128));
        StyleSetForeground(wxSTC_C_NUMBER,       wxColour(128,   0,   0));
        StyleSetForeground(wxSTC_C_PREPROCESSOR, wxColour(128,   0, 128));
        StyleSetBold(wxSTC_C_WORD, true);

    } else if (ext == wxT("py")) {
        SetLexer(wxSTC_LEX_PYTHON);
        SetKeyWords(0, wxT(
            "False None True and as assert async await break class continue "
            "def del elif else except finally for from global if import in "
            "is lambda nonlocal not or pass raise return try while with yield"));
        StyleSetForeground(wxSTC_P_COMMENTLINE, wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_P_STRING,      wxColour(  0,   0, 255));
        StyleSetForeground(wxSTC_P_WORD,        wxColour(  0,   0, 128));
        StyleSetForeground(wxSTC_P_NUMBER,      wxColour(128,   0,   0));
        StyleSetBold(wxSTC_P_WORD, true);

    } else if (ext == wxT("md") || ext == wxT("markdown")) {
        SetLexer(wxSTC_LEX_MARKDOWN);
    } else {
        SetLexer(wxSTC_LEX_NULL);
    }
}

CEditWndWx* CEditViewWx::GetParentFrame() const {
    return dynamic_cast<CEditWndWx*>(GetGrandParent() ? GetGrandParent() : GetParent());
}

void CEditViewWx::UpdateStatusBarPos() {
    auto* frame = GetParentFrame();
    if (!frame) return;
    if (!frame->GetStatusBar()) return;

    int pos  = GetCurrentPos();
    int line = LineFromPosition(pos) + 1;
    int col  = GetColumn(pos) + 1;
    frame->SetStatusText(
        wxString::Format(wxT("行 %d  列 %d"), line, col),
        CEditWndWx::kStatusFieldCursor);
    frame->SetStatusText(
        wxString::FromUTF8(charset::EncodingName(m_encoding)),
        CEditWndWx::kStatusFieldEncoding);
}

void CEditViewWx::OnModified(wxStyledTextEvent& event) {
    event.Skip();
}

void CEditViewWx::OnUpdateUI(wxStyledTextEvent& event) {
    UpdateStatusBarPos();
    event.Skip();
}

void CEditViewWx::OnCharAdded(wxStyledTextEvent& event) {
    int ch = event.GetKey();
    if (ch == '\n') {
        int line = GetCurrentLine();
        if (line > 0) {
            wxString prev = GetLineText(line - 1);
            wxString indent;
            for (size_t i = 0; i < prev.length(); ++i) {
                wxUniChar c = prev[i];
                if (c == ' ' || c == '\t') indent += c;
                else break;
            }
            if (!indent.IsEmpty()) {
                InsertText(GetCurrentPos(), indent);
                GotoPos(GetCurrentPos() + static_cast<int>(indent.length()));
            }
        }
    }
    event.Skip();
}

void CEditViewWx::OnSavePointReached(wxStyledTextEvent& event) {
    if (auto* frame = GetParentFrame()) {
        frame->NotifyModified(false);
    }
    event.Skip();
}

void CEditViewWx::OnSavePointLeft(wxStyledTextEvent& event) {
    if (auto* frame = GetParentFrame()) {
        frame->NotifyModified(true);
    }
    event.Skip();
}
