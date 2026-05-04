#include "CEditViewWx.h"
#include "window/CEditWndWx.h"

#include <wx/filename.h>
#include <wx/strconv.h>

#include <fstream>

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

const char* CEditViewWx::EncodingName(Encoding e) {
    switch (e) {
    case Encoding::UTF8:     return "UTF-8";
    case Encoding::UTF8_BOM: return "UTF-8 BOM";
    case Encoding::SJIS:     return "Shift-JIS";
    case Encoding::EUCJP:    return "EUC-JP";
    case Encoding::UTF16LE:  return "UTF-16LE";
    case Encoding::UTF16BE:  return "UTF-16BE";
    default:                 return "Unknown";
    }
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

    m_encoding = DetectEncoding(bytes);
    wxString text = ConvertToWxString(bytes, m_encoding);

    SetText(text);
    EmptyUndoBuffer();
    SetSavePoint();

    m_currentPath = path;
    SetupSyntaxHighlighting(path);
    return true;
}

bool CEditViewWx::SaveFile(const wxString& path) {
    wxString text = GetText();
    auto bytes = ConvertFromWxString(text, m_encoding);

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

CEditViewWx::Encoding CEditViewWx::DetectEncoding(const std::vector<char>& bytes) const {
    auto u = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };

    if (bytes.size() >= 3 && u(0) == 0xEF && u(1) == 0xBB && u(2) == 0xBF) {
        return Encoding::UTF8_BOM;
    }
    if (bytes.size() >= 2) {
        if (u(0) == 0xFF && u(1) == 0xFE) return Encoding::UTF16LE;
        if (u(0) == 0xFE && u(1) == 0xFF) return Encoding::UTF16BE;
    }

    // ヒューリスティック: SJIS/EUC-JP の典型的な第1バイト範囲をカウント
    int sjisHits = 0;
    int eucHits  = 0;
    int hi = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        unsigned char c = u(i);
        if (c < 0x80) continue;
        ++hi;
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) ++sjisHits;
        if ((c >= 0xA1 && c <= 0xFE) || c == 0x8E || c == 0x8F)   ++eucHits;
    }

    if (hi == 0) return Encoding::UTF8;

    // UTF-8 妥当性の単純チェック: ASCII 以外がすべて UTF-8 multibyte 規則を
    // 満たすなら UTF-8 とみなす
    auto looksUtf8 = [&]() {
        for (size_t i = 0; i < bytes.size();) {
            unsigned char c = u(i);
            if (c < 0x80) { ++i; continue; }
            int extra = 0;
            if      ((c & 0xE0) == 0xC0) extra = 1;
            else if ((c & 0xF0) == 0xE0) extra = 2;
            else if ((c & 0xF8) == 0xF0) extra = 3;
            else return false;
            if (i + extra >= bytes.size()) return false;
            for (int k = 1; k <= extra; ++k) {
                if ((u(i + k) & 0xC0) != 0x80) return false;
            }
            i += extra + 1;
        }
        return true;
    };
    if (looksUtf8()) return Encoding::UTF8;

    if (sjisHits > eucHits) return Encoding::SJIS;
    if (eucHits > 0)        return Encoding::EUCJP;
    return Encoding::UTF8;
}

wxString CEditViewWx::ConvertToWxString(const std::vector<char>& bytes, Encoding enc) const {
    size_t offset = 0;
    if (enc == Encoding::UTF8_BOM) offset = 3;

    auto data = bytes.data() + offset;
    auto len  = bytes.size() - offset;

    switch (enc) {
    case Encoding::UTF8:
    case Encoding::UTF8_BOM:
        return wxString::FromUTF8(data, len);

    case Encoding::SJIS:
        return wxString(data, wxCSConv(wxT("CP932")), len);

    case Encoding::EUCJP:
        return wxString(data, wxCSConv(wxT("EUC-JP")), len);

    case Encoding::UTF16LE:
        return wxString(reinterpret_cast<const char*>(data + 2),
                        wxMBConvUTF16LE(), len > 2 ? len - 2 : 0);

    case Encoding::UTF16BE:
        return wxString(reinterpret_cast<const char*>(data + 2),
                        wxMBConvUTF16BE(), len > 2 ? len - 2 : 0);

    default:
        return wxString::FromUTF8(data, len);
    }
}

std::vector<char> CEditViewWx::ConvertFromWxString(const wxString& text, Encoding enc) const {
    auto from_utf8 = [&]() {
        auto utf8 = text.ToUTF8();
        return std::vector<char>(utf8.data(), utf8.data() + utf8.length());
    };

    switch (enc) {
    case Encoding::UTF8: return from_utf8();
    case Encoding::UTF8_BOM: {
        auto v = from_utf8();
        v.insert(v.begin(), {static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF)});
        return v;
    }
    case Encoding::SJIS: {
        auto mb = text.mb_str(wxCSConv(wxT("CP932")));
        return std::vector<char>(mb.data(), mb.data() + mb.length());
    }
    case Encoding::EUCJP: {
        auto mb = text.mb_str(wxCSConv(wxT("EUC-JP")));
        return std::vector<char>(mb.data(), mb.data() + mb.length());
    }
    case Encoding::UTF16LE: {
        auto mb = text.mb_str(wxMBConvUTF16LE());
        std::vector<char> v;
        v.push_back(static_cast<char>(0xFF));
        v.push_back(static_cast<char>(0xFE));
        v.insert(v.end(), mb.data(), mb.data() + mb.length());
        return v;
    }
    case Encoding::UTF16BE: {
        auto mb = text.mb_str(wxMBConvUTF16BE());
        std::vector<char> v;
        v.push_back(static_cast<char>(0xFE));
        v.push_back(static_cast<char>(0xFF));
        v.insert(v.end(), mb.data(), mb.data() + mb.length());
        return v;
    }
    default:
        return from_utf8();
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
        wxString::FromUTF8(EncodingName(m_encoding)),
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
