#include "CEditWndWx.h"
#include "CSakuraToolBar.h"
#include "CSakuraRuler.h"
#include "CSakuraOutline.h"
#include "CSakuraPrintout.h"
#include "view/CEditViewWx.h"

#include <wx/splitter.h>
#include <wx/printdlg.h>
#include <wx/print.h>
#include "dialog/CDlgSearchWx.h"
#include "dialog/CDlgGrepWx.h"
#include "dialog/CDlgSettingsWx.h"
#include "app/SakuraApp.h"
#include "config/SakuraConfig.h"

#include <wx/aboutdlg.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/fontdlg.h>
#include <wx/msgdlg.h>

namespace {
// FileDropTarget: ファイルドロップを受け取って CEditWndWx に転送
class SakuraFileDropTarget : public wxFileDropTarget {
public:
    explicit SakuraFileDropTarget(CEditWndWx* wnd) : m_wnd(wnd) {}
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override {
        if (!m_wnd || files.IsEmpty()) return false;
        m_wnd->OpenFile(files[0]);
        return true;
    }
private:
    CEditWndWx* m_wnd;
};
} // namespace

wxBEGIN_EVENT_TABLE(CEditWndWx, wxFrame)
    EVT_MENU(wxID_NEW,             CEditWndWx::OnNew)
    EVT_MENU(wxID_OPEN,            CEditWndWx::OnOpen)
    EVT_MENU(wxID_SAVE,            CEditWndWx::OnSave)
    EVT_MENU(wxID_SAVEAS,          CEditWndWx::OnSaveAs)
    EVT_MENU(wxID_CLOSE,           CEditWndWx::OnCloseFile)
    EVT_MENU(wxID_EXIT,            CEditWndWx::OnQuit)
    EVT_MENU(wxID_PRINT,           CEditWndWx::OnPrint)
    EVT_MENU(wxID_PREVIEW,         CEditWndWx::OnPrintPreview)
    EVT_MENU(wxID_PAGE_SETUP,      CEditWndWx::OnPageSetup)
    EVT_MENU(wxID_UNDO,            CEditWndWx::OnUndo)
    EVT_MENU(wxID_REDO,            CEditWndWx::OnRedo)
    EVT_MENU(wxID_CUT,             CEditWndWx::OnCut)
    EVT_MENU(wxID_COPY,            CEditWndWx::OnCopy)
    EVT_MENU(wxID_PASTE,           CEditWndWx::OnPaste)
    EVT_MENU(wxID_SELECTALL,       CEditWndWx::OnSelectAll)
    EVT_MENU(wxID_FIND,            CEditWndWx::OnFind)
    EVT_MENU(wxID_REPLACE,         CEditWndWx::OnReplace)
    EVT_MENU(ID_SEARCH_GREP,       CEditWndWx::OnGrep)
    EVT_MENU(ID_SEARCH_FIND_NEXT,    CEditWndWx::OnFindNext)
    EVT_MENU(ID_SEARCH_FIND_PREV,    CEditWndWx::OnFindPrev)
    EVT_MENU(ID_SEARCH_CLEAR_MARKS,  CEditWndWx::OnClearSearchMarks)
    EVT_MENU(ID_VIEW_LINE_NUMBERS, CEditWndWx::OnToggleLineNumbers)
    EVT_MENU(ID_VIEW_WORD_WRAP,    CEditWndWx::OnToggleWordWrap)
    EVT_MENU(ID_VIEW_WHITESPACE,         CEditWndWx::OnToggleWhitespace)
    EVT_MENU(ID_VIEW_IDEOGRAPHIC_SPACE,  CEditWndWx::OnToggleIdeographicSpace)
    EVT_MENU(ID_VIEW_CODE_FOLDING,       CEditWndWx::OnToggleCodeFolding)
    EVT_MENU(ID_VIEW_RULER,                  CEditWndWx::OnToggleRuler)
    EVT_MENU(ID_VIEW_CURRENT_LINE_UNDERLINE, CEditWndWx::OnToggleCurrentLineUnderline)
    EVT_MENU(ID_VIEW_EOF_MARK,               CEditWndWx::OnToggleEofMark)
    EVT_MENU(ID_VIEW_EOL_VISIBLE,            CEditWndWx::OnToggleEolVisible)
    EVT_MENU(ID_VIEW_OUTLINE,                CEditWndWx::OnToggleOutline)
    EVT_MENU(ID_VIEW_OUTLINE_REFRESH,        CEditWndWx::OnRefreshOutline)
    EVT_MENU(ID_BOOKMARK_TOGGLE,    CEditWndWx::OnBookmarkToggle)
    EVT_MENU(ID_BOOKMARK_NEXT,      CEditWndWx::OnBookmarkNext)
    EVT_MENU(ID_BOOKMARK_PREV,      CEditWndWx::OnBookmarkPrev)
    EVT_MENU(ID_BOOKMARK_CLEAR_ALL, CEditWndWx::OnBookmarkClearAll)
    EVT_MENU_RANGE(ID_ENCODING_BASE, ID_ENCODING_UTF16BE,
                   CEditWndWx::OnEncodingChange)
    EVT_MENU_RANGE(ID_ENCODING_RELOAD_BASE, ID_ENCODING_RELOAD_BASE + 5,
                   CEditWndWx::OnReloadAsEncoding)
    EVT_MENU_RANGE(ID_EOL_CRLF, ID_EOL_CR, CEditWndWx::OnEolChange)
    EVT_MENU_RANGE(ID_RECENT_FILE_BASE, ID_RECENT_FILE_LAST,
                   CEditWndWx::OnRecentFile)
    EVT_MENU(ID_RECENT_FILE_CLEAR, CEditWndWx::OnRecentFile)
    EVT_MENU(wxID_PREFERENCES,     CEditWndWx::OnFont)
    EVT_MENU(wxID_ABOUT,           CEditWndWx::OnAbout)
    EVT_CLOSE(CEditWndWx::OnCloseWindow)
    EVT_UPDATE_UI(wxID_SAVE,       CEditWndWx::OnUpdateUI)
    EVT_UPDATE_UI(wxID_UNDO,       CEditWndWx::OnUpdateUI)
    EVT_UPDATE_UI(wxID_REDO,       CEditWndWx::OnUpdateUI)
    EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, CEditWndWx::OnTabChanged)
    EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY,   CEditWndWx::OnTabClose)
