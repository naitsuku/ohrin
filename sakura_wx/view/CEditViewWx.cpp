#include "CEditViewWx.h"
#include "app/SakuraApp.h"
#include "config/SakuraConfig.h"
#include "window/CEditWndWx.h"

#include <wx/filename.h>

#include <fstream>

namespace charset = sakura_wx::charset;

namespace {
// 設定ストアにオーバーライドが定義されていればそれを返し、
// そうでなければデフォルト色を返す簡易ヘルパ。
wxColour PickSyntaxColor(const wxString& type, const wxString& kind,
                         const wxColour& fallback) {
    auto* app = wxDynamicCast(wxTheApp, SakuraApp);
    if (!app || !app->Config()) return fallback;
    wxColour c = app->Config()->GetSyntaxColor(type, kind);
    return c.IsOk() ? c : fallback;
}
} // namespace

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

    // 現在行アンダーライン用マーカー (STR_COLOR_CURSOR_LINE: RGB(0, 0, 255) 青)
    // wxSTC_MARK_UNDERLINE は line の下端に水平線を描く。
    MarkerDefine(kCurrentLineMarker, wxSTC_MARK_UNDERLINE);
    MarkerSetForeground(kCurrentLineMarker, wxColour(0, 0, 255));
    MarkerSetBackground(kCurrentLineMarker, wxColour(0, 0, 255));
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

    // 矩形選択 / 複数キャレット編集を有効化（Alt+ドラッグで矩形選択）
    EnableRectangularEditing();

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
    //
    // Phase 15a: m_darkMode が true なら VS Code Dark+ 風プリセットに切替。
    static const wxColour kBgText_L      (255, 251, 240);  // テキスト背景 (light)
    static const wxColour kFgText_L      (  0,   0,   0);
    static const wxColour kBgRuler_L     (239, 239, 239);
    static const wxColour kFgLineNo_L    (  0,   0, 255);
    static const wxColour kCaretFg_L     (  0,   0,   0);
    static const wxColour kSelBg_L       ( 49, 106, 197);
    static const wxColour kSelFg_L       (255, 255, 255);
    static const wxColour kCaretLineBg_L (255, 255, 232);

    static const wxColour kBgText_D      ( 30,  30,  30);  // テキスト背景 (dark)
    static const wxColour kFgText_D      (212, 212, 212);
    static const wxColour kBgRuler_D     ( 45,  45,  48);
    static const wxColour kFgLineNo_D    (133, 133, 133);
    static const wxColour kCaretFg_D     (255, 255, 255);
    static const wxColour kSelBg_D       ( 38,  79, 120);
    static const wxColour kSelFg_D       (212, 212, 212);
    static const wxColour kCaretLineBg_D ( 45,  45,  45);

    const bool dark = m_darkMode;
    const wxColour& kBgText      = dark ? kBgText_D      : kBgText_L;
    const wxColour& kFgText      = dark ? kFgText_D      : kFgText_L;
    const wxColour& kBgRuler     = dark ? kBgRuler_D     : kBgRuler_L;
    const wxColour& kFgLineNo    = dark ? kFgLineNo_D    : kFgLineNo_L;
    const wxColour& kCaretFg     = dark ? kCaretFg_D     : kCaretFg_L;
    const wxColour& kSelBg       = dark ? kSelBg_D       : kSelBg_L;
    const wxColour& kSelFg       = dark ? kSelFg_D       : kSelFg_L;
    const wxColour& kCaretLineBg = dark ? kCaretLineBg_D : kCaretLineBg_L;

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

    // 空白可視化色 (light: 薄灰、dark: やや明るい灰)
    SetWhitespaceForeground(true,
        dark ? wxColour(80, 80, 80) : wxColour(192, 192, 192));
}

