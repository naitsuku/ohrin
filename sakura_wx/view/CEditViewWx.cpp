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
    // マージン 0: 行番号
    SetMarginType(0, wxSTC_MARGIN_NUMBER);
    SetMarginWidth(0, 50);

    // マージン 1: ブックマーク（クリックで toggle）
    SetMarginType(kBookmarkMargin, wxSTC_MARGIN_SYMBOL);
    SetMarginWidth(kBookmarkMargin, 16);
    SetMarginSensitive(kBookmarkMargin, true);
    // ブックマークマーカー: サクラ初期色 STR_COLOR_BOOKMARK
    // 文字色 RGB(255, 251, 240) (テキスト背景と同色)、背景 RGB(0, 128, 192)
    MarkerDefine(kBookmarkMarker, wxSTC_MARK_CIRCLE);
    MarkerSetForeground(kBookmarkMarker, wxColour(255, 251, 240));
    MarkerSetBackground(kBookmarkMarker, wxColour(  0, 128, 192));
    // マージンクリックハンドラを Bind
    Bind(wxEVT_STC_MARGINCLICK,
         [this](wxStyledTextEvent& e) {
             if (e.GetMargin() == kBookmarkMargin) {
                 int line = LineFromPosition(e.GetPosition());
                 if (MarkerGet(line) & (1 << kBookmarkMarker)) {
                     MarkerDelete(line, kBookmarkMarker);
                 } else {
                     MarkerAdd(line, kBookmarkMarker);
                 }
             } else if (e.GetMargin() == 2) {
                 // 折りたたみマージンクリック
                 int line = LineFromPosition(e.GetPosition());
                 ToggleFold(line);
             }
         });

    // マージン 2: 折りたたみ（初期は非表示）
    SetMarginType(2, wxSTC_MARGIN_SYMBOL);
    SetMarginMask(2, wxSTC_MASK_FOLDERS);
    SetMarginWidth(2, 0);
    SetMarginSensitive(2, true);

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
    // === サクラエディタ デフォルト初期色 ===
    // sakura_core/doc/CDocTypeSetting.cpp の ColorInfoIni 定義より引用。
    // 2000.10.08 に「眩しさを押さえる」目的で 真っ白 → 薄ピンクに変更された
    // 伝統色 RGB(255, 251, 240) を採用する。
    static const wxColour kBgText      (255, 251, 240);  // テキスト背景
    static const wxColour kFgText      (  0,   0,   0);  // テキスト前景
    static const wxColour kBgRuler     (239, 239, 239);  // ルーラー／行番号 背景
    static const wxColour kFgLineNo    (  0,   0, 255);  // 行番号 (青)
    static const wxColour kCaretFg     (  0,   0,   0);  // キャレット
    static const wxColour kSelBg       ( 49, 106, 197);  // 選択範囲背景
    static const wxColour kSelFg       (255, 255, 255);  // 選択範囲文字色（白）
    static const wxColour kCaretLineBg (255, 255, 232);  // 現在行ハイライト
                                                          // (オリジナル CVL=透過なので
                                                          //  視認性のため淡い黄を使用)

    StyleSetBackground(wxSTC_STYLE_DEFAULT, kBgText);
    StyleSetForeground(wxSTC_STYLE_DEFAULT, kFgText);
    StyleClearAll();  // すべてのスタイルにデフォルト色を適用

    StyleSetBackground(wxSTC_STYLE_LINENUMBER, kBgRuler);
    StyleSetForeground(wxSTC_STYLE_LINENUMBER, kFgLineNo);

    // 折りたたみマージンの背景もテキスト背景に揃える
    SetFoldMarginColour(true, kBgText);
    SetFoldMarginHiColour(true, kBgRuler);

    SetCaretForeground(kCaretFg);
    SetCaretLineVisible(true);
    SetCaretLineBackground(kCaretLineBg);

    // 選択範囲色
    SetSelBackground(true, kSelBg);
    SetSelForeground(true, kSelFg);

    // 空白可視化色（オリジナル: 半角空白 RGB(192,192,192)）
    SetWhitespaceForeground(true, wxColour(192, 192, 192));
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