wxEND_EVENT_TABLE()

CEditWndWx::CEditWndWx(const wxString& filePath)
    : wxFrame(nullptr, wxID_ANY,
              wxT("サクラエディタ (wxWidgets)"),
              wxDefaultPosition, wxSize(900, 700))
{
    CreateMenuBarImpl();
    CreateStatusBarImpl();

    // ツールバー: 通常の wxToolBar を子として作り、タイトル直下に配置。
    auto* toolBar = sakura_wx::ui::CSakuraToolBar::Build(this);

    // ルーラー: ツールバー直下、タブの上に配置。アクティブビューに追従。
    m_ruler = new sakura_wx::ui::CSakuraRuler(this);

    // 左にアウトライン、右に wxAuiNotebook の splitter 構成。
    m_splitter = new wxSplitterWindow(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxSP_3D | wxSP_LIVE_UPDATE);
    m_splitter->SetMinimumPaneSize(100);

    m_outline = new sakura_wx::ui::CSakuraOutline(m_splitter, this);

    m_notebook = new wxAuiNotebook(m_splitter, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_TAB_MOVE | wxAUI_NB_CLOSE_ON_ACTIVE_TAB
            | wxAUI_NB_WINDOWLIST_BUTTON);

    m_splitter->SplitVertically(m_outline, m_notebook, m_outlineSash);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    if (toolBar) sizer->Add(toolBar, 0, wxEXPAND);
    sizer->Add(m_ruler,    0, wxEXPAND);
    sizer->Add(m_splitter, 1, wxEXPAND);
    SetSizer(sizer);

    // 最初のタブを作成
    AddNewTab();

    // フレーム本体にもファイルドロップ
    SetDropTarget(new SakuraFileDropTarget(this));

    RefreshRecentFilesMenu();
    Centre();

    if (!filePath.IsEmpty()) {
        OpenFile(filePath);
    } else {
        UpdateTitle();
    }
}

// ============================================================
// タブ管理
// ============================================================
CEditViewWx* CEditWndWx::ActiveView() const {
    if (!m_notebook) return m_editView;
    int sel = m_notebook->GetSelection();
    if (sel == wxNOT_FOUND) return nullptr;
    return dynamic_cast<CEditViewWx*>(m_notebook->GetPage(sel));
}

CEditWndWx::TabState* CEditWndWx::ActiveState() {
    auto* v = ActiveView();
    if (!v) return nullptr;
    auto it = m_tabs.find(v);
    return it == m_tabs.end() ? nullptr : &it->second;
}

const CEditWndWx::TabState* CEditWndWx::ActiveState() const {
    auto* v = ActiveView();
    if (!v) return nullptr;
    auto it = m_tabs.find(v);
    return it == m_tabs.end() ? nullptr : &it->second;
}

void CEditWndWx::SyncMirrorFromActive() {
    m_editView = ActiveView();
    if (auto* st = ActiveState()) {
        m_currentFile = st->currentFile;
        m_modified    = st->modified;
    } else {
        m_currentFile.Clear();
        m_modified = false;
    }
    UpdateTitle();
    if (m_ruler) m_ruler->Attach(m_editView);
    if (m_outline) m_outline->Rebuild(m_editView);
}

int CEditWndWx::AddNewTab(const wxString& title) {
    auto* view = new CEditViewWx(m_notebook, wxID_ANY);
    view->SetDropTarget(new SakuraFileDropTarget(this));
    // デフォルトでメニュー初期チェック ON 項目を反映
    view->SetCurrentLineUnderline(true);
    view->SetEofMarkVisible(true);
    m_tabs[view] = TabState{};
    m_notebook->AddPage(view, title, /*select=*/true);
    SyncMirrorFromActive();
    return m_notebook->GetPageCount() - 1;
}

bool CEditWndWx::ActivateTabForFile(const wxString& path) {
    if (!m_notebook) return false;
    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
        auto* view = dynamic_cast<CEditViewWx*>(m_notebook->GetPage(i));
        if (!view) continue;
        auto it = m_tabs.find(view);
        if (it != m_tabs.end() && it->second.currentFile == path) {
            m_notebook->SetSelection(i);
            return true;
        }
    }
    return false;
}

