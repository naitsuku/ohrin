#include "CEditWndWx.h"
#include "view/CEditViewWx.h"
#include "dialog/CDlgSearchWx.h"

#include <wx/aboutdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/fontdlg.h>
#include <wx/msgdlg.h>

wxBEGIN_EVENT_TABLE(CEditWndWx, wxFrame)
    EVT_MENU(wxID_NEW,             CEditWndWx::OnNew)
    EVT_MENU(wxID_OPEN,            CEditWndWx::OnOpen)
    EVT_MENU(wxID_SAVE,            CEditWndWx::OnSave)
    EVT_MENU(wxID_SAVEAS,          CEditWndWx::OnSaveAs)
    EVT_MENU(wxID_CLOSE,           CEditWndWx::OnCloseFile)
    EVT_MENU(wxID_EXIT,            CEditWndWx::OnQuit)
    EVT_MENU(wxID_UNDO,            CEditWndWx::OnUndo)
    EVT_MENU(wxID_REDO,            CEditWndWx::OnRedo)
    EVT_MENU(wxID_CUT,             CEditWndWx::OnCut)
    EVT_MENU(wxID_COPY,            CEditWndWx::OnCopy)
    EVT_MENU(wxID_PASTE,           CEditWndWx::OnPaste)
    EVT_MENU(wxID_SELECTALL,       CEditWndWx::OnSelectAll)
    EVT_MENU(wxID_FIND,            CEditWndWx::OnFind)
    EVT_MENU(ID_VIEW_LINE_NUMBERS, CEditWndWx::OnToggleLineNumbers)
    EVT_MENU(ID_VIEW_WORD_WRAP,    CEditWndWx::OnToggleWordWrap)
    EVT_MENU(wxID_PREFERENCES,     CEditWndWx::OnFont)
    EVT_MENU(wxID_ABOUT,           CEditWndWx::OnAbout)
    EVT_CLOSE(CEditWndWx::OnCloseWindow)
    EVT_UPDATE_UI(wxID_SAVE,       CEditWndWx::OnUpdateUI)
    EVT_UPDATE_UI(wxID_UNDO,       CEditWndWx::OnUpdateUI)
    EVT_UPDATE_UI(wxID_REDO,       CEditWndWx::OnUpdateUI)
wxEND_EVENT_TABLE()

CEditWndWx::CEditWndWx(const wxString& filePath)
    : wxFrame(nullptr, wxID_ANY,
              wxT("サクラエディタ (wxWidgets)"),
              wxDefaultPosition, wxSize(900, 700))
{
    CreateMenuBarImpl();
    CreateStatusBarImpl();

    m_editView = new CEditViewWx(this, wxID_ANY);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_editView, 1, wxEXPAND);
    SetSizer(sizer);

    Centre();

    if (!filePath.IsEmpty()) {
        OpenFile(filePath);
    } else {
        UpdateTitle();
    }
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
    menuSearch->Append(wxID_FIND, wxT("検索(&F)...\tCtrl+F"));
    menuBar->Append(menuSearch, wxT("検索(&S)"));

    auto* menuView = new wxMenu();
    menuView->AppendCheckItem(ID_VIEW_LINE_NUMBERS, wxT("行番号を表示(&L)"));
    menuView->Check(ID_VIEW_LINE_NUMBERS, true);
    menuView->AppendCheckItem(ID_VIEW_WORD_WRAP,    wxT("折り返し(&W)"));
    menuView->Check(ID_VIEW_WORD_WRAP, false);
    menuView->AppendSeparator();
    menuView->Append(wxID_PREFERENCES, wxT("フォント設定(&F)..."));
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
    if (!m_editView->LoadFile(path)) {
        wxMessageBox(wxT("ファイルを開けませんでした: ") + path,
                     wxT("エラー"), wxOK | wxICON_ERROR);
        return false;
    }
    m_currentFile = path;
    m_modified = false;
    UpdateTitle();
    SetStatusText(path, kStatusFieldFile);
    return true;
}

bool CEditWndWx::SaveFile() {
    if (m_currentFile.IsEmpty()) return SaveFileAs();
    if (!m_editView->SaveFile(m_currentFile)) {
        wxMessageBox(wxT("保存に失敗しました: ") + m_currentFile,
                     wxT("エラー"), wxOK | wxICON_ERROR);
        return false;
    }
    m_modified = false;
    UpdateTitle();
    return true;
}

bool CEditWndWx::SaveFileAs() {
    wxFileDialog dlg(this,
        wxT("名前を付けて保存"),
        wxEmptyString,
        m_currentFile.IsEmpty() ? wxT("無題.txt") : wxFileName(m_currentFile).GetFullName(),
        wxT("テキストファイル (*.txt)|*.txt|すべてのファイル (*.*)|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return false;
    m_currentFile = dlg.GetPath();
    return SaveFile();
}

void CEditWndWx::UpdateTitle() {
    wxString name = m_currentFile.IsEmpty()
        ? wxString(wxT("無題"))
        : wxFileName(m_currentFile).GetFullName();
    if (m_modified) name = wxT("* ") + name;
    SetTitle(name + wxT(" - サクラエディタ (wxWidgets)"));
}

void CEditWndWx::NotifyModified(bool modified) {
    if (m_modified == modified) return;
    m_modified = modified;
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
    if (!MaybeSaveBeforeAction()) return;
    m_editView->ClearAll();
    m_editView->EmptyUndoBuffer();
    m_editView->SetSavePoint();
    m_currentFile.Clear();
    m_modified = false;
    UpdateTitle();
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

void CEditWndWx::OnCloseFile(wxCommandEvent& event) { OnNew(event); }
void CEditWndWx::OnQuit(wxCommandEvent&)            { Close(); }

void CEditWndWx::OnUndo(wxCommandEvent&)      { m_editView->Undo(); }
void CEditWndWx::OnRedo(wxCommandEvent&)      { m_editView->Redo(); }
void CEditWndWx::OnCut(wxCommandEvent&)       { m_editView->Cut(); }
void CEditWndWx::OnCopy(wxCommandEvent&)      { m_editView->Copy(); }
void CEditWndWx::OnPaste(wxCommandEvent&)     { m_editView->Paste(); }
void CEditWndWx::OnSelectAll(wxCommandEvent&) { m_editView->SelectAll(); }

void CEditWndWx::OnFind(wxCommandEvent&) {
    auto* dlg = new CDlgSearchWx(this, m_editView);
    dlg->Show(true);
}

void CEditWndWx::OnToggleLineNumbers(wxCommandEvent& event) {
    m_editView->SetLineNumbersVisible(event.IsChecked());
}

void CEditWndWx::OnToggleWordWrap(wxCommandEvent& event) {
    m_editView->SetWordWrapEnabled(event.IsChecked());
}

void CEditWndWx::OnFont(wxCommandEvent&) {
    wxFontData data;
    data.SetInitialFont(m_editView->GetCurrentEditorFont());
    wxFontDialog dlg(this, data);
    if (dlg.ShowModal() == wxID_OK) {
        m_editView->SetEditorFont(dlg.GetFontData().GetChosenFont());
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