void CEditViewWx::SetWhitespaceVisible(bool visible) {
    m_showWhitespace = visible;
    SetViewWhiteSpace(visible ? wxSTC_WS_VISIBLEALWAYS : wxSTC_WS_INVISIBLE);
    SetWhitespaceForeground(true, wxColour(180, 180, 180));
}

void CEditViewWx::SetIdeographicSpaceHighlight(bool visible) {
    m_highlightIdeographic = visible;
    constexpr int kIdeoIndicator = 1;  // インジケータ #1 を全角空白用に確保
    // オリジナル STR_COLOR_FULL_SPACE = RGB(192, 192, 192) (薄灰)
    IndicatorSetStyle(kIdeoIndicator, wxSTC_INDIC_STRAIGHTBOX);
    IndicatorSetForeground(kIdeoIndicator, wxColour(192, 192, 192));
    IndicatorSetAlpha(kIdeoIndicator, 100);
    SetIndicatorCurrent(kIdeoIndicator);
    IndicatorClearRange(0, GetLength());
    if (!visible) return;

    wxString text = GetText();
    int pos = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        wxUniChar c = text[i];
        if (c == wxUniChar(0x3000)) {  // U+3000 IDEOGRAPHIC SPACE
            IndicatorFillRange(pos, 1);
        }
        // wxString は wchar_t なので 1 文字 = 1 STC バイト位置にはならないが、
        // wxSTC は内部 UTF-8 のため utf8 byte 単位の position が必要。
        // ここでは PositionFromLine + GetLineLength で行ベースに走査する近似実装でも
        // よいが、簡略化のため 1 文字進めごとに pos++ を utf8 換算しないと不正確。
        wxScopedCharBuffer buf = wxString(c).ToUTF8();
        pos += static_cast<int>(buf.length());
    }
}

void CEditViewWx::SetCodeFoldingEnabled(bool enabled) {
    m_foldingEnabled = enabled;
    if (enabled) {
        SetMarginWidth(2, 16);
        SetProperty(wxT("fold"), wxT("1"));
        SetProperty(wxT("fold.compact"), wxT("0"));
        SetProperty(wxT("fold.comment"), wxT("1"));
        SetProperty(wxT("fold.preprocessor"), wxT("1"));
        // 折りたたみマーカーのスタイル
        MarkerDefine(wxSTC_MARKNUM_FOLDER,        wxSTC_MARK_BOXPLUS);
        MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN,    wxSTC_MARK_BOXMINUS);
        MarkerDefine(wxSTC_MARKNUM_FOLDEREND,     wxSTC_MARK_BOXPLUSCONNECTED);
        MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER);
        MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUSCONNECTED);
        MarkerDefine(wxSTC_MARKNUM_FOLDERSUB,     wxSTC_MARK_VLINE);
        MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL,    wxSTC_MARK_LCORNER);
        for (int m = wxSTC_MARKNUM_FOLDEREND; m <= wxSTC_MARKNUM_FOLDEROPEN; ++m) {
            MarkerSetForeground(m, *wxWHITE);
            MarkerSetBackground(m, wxColour(120, 120, 120));
        }
        SetFoldFlags(wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);
    } else {
        SetMarginWidth(2, 0);
        SetProperty(wxT("fold"), wxT("0"));
    }
}

const char* CEditViewWx::EolName(EolMode m) {
    switch (m) {
    case EolMode::CRLF: return "CRLF";
    case EolMode::LF:   return "LF";
    case EolMode::CR:   return "CR";
    }
    return "?";
}

void CEditViewWx::SetEolMode(EolMode m) {
    m_eolMode = m;
    switch (m) {
    case EolMode::CRLF: wxStyledTextCtrl::SetEOLMode(wxSTC_EOL_CRLF); break;
    case EolMode::LF:   wxStyledTextCtrl::SetEOLMode(wxSTC_EOL_LF);   break;
    case EolMode::CR:   wxStyledTextCtrl::SetEOLMode(wxSTC_EOL_CR);   break;
    }
}