void CEditWndWx::OnTabChanged(wxAuiNotebookEvent& event) {
    SyncMirrorFromActive();
    event.Skip();
}

void CEditWndWx::OnTabClose(wxAuiNotebookEvent& event) {
    int idx = event.GetSelection();
    auto* view = dynamic_cast<CEditViewWx*>(m_notebook->GetPage(idx));
    if (!view) { event.Skip(); return; }
    auto it = m_tabs.find(view);
    if (it != m_tabs.end() && it->second.modified) {
        // 一旦アクティブにして保存確認
        m_notebook->SetSelection(idx);
        SyncMirrorFromActive();
        if (!MaybeSaveBeforeAction()) {
            event.Veto();
            return;
        }
    }
    if (it != m_tabs.end()) m_tabs.erase(it);

    // 最後のタブが閉じられそうなら新規タブで補充
    if (m_notebook->GetPageCount() <= 1) {
        // close を許可した後、空にならないように後で 1 つ追加する。
        CallAfter([this]{
            if (m_notebook->GetPageCount() == 0) AddNewTab();
            SyncMirrorFromActive();
        });
    }
    event.Skip();
}

CEditWndWx::~CEditWndWx() = default;

void CEditWndWx::CreateMenuBarImpl() {
    auto* menuBar = new wxMenuBar();

    auto* menuFile = new wxMenu();
    menuFile->Append(wxID_NEW,    wxT("新規(&N)\tCtrl+N"));
    menuFile->Append(wxID_OPEN,   wxT("開く(&O)...\tCtrl+O"));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_SAVE,   wxT("上書き保存(&S)\tCtrl+S"));
    menuFile->Append(wxID_SAVEAS, wxT("名前を付けて保存(&A)..."));
    menuFile->AppendSeparator();

    // 最近使ったファイル サブメニュー（後で RefreshRecentFilesMenu で内容更新）
    m_recentMenu = new wxMenu();
    menuFile->AppendSubMenu(m_recentMenu, wxT("最近使ったファイル(&R)"));

    menuFile->AppendSeparator();
    menuFile->Append(wxID_PAGE_SETUP, wxT("ページ設定(&G)..."));
    menuFile->Append(wxID_PREVIEW,    wxT("印刷プレビュー(&V)..."));
    menuFile->Append(wxID_PRINT,      wxT("印刷(&P)...\tCtrl+P"));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_CLOSE,  wxT("閉じる(&W)\tCtrl+W"));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT,   wxT("終了(&X)"));
    menuBar->Append(menuFile, wxT("ファイル(&F)"));

    auto* menuEdit = new wxMenu();
    menuEdit->Append(wxID_UNDO,      wxT("元に戻す(&U)\tCtrl+Z"));
    menuEdit->Append(wxID_REDO,      wxT("やり直し(&R)\tCtrl+Shift+Z"));
    menuEdit->AppendSeparator();
    menuEdit->Append(wxID_CUT,       wxT("切り取り(&T)\tCtrl+X"));
    menuEdit->Append(wxID_COPY,      wxT("コピー(&C)\tCtrl+C"));
    menuEdit->Append(wxID_PASTE,     wxT("貼り付け(&P)\tCtrl+V"));
    menuEdit->AppendSeparator();
    menuEdit->Append(wxID_SELECTALL, wxT("すべて選択(&A)\tCtrl+A"));
    menuBar->Append(menuEdit, wxT("編集(&E)"));

    auto* menuSearch = new wxMenu();
    menuSearch->Append(wxID_FIND,             wxT("検索(&F)...\tCtrl+F"));
    menuSearch->Append(ID_SEARCH_FIND_NEXT,   wxT("次を検索(&N)\tF3"));
    menuSearch->Append(ID_SEARCH_FIND_PREV,   wxT("前を検索(&P)\tShift+F3"));
    menuSearch->Append(wxID_REPLACE,          wxT("置換(&R)...\tCtrl+H"));
    menuSearch->Append(ID_SEARCH_CLEAR_MARKS, wxT("検索マーククリア"));
    menuSearch->AppendSeparator();
    menuSearch->Append(ID_SEARCH_GREP,        wxT("Grep（フォルダ検索）(&G)...\tCtrl+G"));
    menuSearch->AppendSeparator();
    menuSearch->Append(ID_BOOKMARK_TOGGLE,    wxT("ブックマーク 切替\tCtrl+F2"));
    menuSearch->Append(ID_BOOKMARK_NEXT,      wxT("次のブックマーク\tF2"));
    menuSearch->Append(ID_BOOKMARK_PREV,      wxT("前のブックマーク\tShift+F2"));
    menuSearch->Append(ID_BOOKMARK_CLEAR_ALL, wxT("すべてのブックマークをクリア"));
    menuBar->Append(menuSearch, wxT("検索(&S)"));

    auto* menuView = new wxMenu();
    menuView->AppendCheckItem(ID_VIEW_LINE_NUMBERS, wxT("行番号を表示(&L)"));
    menuView->Check(ID_VIEW_LINE_NUMBERS, true);
    menuView->AppendCheckItem(ID_VIEW_RULER,
                              wxT("ルーラーを表示(&U)"));
    menuView->Check(ID_VIEW_RULER, true);
    menuView->AppendCheckItem(ID_VIEW_CURRENT_LINE_UNDERLINE,
                              wxT("編集中行のアンダーライン(&K)"));
    menuView->Check(ID_VIEW_CURRENT_LINE_UNDERLINE, true);
    menuView->AppendCheckItem(ID_VIEW_EOF_MARK,
                              wxT("[EOF] を表示(&Z)"));
    menuView->Check(ID_VIEW_EOF_MARK, true);
    menuView->AppendCheckItem(ID_VIEW_EOL_VISIBLE,
                              wxT("改行コードを表示(&O)"));
    menuView->AppendSeparator();
    menuView->AppendCheckItem(ID_VIEW_OUTLINE,
                              wxT("アウトラインを表示(&T)"));
    menuView->Check(ID_VIEW_OUTLINE, true);
    menuView->Append(ID_VIEW_OUTLINE_REFRESH,
                     wxT("アウトラインを更新\tF5"));
    menuView->AppendCheckItem(ID_VIEW_WORD_WRAP, wxT("折り返し(&W)"));
    menuView->AppendCheckItem(ID_VIEW_WHITESPACE,
                              wxT("空白・タブを可視化(&S)"));
    menuView->AppendCheckItem(ID_VIEW_IDEOGRAPHIC_SPACE,
                              wxT("全角空白をハイライト(&I)"));
    menuView->AppendCheckItem(ID_VIEW_CODE_FOLDING,
                              wxT("コード折りたたみ(&D)"));
    menuView->AppendSeparator();

    // 文字コード サブメニュー
    auto* menuEnc = new wxMenu();
    menuEnc->Append(ID_ENCODING_UTF8,     wxT("UTF-8 として保存"));
    menuEnc->Append(ID_ENCODING_UTF8_BOM, wxT("UTF-8 BOM として保存"));
    menuEnc->Append(ID_ENCODING_SJIS,     wxT("Shift-JIS として保存"));
    menuEnc->Append(ID_ENCODING_EUCJP,    wxT("EUC-JP として保存"));
    menuEnc->Append(ID_ENCODING_UTF16LE,  wxT("UTF-16 LE として保存"));
    menuEnc->Append(ID_ENCODING_UTF16BE,  wxT("UTF-16 BE として保存"));
    menuEnc->AppendSeparator();
    menuEnc->Append(ID_ENCODING_RELOAD_BASE + 0, wxT("UTF-8 として再読み込み"));
    menuEnc->Append(ID_ENCODING_RELOAD_BASE + 1, wxT("UTF-8 BOM として再読み込み"));
    menuEnc->Append(ID_ENCODING_RELOAD_BASE + 2, wxT("Shift-JIS として再読み込み"));
    menuEnc->Append(ID_ENCODING_RELOAD_BASE + 3, wxT("EUC-JP として再読み込み"));
    menuEnc->Append(ID_ENCODING_RELOAD_BASE + 4, wxT("UTF-16 LE として再読み込み"));
    menuEnc->Append(ID_ENCODING_RELOAD_BASE + 5, wxT("UTF-16 BE として再読み込み"));
    menuView->AppendSubMenu(menuEnc, wxT("文字コード(&C)"));

    // 改行コード サブメニュー
    auto* menuEol = new wxMenu();
    menuEol->Append(ID_EOL_CRLF, wxT("CRLF (Windows)"));
    menuEol->Append(ID_EOL_LF,   wxT("LF (macOS / Linux)"));
    menuEol->Append(ID_EOL_CR,   wxT("CR (旧 Mac)"));
    menuView->AppendSubMenu(menuEol, wxT("改行コード(&E)"));

    menuView->AppendSeparator();
    menuView->Append(wxID_PREFERENCES, wxT("設定(&P)..."));
    menuBar->Append(menuView, wxT("表示(&V)"));

    auto* menuHelp = new wxMenu();
    menuHelp->Append(wxID_ABOUT, wxT("バージョン情報(&A)..."));
    menuBar->Append(menuHelp, wxT("ヘルプ(&H)"));

    SetMenuBar(menuBar);
}