void CEditViewWx::SetDarkMode(bool dark) {
    if (m_darkMode == dark) return;
    m_darkMode = dark;
    ApplyColorScheme();
    // 現在開いているファイルに応じてシンタックス色も再適用
    SetupSyntaxHighlighting(m_currentPath);
    Refresh();
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

    // ロード後に [EOF] アノテーションと現在行アンダーラインを再配置
    if (m_showEofMark) SetEofMarkVisible(true);
    if (m_currentLineUnderline) SetCurrentLineUnderline(true);
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

void CEditViewWx::SetCurrentLineUnderline(bool visible) {
    m_currentLineUnderline = visible;
    if (visible) {
        int line = GetCurrentLine();
        MarkerDeleteAll(kCurrentLineMarker);
        MarkerAdd(line, kCurrentLineMarker);
    } else {
        MarkerDeleteAll(kCurrentLineMarker);
    }
}

void CEditViewWx::SetEolVisible(bool visible) {
    m_showEol = visible;
    SetViewEOL(visible);
    // EOL マーク色: STR_COLOR_CR 相当 RGB(0, 128, 255) 水色
    // wxSTC は EOL 表示を独立スタイルとしては持たないので、近い水色背景が
    // 選ばれるよう EolForeground / Background は変更せず標準にする。
}

void CEditViewWx::EnableRectangularEditing() {
    // 矩形選択は wxSTC のネイティブ機能。Alt+ドラッグまたは Alt+矢印で開始。
    SetMultipleSelection(true);
    SetAdditionalSelectionTyping(true);
    SetMultiPaste(wxSTC_MULTIPASTE_EACH);
    SetVirtualSpaceOptions(wxSTC_VS_RECTANGULARSELECTION
                         | wxSTC_VS_USERACCESSIBLE);
}

void CEditViewWx::SetEofMarkVisible(bool visible) {
    m_showEofMark = visible;
    m_updatingEofMark = true;  // 再帰防止
    EOLAnnotationClearAll();
    if (visible) {
        EOLAnnotationSetVisible(wxSTC_EOLANNOTATION_STANDARD);
        int last = GetLineCount() - 1;
        if (last < 0) last = 0;
        EOLAnnotationSetText(last, "[EOF]");
    }
    m_updatingEofMark = false;
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
        // ライト: COMMENT 緑、STRING 暗紫、KEYWORD 青、NUMBER 赤
        // Phase 15a ダーク: VS Code Dark+ 風プリセット (m_darkMode 時)
        const bool dark = m_darkMode;
        const wxColour defComment = dark ? wxColour(106, 153,  85) : wxColour(  0, 128,   0);
        const wxColour defString  = dark ? wxColour(206, 145, 120) : wxColour(128,   0,  64);
        const wxColour defKeyword = dark ? wxColour( 86, 156, 214) : wxColour(  0,   0, 255);
        const wxColour defNumber  = dark ? wxColour(181, 206, 168) : wxColour(235,   0,   0);
        const wxColour defChar    = dark ? wxColour(206, 145, 120) : wxColour( 64, 128, 128);
        const wxColour defPrep    = dark ? wxColour(155, 155, 155) : wxColour(255, 128,   0);

        wxColour cComment = PickSyntaxColor(wxT("cpp"), wxT("comment"), defComment);
        wxColour cString  = PickSyntaxColor(wxT("cpp"), wxT("string"),  defString);
        wxColour cKeyword = PickSyntaxColor(wxT("cpp"), wxT("keyword"), defKeyword);
        wxColour cNumber  = PickSyntaxColor(wxT("cpp"), wxT("number"),  defNumber);

        StyleSetForeground(wxSTC_C_COMMENT,      cComment);
        StyleSetForeground(wxSTC_C_COMMENTLINE,  cComment);
        StyleSetForeground(wxSTC_C_COMMENTDOC,   cComment);
        StyleSetForeground(wxSTC_C_STRING,       cString);
        StyleSetForeground(wxSTC_C_CHARACTER,    defChar);
        StyleSetForeground(wxSTC_C_WORD,         cKeyword);
        StyleSetForeground(wxSTC_C_NUMBER,       cNumber);
        StyleSetForeground(wxSTC_C_PREPROCESSOR, defPrep);
        StyleSetBold(wxSTC_C_WORD, true);
        StyleSetBold(wxSTC_C_PREPROCESSOR, true);

    } else if (ext == wxT("py")) {
        SetLexer(wxSTC_LEX_PYTHON);
        SetKeyWords(0, wxT(
            "False None True and as assert async await break class continue "
            "def del elif else except finally for from global if import in "
            "is lambda nonlocal not or pass raise return try while with yield"));
        const bool dark = m_darkMode;
        const wxColour defComment = dark ? wxColour(106, 153,  85) : wxColour(  0, 128,   0);
        const wxColour defString  = dark ? wxColour(206, 145, 120) : wxColour(128,   0,  64);
        const wxColour defKeyword = dark ? wxColour( 86, 156, 214) : wxColour(  0,   0, 255);
        const wxColour defNumber  = dark ? wxColour(181, 206, 168) : wxColour(235,   0,   0);
        const wxColour defChar    = dark ? wxColour(206, 145, 120) : wxColour( 64, 128, 128);

        wxColour pyComment = PickSyntaxColor(wxT("py"), wxT("comment"), defComment);
        wxColour pyString  = PickSyntaxColor(wxT("py"), wxT("string"),  defString);
        wxColour pyKeyword = PickSyntaxColor(wxT("py"), wxT("keyword"), defKeyword);
        wxColour pyNumber  = PickSyntaxColor(wxT("py"), wxT("number"),  defNumber);

        StyleSetForeground(wxSTC_P_COMMENTLINE,  pyComment);
        StyleSetForeground(wxSTC_P_COMMENTBLOCK, pyComment);
        StyleSetForeground(wxSTC_P_STRING,       pyString);
        StyleSetForeground(wxSTC_P_CHARACTER,    defChar);
        StyleSetForeground(wxSTC_P_TRIPLE,       pyString);
        StyleSetForeground(wxSTC_P_TRIPLEDOUBLE, pyString);
        StyleSetForeground(wxSTC_P_WORD,         pyKeyword);
        StyleSetForeground(wxSTC_P_NUMBER,       pyNumber);
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
    const int mt = event.GetModificationType();
    const bool textChanged = (mt & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) != 0;

    // [EOF] 再配置 (再帰回避ガード付き)
    if (m_showEofMark && textChanged && !m_updatingEofMark) {
        m_updatingEofMark = true;
        int last = GetLineCount() - 1;
        if (last < 0) last = 0;
        EOLAnnotationClearAll();
        EOLAnnotationSetText(last, "[EOF]");
        m_updatingEofMark = false;
    }

    // アウトラインへの自動更新通知 (debounce タイマで実 Rebuild)
    if (textChanged) {
        if (auto* frame = GetParentFrame()) {
            frame->NotifyOutlineDirty();
        }
    }

    event.Skip();
}

void CEditViewWx::OnUpdateUI(wxStyledTextEvent& event) {
    UpdateStatusBarPos();

    // 現在行アンダーライン: カーソル行が変わるたびに再配置
    if (m_currentLineUnderline) {
        int line = GetCurrentLine();
        // 既に同じ行にあるか確認 (無駄な再描画防止)
        if (!(MarkerGet(line) & (1 << kCurrentLineMarker))) {
            MarkerDeleteAll(kCurrentLineMarker);
            MarkerAdd(line, kCurrentLineMarker);
        }
    }
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