void CEditViewWx::ConvertAllLineEndingsTo(EolMode m) {
    SetEolMode(m);
    int sciMode = (m == EolMode::CRLF) ? wxSTC_EOL_CRLF
                : (m == EolMode::LF)   ? wxSTC_EOL_LF
                                       : wxSTC_EOL_CR;
    ConvertEOLs(sciMode);
}

void CEditViewWx::ToggleBookmarkAtCurrent() {
    int line = GetCurrentLine();
    if (MarkerGet(line) & (1 << kBookmarkMarker)) {
        MarkerDelete(line, kBookmarkMarker);
    } else {
        MarkerAdd(line, kBookmarkMarker);
    }
}

void CEditViewWx::GoToNextBookmark() {
    int from = GetCurrentLine() + 1;
    int found = MarkerNext(from, 1 << kBookmarkMarker);
    if (found < 0) {
        // 折り返し: 先頭から再探索
        found = MarkerNext(0, 1 << kBookmarkMarker);
    }
    if (found >= 0) {
        GotoLine(found);
        EnsureCaretVisible();
    }
}

void CEditViewWx::GoToPrevBookmark() {
    int from = GetCurrentLine() - 1;
    if (from < 0) from = GetLineCount() - 1;
    int found = MarkerPrevious(from, 1 << kBookmarkMarker);
    if (found < 0) {
        found = MarkerPrevious(GetLineCount() - 1, 1 << kBookmarkMarker);
    }
    if (found >= 0) {
        GotoLine(found);
        EnsureCaretVisible();
    }
}

void CEditViewWx::ClearAllBookmarks() {
    MarkerDeleteAll(kBookmarkMarker);
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

        // === サクラエディタ初期色 (CDocTypeSetting.cpp ColorInfoIni より) ===
        // STR_COLOR_COMMENT       RGB(  0, 128,   0)  緑
        // STR_COLOR_DOUBLE_QUOTE  RGB(128,   0,  64)  暗紫
        // STR_COLOR_SINGLE_QUOTE  RGB( 64, 128, 128)  暗青緑
        // STR_COLOR_KEYWORD1      RGB(  0,   0, 255)  青 (Bold)
        // STR_COLOR_KEYWORD2      RGB(255, 128,   0)  オレンジ (Bold) ← プリプロ等
        // STR_COLOR_NUMBER        RGB(235,   0,   0)  赤
        StyleSetForeground(wxSTC_C_COMMENT,      wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_C_COMMENTLINE,  wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_C_COMMENTDOC,   wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_C_STRING,       wxColour(128,   0,  64));
        StyleSetForeground(wxSTC_C_CHARACTER,    wxColour( 64, 128, 128));
        StyleSetForeground(wxSTC_C_WORD,         wxColour(  0,   0, 255));
        StyleSetForeground(wxSTC_C_NUMBER,       wxColour(235,   0,   0));
        StyleSetForeground(wxSTC_C_PREPROCESSOR, wxColour(255, 128,   0));
        StyleSetBold(wxSTC_C_WORD, true);
        StyleSetBold(wxSTC_C_PREPROCESSOR, true);

    } else if (ext == wxT("py")) {
        SetLexer(wxSTC_LEX_PYTHON);
        SetKeyWords(0, wxT(
            "False None True and as assert async await break class continue "
            "def del elif else except finally for from global if import in "
            "is lambda nonlocal not or pass raise return try while with yield"));
        // Python もサクラ初期色に統一
        StyleSetForeground(wxSTC_P_COMMENTLINE,  wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_P_COMMENTBLOCK, wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_P_STRING,       wxColour(128,   0,  64));
        StyleSetForeground(wxSTC_P_CHARACTER,    wxColour( 64, 128, 128));
        StyleSetForeground(wxSTC_P_TRIPLE,       wxColour(128,   0,  64));
        StyleSetForeground(wxSTC_P_TRIPLEDOUBLE, wxColour(128,   0,  64));
        StyleSetForeground(wxSTC_P_WORD,         wxColour(  0,   0, 255));
        StyleSetForeground(wxSTC_P_NUMBER,       wxColour(235,   0,   0));
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