void CEditWndWx::CreateStatusBarImpl() {
    auto* sb = wxFrame::CreateStatusBar(kStatusFieldCount);
    int widths[kStatusFieldCount] = {-3, -1, 100};
    sb->SetStatusWidths(kStatusFieldCount, widths);
    SetStatusText(wxT("Ready"), kStatusFieldFile);
    SetStatusText(wxT(""), kStatusFieldCursor);
    SetStatusText(wxT("UTF-8"), kStatusFieldEncoding);
}

bool CEditWndWx::OpenFile(const wxString& path) {
    if (!wxFileName::FileExists(path)) {
        wxMessageBox(wxT("ファイルが見つかりません: ") + path,
                     wxT("エラー"), wxOK | wxICON_ERROR);
        return false;
    }
    // 既に開いているタブがあればそちらをアクティブ化
    if (ActivateTabForFile(path)) return true;

    // アクティブタブが「無題かつ未編集」なら再利用、そうでなければ新規タブ
    auto* st = ActiveState();
    bool reuse = (st && st->currentFile.IsEmpty() && !st->modified);
    if (!reuse) AddNewTab();

    auto* view = ActiveView();
    if (!view || !view->LoadFile(path)) {
        wxMessageBox(wxT("ファイルを開けませんでした: ") + path,
                     wxT("エラー"), wxOK | wxICON_ERROR);
        return false;
    }
    if (auto* s = ActiveState()) {
        s->currentFile = path;
        s->modified    = false;
    }
    SyncMirrorFromActive();

    // タブタイトルをファイル名に
    int sel = m_notebook->GetSelection();
    if (sel != wxNOT_FOUND) {
        m_notebook->SetPageText(sel, wxFileName(path).GetFullName());
    }
    SetStatusText(path, kStatusFieldFile);

    if (auto* app = wxDynamicCast(wxTheApp, SakuraApp)) {
        if (auto* cfg = app->Config()) {
            cfg->AddRecentFile(path);
            cfg->Save();
            RefreshRecentFilesMenu();
        }
    }
    return true;
}

bool CEditWndWx::SaveFile() {
    auto* view = ActiveView();
    auto* st   = ActiveState();
    if (!view || !st) return false;
    if (st->currentFile.IsEmpty()) return SaveFileAs();
    if (!view->SaveFile(st->currentFile)) {
        wxMessageBox(wxT("保存に失敗しました: ") + st->currentFile,
                     wxT("エラー"), wxOK | wxICON_ERROR);
        return false;
    }
    st->modified = false;
    SyncMirrorFromActive();
    return true;
}

bool CEditWndWx::SaveFileAs() {
    auto* st = ActiveState();
    if (!st) return false;
    wxFileDialog dlg(this,
        wxT("名前を付けて保存"),
        wxEmptyString,
        st->currentFile.IsEmpty() ? wxT("無題.txt")
                                  : wxFileName(st->currentFile).GetFullName(),
        wxT("テキストファイル (*.txt)|*.txt|すべてのファイル (*.*)|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return false;
    st->currentFile = dlg.GetPath();
    int sel = m_notebook ? m_notebook->GetSelection() : wxNOT_FOUND;
    if (sel != wxNOT_FOUND) {
        m_notebook->SetPageText(sel, wxFileName(st->currentFile).GetFullName());
    }
    return SaveFile();
}

void CEditWndWx::UpdateTitle() {
    wxString name = m_currentFile.IsEmpty()
        ? wxString(wxT("無題"))
        : wxFileName(m_currentFile).GetFullName();
    if (m_modified) name = wxT("* ") + name;
    SetTitle(name + wxT(" - サクラエディタ (wxWidgets)"));
}

bool CEditWndWx::OpenFileAtLine(const wxString& path, int line1Based) {
    if (!OpenFile(path)) return false;
    if (!m_editView) return false;
    if (line1Based <= 0) return true;
    int target = line1Based - 1;  // wxSTC は 0-based
    m_editView->GotoLine(target);
    m_editView->EnsureCaretVisible();
    m_editView->SetFocus();
    return true;
}

void CEditWndWx::NotifyModified(bool modified) {
    auto* st = ActiveState();
    if (st) st->modified = modified;
    if (m_modified == modified) return;
    m_modified = modified;
    // タブタイトルに * を反映
    int sel = m_notebook ? m_notebook->GetSelection() : wxNOT_FOUND;
    if (sel != wxNOT_FOUND && st) {
        wxString base = st->currentFile.IsEmpty()
            ? wxString(wxT("無題"))
            : wxFileName(st->currentFile).GetFullName();
        m_notebook->SetPageText(sel, modified ? wxT("* ") + base : base);
    }
    UpdateTitle();
}

bool CEditWndWx::MaybeSaveBeforeAction() {
    if (!m_modified) return true;
    int ret = wxMessageBox(
        wxT("変更が保存されていません。保存しますか?"),
        wxT("確認"), wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (ret == wxCANCEL) return false;
    if (ret == wxYES)    return SaveFile();
    return true;
}

void CEditWndWx::OnNew(wxCommandEvent&) {
    // 新規タブを開く（既存タブはそのまま残す）
    AddNewTab();
    SetStatusText(wxT("Ready"), kStatusFieldFile);
}

void CEditWndWx::OnOpen(wxCommandEvent&) {
    if (!MaybeSaveBeforeAction()) return;
    wxFileDialog dlg(this,
        wxT("ファイルを開く"),
        wxEmptyString, wxEmptyString,
        wxT("テキストファイル (*.txt;*.md;*.cpp;*.h)|*.txt;*.md;*.cpp;*.h|すべてのファイル (*.*)|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        OpenFile(dlg.GetPath());
    }
}

void CEditWndWx::OnSave(wxCommandEvent&)   { SaveFile(); }
void CEditWndWx::OnSaveAs(wxCommandEvent&) { SaveFileAs(); }

// ============================================================
// 印刷 (Phase 8)
// ============================================================
namespace {
// アプリ全体で 1 つの wxPrintData / wxPageSetupDialogData を保持。
wxPrintData& GlobalPrintData() {
    static wxPrintData s_data;
    return s_data;
}
wxPageSetupDialogData& GlobalPageSetupData() {
    static wxPageSetupDialogData s_data;
    return s_data;
}

wxString DisplayNameForPrint(const wxString& currentFile) {
    return currentFile.IsEmpty() ? wxString(wxT("(無題)"))
                                 : wxFileName(currentFile).GetFullName();
}
} // namespace

void CEditWndWx::OnPageSetup(wxCommandEvent&) {
    auto& psd = GlobalPageSetupData();
    psd.SetPrintData(GlobalPrintData());
    wxPageSetupDialog dlg(this, &psd);
    if (dlg.ShowModal() == wxID_OK) {
        GlobalPrintData() = dlg.GetPageSetupDialogData().GetPrintData();
        GlobalPageSetupData() = dlg.GetPageSetupDialogData();
    }
}

void CEditWndWx::OnPrintPreview(wxCommandEvent&) {
    if (!m_editView) return;
    wxString name = DisplayNameForPrint(m_currentFile);

    auto* printPreview = new wxPrintPreview(
        new sakura_wx::ui::CSakuraPrintout(m_editView, name),
        new sakura_wx::ui::CSakuraPrintout(m_editView, name),
        &GlobalPrintData());
    if (!printPreview->IsOk()) {
        delete printPreview;
        wxMessageBox(wxT("印刷プレビューの初期化に失敗しました"),
                     wxT("エラー"), wxOK | wxICON_ERROR, this);
        return;
    }
    auto* frame = new wxPreviewFrame(printPreview, this,
                                     wxT("印刷プレビュー"));
    frame->Centre(wxBOTH);
    frame->Initialize();
    frame->Show();
}

void CEditWndWx::OnPrint(wxCommandEvent&) {
    if (!m_editView) return;
    wxString name = DisplayNameForPrint(m_currentFile);

    wxPrintDialogData dlgData(GlobalPrintData());
    wxPrinter printer(&dlgData);
    sakura_wx::ui::CSakuraPrintout printout(m_editView, name);
    if (!printer.Print(this, &printout, /*prompt=*/true)) {
        if (wxPrinter::GetLastError() == wxPRINTER_ERROR) {
            wxMessageBox(wxT("印刷に失敗しました"),
                         wxT("エラー"), wxOK | wxICON_ERROR, this);
        }
    } else {
        GlobalPrintData() = printer.GetPrintDialogData().GetPrintData();
    }
}

void CEditWndWx::OnCloseFile(wxCommandEvent&) {
    if (!m_notebook) return;
    int sel = m_notebook->GetSelection();
    if (sel == wxNOT_FOUND) return;
    // wxAuiNotebook::DeletePage は内部で PAGE_CLOSE イベントを発火する
    m_notebook->DeletePage(sel);
    if (m_notebook->GetPageCount() == 0) {
        AddNewTab();
    }
    SyncMirrorFromActive();
}
void CEditWndWx::OnQuit(wxCommandEvent&)            { Close(); }

void CEditWndWx::OnUndo(wxCommandEvent&)      { m_editView->Undo(); }
void CEditWndWx::OnRedo(wxCommandEvent&)      { m_editView->Redo(); }
void CEditWndWx::OnCut(wxCommandEvent&)       { m_editView->Cut(); }
void CEditWndWx::OnCopy(wxCommandEvent&)      { m_editView->Copy(); }
void CEditWndWx::OnPaste(wxCommandEvent&)     { m_editView->Paste(); }
void CEditWndWx::OnSelectAll(wxCommandEvent&) { m_editView->SelectAll(); }

void CEditWndWx::OnFind(wxCommandEvent&) {
    auto* dlg = new CDlgSearchWx(this, m_editView, /*replaceFocus=*/false);
    dlg->Show(true);
}

void CEditWndWx::OnReplace(wxCommandEvent&) {
    auto* dlg = new CDlgSearchWx(this, m_editView, /*replaceFocus=*/true);
    dlg->Show(true);
}

void CEditWndWx::OnGrep(wxCommandEvent&) {
    auto* dlg = new sakura_wx::dialog::CDlgGrepWx(this, this);
    dlg->Show(true);
}

namespace {
// 検索 1 回分の内部関数。CDlgSearchWx::DoFind と同じロジックの軽量版。
bool RepeatSearchOnce(CEditViewWx* ed, const wxString& needle, int flags, bool forward) {
    if (!ed || needle.IsEmpty()) return false;
    int start, end;
    if (forward) {
        start = ed->GetCurrentPos();
        end   = ed->GetLength();
    } else {
        start = ed->GetCurrentPos() - 1;
        if (start < 0) start = 0;
        end = 0;
    }
    ed->SetTargetStart(start);
    ed->SetTargetEnd(end);
    ed->SetSearchFlags(flags);
    int found = ed->SearchInTarget(needle);
    if (found < 0) {
        // 折り返し検索
        ed->SetTargetStart(forward ? 0 : ed->GetLength());
        ed->SetTargetEnd(forward ? ed->GetLength() : 0);
        found = ed->SearchInTarget(needle);
        if (found < 0) return false;
    }
    int matchStart = ed->GetTargetStart();
    int matchEnd   = ed->GetTargetEnd();
    ed->SetSelection(matchStart, matchEnd);
    ed->EnsureCaretVisible();
    return true;
}
} // namespace

void CEditWndWx::OnFindNext(wxCommandEvent&) {
    if (m_lastSearchNeedle.IsEmpty()) {
        // まだ検索していなければ検索ダイアログを開く
        wxCommandEvent dummy;
        OnFind(dummy);
        return;
    }
    if (!RepeatSearchOnce(m_editView, m_lastSearchNeedle,
                          m_lastSearchFlags, /*forward=*/true)) {
        SetStatusText(wxT("見つかりません"), kStatusFieldFile);
    }
}

void CEditWndWx::OnFindPrev(wxCommandEvent&) {
    if (m_lastSearchNeedle.IsEmpty()) {
        wxCommandEvent dummy;
        OnFind(dummy);
        return;
    }
    if (!RepeatSearchOnce(m_editView, m_lastSearchNeedle,
                          m_lastSearchFlags, /*forward=*/false)) {
        SetStatusText(wxT("見つかりません"), kStatusFieldFile);
    }
}

void CEditWndWx::OnClearSearchMarks(wxCommandEvent&) {
    if (!m_editView) return;
    constexpr int kIndicator = 0;  // CDlgSearchWx::OnHighlightAll が使うのと同じ #0
    m_editView->SetIndicatorCurrent(kIndicator);
    m_editView->IndicatorClearRange(0, m_editView->GetLength());
    SetStatusText(wxT("検索ハイライトをクリアしました"), kStatusFieldFile);
}

void CEditWndWx::OnToggleLineNumbers(wxCommandEvent& event) {
    m_editView->SetLineNumbersVisible(event.IsChecked());
}

void CEditWndWx::OnToggleWordWrap(wxCommandEvent& event) {
    m_editView->SetWordWrapEnabled(event.IsChecked());
}

void CEditWndWx::OnFont(wxCommandEvent&) {
    auto* app = wxDynamicCast(wxTheApp, SakuraApp);
    SakuraConfig* cfg = app ? app->Config() : nullptr;
    sakura_wx::dialog::CDlgSettingsWx dlg(this, cfg);
    if (dlg.ShowModal() == wxID_OK) {
        // 即時反映: 現在のアクティブビューに新しいフォント／設定を当てる
        if (m_editView && cfg) {
            m_editView->SetEditorFont(cfg->GetEditorFont());
            m_editView->SetLineNumbersVisible(cfg->GetShowLineNumbers());
            m_editView->SetWordWrapEnabled(cfg->GetWordWrap());
            m_editView->SetTabWidth(cfg->GetTabWidth());
        }
    }
}

void CEditWndWx::OnAbout(wxCommandEvent&) {
    wxAboutDialogInfo info;
    info.SetName(wxT("サクラエディタ (wxWidgets ポート)"));
    info.SetVersion(wxT("0.1.0 — Phase 2 prototype"));
    info.SetDescription(wxT("Win32 専用テキストエディタ サクラエディタの\n"
                            "wxWidgets による macOS / Linux 移植版です。"));
    info.SetCopyright(wxT("(C) 1998-2026 Sakura Editor Organization, zlib license"));
    info.SetWebSite(wxT("https://github.com/sakura-editor/sakura"));
    wxAboutBox(info, this);
}

void CEditWndWx::OnCloseWindow(wxCloseEvent& event) {
    if (!MaybeSaveBeforeAction()) {
        event.Veto();
        return;
    }
    event.Skip();
}

void CEditWndWx::OnUpdateUI(wxUpdateUIEvent& event) {
    switch (event.GetId()) {
    case wxID_SAVE: event.Enable(m_modified || m_currentFile.IsEmpty()); break;
    case wxID_UNDO: event.Enable(m_editView && m_editView->CanUndo()); break;
    case wxID_REDO: event.Enable(m_editView && m_editView->CanRedo()); break;
    default: event.Skip(); break;
    }
}

// ============================================================
// Phase 7: 表示の高度な切替
// ============================================================
void CEditWndWx::OnToggleWhitespace(wxCommandEvent& event) {
    if (m_editView) m_editView->SetWhitespaceVisible(event.IsChecked());
}
void CEditWndWx::OnToggleIdeographicSpace(wxCommandEvent& event) {
    if (m_editView) m_editView->SetIdeographicSpaceHighlight(event.IsChecked());
}
void CEditWndWx::OnToggleCodeFolding(wxCommandEvent& event) {
    if (m_editView) m_editView->SetCodeFoldingEnabled(event.IsChecked());
}
void CEditWndWx::OnToggleRuler(wxCommandEvent& event) {
    if (m_ruler) m_ruler->SetVisibleByMenu(event.IsChecked());
}
void CEditWndWx::OnToggleCurrentLineUnderline(wxCommandEvent& event) {
    if (m_editView) m_editView->SetCurrentLineUnderline(event.IsChecked());
}
void CEditWndWx::OnToggleEofMark(wxCommandEvent& event) {
    if (m_editView) m_editView->SetEofMarkVisible(event.IsChecked());
}
void CEditWndWx::OnToggleEolVisible(wxCommandEvent& event) {
    if (m_editView) m_editView->SetEolVisible(event.IsChecked());
}
void CEditWndWx::OnToggleOutline(wxCommandEvent& event) {
    if (!m_splitter) return;
    if (event.IsChecked()) {
        if (!m_splitter->IsSplit()) {
            m_splitter->SplitVertically(m_outline, m_notebook, m_outlineSash);
        }
        if (m_outline) m_outline->Rebuild(m_editView);
    } else {
        if (m_splitter->IsSplit()) {
            m_outlineSash = m_splitter->GetSashPosition();
            m_splitter->Unsplit(m_outline);
        }
    }
}
void CEditWndWx::OnRefreshOutline(wxCommandEvent&) {
    if (m_outline) m_outline->Rebuild(m_editView);
}

// ============================================================
// Phase 7: ブックマーク
// ============================================================
void CEditWndWx::OnBookmarkToggle(wxCommandEvent&) {
    if (m_editView) m_editView->ToggleBookmarkAtCurrent();
}
void CEditWndWx::OnBookmarkNext(wxCommandEvent&) {
    if (m_editView) m_editView->GoToNextBookmark();
}
void CEditWndWx::OnBookmarkPrev(wxCommandEvent&) {
    if (m_editView) m_editView->GoToPrevBookmark();
}
void CEditWndWx::OnBookmarkClearAll(wxCommandEvent&) {
    if (m_editView) m_editView->ClearAllBookmarks();
}

// ============================================================
// Phase 7: 文字コード／改行コード
// ============================================================
namespace {
sakura_wx::charset::Encoding EncIdToEnum(int id, int base) {
    using E = sakura_wx::charset::Encoding;
    switch (id - base) {
    case 0: return E::UTF8;
    case 1: return E::UTF8_BOM;
    case 2: return E::SJIS;
    case 3: return E::EUCJP;
    case 4: return E::UTF16LE;
    case 5: return E::UTF16BE;
    default: return E::UTF8;
    }
}
} // namespace

void CEditWndWx::OnEncodingChange(wxCommandEvent& event) {
    if (!m_editView) return;
    auto enc = EncIdToEnum(event.GetId(), ID_ENCODING_BASE);
    m_editView->SetEncoding(enc);
    SetStatusText(wxString::FromUTF8(sakura_wx::charset::EncodingName(enc)),
                  kStatusFieldEncoding);
    NotifyModified(true); // 保存時に書き出すため変更扱い
}

void CEditWndWx::OnReloadAsEncoding(wxCommandEvent& event) {
    if (!m_editView || m_currentFile.IsEmpty()) return;
    if (!MaybeSaveBeforeAction()) return;
    auto enc = EncIdToEnum(event.GetId(), ID_ENCODING_RELOAD_BASE);
    // 単純実装: 一旦ファイルを開き直してから encoding を上書き
    m_editView->SetEncoding(enc);
    m_editView->LoadFile(m_currentFile);
    m_editView->SetEncoding(enc);  // 自動判定上書き
    NotifyModified(false);
}

void CEditWndWx::OnEolChange(wxCommandEvent& event) {
    if (!m_editView) return;
    using EM = CEditViewWx::EolMode;
    EM mode = EM::LF;
    switch (event.GetId()) {
    case ID_EOL_CRLF: mode = EM::CRLF; break;
    case ID_EOL_LF:   mode = EM::LF;   break;
    case ID_EOL_CR:   mode = EM::CR;   break;
    }
    m_editView->ConvertAllLineEndingsTo(mode);
    NotifyModified(true);
}

// ============================================================
// Phase 7: 最近使ったファイル
// ============================================================
void CEditWndWx::RefreshRecentFilesMenu() {
    if (!m_recentMenu) return;
    // 既存項目をすべて削除
    while (m_recentMenu->GetMenuItemCount() > 0) {
        auto* it = m_recentMenu->FindItemByPosition(0);
        if (!it) break;
        m_recentMenu->Destroy(it);
    }
    auto* app = wxDynamicCast(wxTheApp, SakuraApp);
    if (!app || !app->Config()) {
        m_recentMenu->Append(wxID_ANY, wxT("(なし)"))->Enable(false);
        return;
    }
    auto files = app->Config()->GetRecentFiles();
    if (files.IsEmpty()) {
        m_recentMenu->Append(wxID_ANY, wxT("(なし)"))->Enable(false);
        return;
    }
    int n = static_cast<int>(files.GetCount());
    if (n > (ID_RECENT_FILE_LAST - ID_RECENT_FILE_BASE + 1)) {
        n = (ID_RECENT_FILE_LAST - ID_RECENT_FILE_BASE + 1);
    }
    for (int i = 0; i < n; ++i) {
        wxString label = wxString::Format(wxT("&%d  %s"), (i + 1) % 10, files[i]);
        m_recentMenu->Append(ID_RECENT_FILE_BASE + i, label);
    }
    m_recentMenu->AppendSeparator();
    m_recentMenu->Append(ID_RECENT_FILE_CLEAR, wxT("履歴をクリア"));
}

void CEditWndWx::OnRecentFile(wxCommandEvent& event) {
    auto* app = wxDynamicCast(wxTheApp, SakuraApp);
    if (!app || !app->Config()) return;

    if (event.GetId() == ID_RECENT_FILE_CLEAR) {
        app->Config()->ClearRecentFiles();
        app->Config()->Save();
        RefreshRecentFilesMenu();
        return;
    }

    int idx = event.GetId() - ID_RECENT_FILE_BASE;
    auto files = app->Config()->GetRecentFiles();
    if (idx < 0 || idx >= static_cast<int>(files.GetCount())) return;
    if (!MaybeSaveBeforeAction()) return;
    OpenFile(files[idx]);
}
