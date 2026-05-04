# Phase 2: wxWidgets フロントエンド骨格の実装

## 前提条件
- Phase 1 が完了していること
- `cmake --build build` が macOS/Linux でエラーなく完了していること

## このフェーズのゴール
1. `sakura_wx/` アプリケーションが起動してウィンドウを表示できる
2. ファイルの読み込みとテキスト表示ができる
3. 日本語テキストが正しく表示される（IME 入力は Phase 4 で対応）
4. ファイル保存ができる

**重要**: このフェーズは「動く最小構成」を優先する。
機能の完全性より「クラッシュしない・文字化けしない」を優先すること。

---

## Task 2-1: sakura_wx/ ディレクトリ構成の作成

```
sakura_wx/
├── CMakeLists.txt
├── app/
│   ├── SakuraApp.h
│   └── SakuraApp.cpp
├── window/
│   ├── CEditWndWx.h
│   └── CEditWndWx.cpp
├── view/
│   ├── CEditViewWx.h
│   └── CEditViewWx.cpp
├── dialog/
│   ├── CDlgSearchWx.h
│   └── CDlgSearchWx.cpp
└── resource/
    ├── sakura_wx.xrc    (wxWidgets リソース - 後で使用)
    └── app_icon.png
```

`sakura_wx/CMakeLists.txt` を作成せよ：

```cmake
cmake_minimum_required(VERSION 3.20)

# ソースファイル
set(SAKURA_WX_SOURCES
    app/SakuraApp.cpp
    window/CEditWndWx.cpp
    view/CEditViewWx.cpp
    dialog/CDlgSearchWx.cpp
)

# 実行ファイル
if(APPLE)
    add_executable(sakura_wx MACOSX_BUNDLE ${SAKURA_WX_SOURCES})
    set_target_properties(sakura_wx PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "jp.sourceforge.sakura-editor"
        MACOSX_BUNDLE_BUNDLE_NAME "SakuraEditor"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    )
else()
    add_executable(sakura_wx ${SAKURA_WX_SOURCES})
endif()

target_include_directories(sakura_wx PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/sakura_core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sakura_wx PRIVATE
    sakura_core
    ${wxWidgets_LIBRARIES}
)

target_compile_options(sakura_wx PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)
```

---

## Task 2-2: アプリケーションエントリポイント

### `sakura_wx/app/SakuraApp.h`

```cpp
#pragma once
#include <wx/wx.h>

/**
 * SakuraApp - wxWidgets アプリケーションクラス
 * 
 * wxApp のサブクラス。アプリケーションの初期化と
 * メインウィンドウの生成を担当する。
 */
class SakuraApp : public wxApp {
public:
    bool OnInit() override;
    int  OnExit() override;

private:
    /**
     * コマンドライン引数の解析
     * @return 起動時に開くファイルパス（なければ空）
     */
    wxString ParseCommandLine();
};

wxDECLARE_APP(SakuraApp);
```

### `sakura_wx/app/SakuraApp.cpp`

```cpp
#include "SakuraApp.h"
#include "window/CEditWndWx.h"
#include <wx/filename.h>
#include <wx/image.h>

wxIMPLEMENT_APP(SakuraApp);

bool SakuraApp::OnInit() {
    // wxWidgets の初期化
    if (!wxApp::OnInit()) return false;

    // 画像ハンドラの登録（アイコン等に使用）
    wxInitAllImageHandlers();

    // ロケールの設定（日本語対応）
    wxLocale* locale = new wxLocale(wxLANGUAGE_JAPANESE, wxLOCALE_DONT_LOAD_DEFAULT);
    if (!locale->IsOk()) {
        // 日本語ロケールが使えなくてもデフォルトで続行
        delete locale;
        locale = nullptr;
    }

    // 起動時ファイルの確認
    wxString startupFile = ParseCommandLine();

    // メインウィンドウの作成と表示
    auto* frame = new CEditWndWx(startupFile);
    frame->Show(true);
    SetTopWindow(frame);

    return true;
}

int SakuraApp::OnExit() {
    return wxApp::OnExit();
}

wxString SakuraApp::ParseCommandLine() {
    // コマンドライン引数にファイルパスが指定されていれば返す
    if (argc >= 2) {
        wxString arg = argv[1];
        if (wxFileName::FileExists(arg)) {
            return arg;
        }
    }
    return wxEmptyString;
}
```

---

## Task 2-3: メインウィンドウ（CEditWndWx）

### `sakura_wx/window/CEditWndWx.h`

```cpp
#pragma once
#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <memory>

class CEditViewWx;

/**
 * CEditWndWx - サクラエディタ メインウィンドウ（wxWidgets版）
 *
 * Win32版の CEditWnd に相当するが、wxFrame ベースで独立実装。
 * CEditDoc / CEditView の wxWidgets 版インスタンスを管理する。
 */
class CEditWndWx : public wxFrame {
public:
    explicit CEditWndWx(const wxString& filePath = wxEmptyString);
    ~CEditWndWx() override;

    // --- ファイル操作 ---
    bool OpenFile(const wxString& path);
    bool SaveFile();
    bool SaveFileAs();

    // --- ウィンドウタイトルの更新 ---
    void UpdateTitle();

private:
    // --- 初期化 ---
    void CreateMenuBar();
    void CreateStatusBar();
    void CreateToolBar();

    // --- イベントハンドラ ---
    // ファイルメニュー
    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnSaveAs(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);

    // 編集メニュー
    void OnUndo(wxCommandEvent& event);
    void OnRedo(wxCommandEvent& event);
    void OnCut(wxCommandEvent& event);
    void OnCopy(wxCommandEvent& event);
    void OnPaste(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnFind(wxCommandEvent& event);

    // 表示メニュー
    void OnToggleLineNumbers(wxCommandEvent& event);
    void OnFont(wxCommandEvent& event);

    // ウィンドウイベント
    void OnCloseWindow(wxCloseEvent& event);
    void OnUpdateUI(wxUpdateUIEvent& event);
    void OnStatusUpdate(wxCommandEvent& event);

    // --- メンバ変数 ---
    CEditViewWx*  m_editView  = nullptr;   // テキスト編集ビュー
    wxString      m_currentFile;            // 現在開いているファイルパス
    bool          m_modified = false;       // 未保存変更あり

    wxDECLARE_EVENT_TABLE();
};

// カスタムイベント ID
enum {
    ID_VIEW_LINE_NUMBERS = wxID_HIGHEST + 100,
};
```

### `sakura_wx/window/CEditWndWx.cpp`

以下の実装を作成せよ（長いため要点のみ示す。コードは完全実装すること）：

```cpp
#include "CEditWndWx.h"
#include "view/CEditViewWx.h"
#include "dialog/CDlgSearchWx.h"
#include <wx/filedlg.h>
#include <wx/fontdlg.h>
#include <wx/msgdlg.h>

wxBEGIN_EVENT_TABLE(CEditWndWx, wxFrame)
    EVT_MENU(wxID_NEW,      CEditWndWx::OnNew)
    EVT_MENU(wxID_OPEN,     CEditWndWx::OnOpen)
    EVT_MENU(wxID_SAVE,     CEditWndWx::OnSave)
    EVT_MENU(wxID_SAVEAS,   CEditWndWx::OnSaveAs)
    EVT_MENU(wxID_CLOSE,    CEditWndWx::OnClose)
    EVT_MENU(wxID_EXIT,     CEditWndWx::OnQuit)
    EVT_MENU(wxID_UNDO,     CEditWndWx::OnUndo)
    EVT_MENU(wxID_REDO,     CEditWndWx::OnRedo)
    EVT_MENU(wxID_CUT,      CEditWndWx::OnCut)
    EVT_MENU(wxID_COPY,     CEditWndWx::OnCopy)
    EVT_MENU(wxID_PASTE,    CEditWndWx::OnPaste)
    EVT_MENU(wxID_SELECTALL,CEditWndWx::OnSelectAll)
    EVT_MENU(wxID_FIND,     CEditWndWx::OnFind)
    EVT_MENU(ID_VIEW_LINE_NUMBERS, CEditWndWx::OnToggleLineNumbers)
    EVT_MENU(wxID_PREFERENCES, CEditWndWx::OnFont)
    EVT_CLOSE(CEditWndWx::OnCloseWindow)
    EVT_UPDATE_UI(wxID_SAVE, CEditWndWx::OnUpdateUI)
wxEND_EVENT_TABLE()

CEditWndWx::CEditWndWx(const wxString& filePath)
    : wxFrame(nullptr, wxID_ANY,
              wxT("サクラエディタ"),
              wxDefaultPosition, wxSize(900, 700))
{
    CreateMenuBar();
    CreateStatusBar();
    CreateToolBar();

    // エディタビューの作成（ウィンドウ全体に広げる）
    m_editView = new CEditViewWx(this, wxID_ANY);

    // ウィンドウを画面中央に配置
    Centre();

    // 起動時ファイルが指定されていれば開く
    if (!filePath.IsEmpty()) {
        OpenFile(filePath);
    } else {
        UpdateTitle();
    }
}

CEditWndWx::~CEditWndWx() {}

void CEditWndWx::CreateMenuBar() {
    auto* menuBar = new wxMenuBar();

    // ファイルメニュー
    auto* menuFile = new wxMenu();
    menuFile->Append(wxID_NEW,    wxT("新規(&N)\tCtrl+N"));
    menuFile->Append(wxID_OPEN,   wxT("開く(&O)...\tCtrl+O"));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_SAVE,   wxT("上書き保存(&S)\tCtrl+S"));
    menuFile->Append(wxID_SAVEAS, wxT("名前を付けて保存(&A)..."));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_CLOSE,  wxT("閉じる(&W)\tCtrl+W"));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT,   wxT("終了(&X)\tAlt+F4"));
    menuBar->Append(menuFile, wxT("ファイル(&F)"));

    // 編集メニュー
    auto* menuEdit = new wxMenu();
    menuEdit->Append(wxID_UNDO,      wxT("元に戻す(&U)\tCtrl+Z"));
    menuEdit->Append(wxID_REDO,      wxT("やり直し(&R)\tCtrl+Y"));
    menuEdit->AppendSeparator();
    menuEdit->Append(wxID_CUT,       wxT("切り取り(&T)\tCtrl+X"));
    menuEdit->Append(wxID_COPY,      wxT("コピー(&C)\tCtrl+C"));
    menuEdit->Append(wxID_PASTE,     wxT("貼り付け(&P)\tCtrl+V"));
    menuEdit->AppendSeparator();
    menuEdit->Append(wxID_SELECTALL, wxT("すべて選択(&A)\tCtrl+A"));
    menuBar->Append(menuEdit, wxT("編集(&E)"));

    // 検索メニュー
    auto* menuSearch = new wxMenu();
    menuSearch->Append(wxID_FIND,    wxT("検索(&F)...\tCtrl+F"));
    menuSearch->Append(wxID_REPLACE, wxT("置換(&R)...\tCtrl+H"));
    menuBar->Append(menuSearch, wxT("検索(&S)"));

    // 表示メニュー
    auto* menuView = new wxMenu();
    menuView->AppendCheckItem(ID_VIEW_LINE_NUMBERS, wxT("行番号を表示(&L)"));
    menuView->Check(ID_VIEW_LINE_NUMBERS, true);
    menuView->AppendSeparator();
    menuView->Append(wxID_PREFERENCES, wxT("フォント設定(&F)..."));
    menuBar->Append(menuView, wxT("表示(&V)"));

    SetMenuBar(menuBar);
}

bool CEditWndWx::OpenFile(const wxString& path) {
    // ファイルをバイト列として読み込む
    if (!wxFileName::FileExists(path)) {
        wxMessageBox(wxT("ファイルが見つかりません: ") + path,
                     wxT("エラー"), wxOK | wxICON_ERROR);
        return false;
    }

    // CEditViewWx にファイルを渡して読み込む
    if (!m_editView->LoadFile(path)) {
        wxMessageBox(wxT("ファイルを開けませんでした: ") + path,
                     wxT("エラー"), wxOK | wxICON_ERROR);
        return false;
    }

    m_currentFile = path;
    m_modified = false;
    UpdateTitle();

    // ステータスバーにファイルパスを表示
    SetStatusText(path, 0);
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
        m_currentFile.IsEmpty() ? wxT("無題") : m_currentFile,
        wxT("テキストファイル (*.txt)|*.txt|すべてのファイル (*.*)|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dlg.ShowModal() != wxID_OK) return false;

    m_currentFile = dlg.GetPath();
    return SaveFile();
}

void CEditWndWx::UpdateTitle() {
    wxString title;
    if (m_currentFile.IsEmpty()) {
        title = wxT("無題");
    } else {
        title = wxFileName(m_currentFile).GetFullName();
    }
    if (m_modified) title = wxT("*") + title;
    SetTitle(title + wxT(" - サクラエディタ (wxWidgets)"));
}

// イベントハンドラ（残りの実装）
void CEditWndWx::OnNew(wxCommandEvent&) {
    // 未保存チェック
    if (m_modified) {
        auto ret = wxMessageBox(
            wxT("変更が保存されていません。保存しますか?"),
            wxT("確認"), wxYES_NO | wxCANCEL | wxICON_QUESTION);
        if (ret == wxCANCEL) return;
        if (ret == wxYES && !SaveFile()) return;
    }
    m_editView->ClearAll();
    m_currentFile.Clear();
    m_modified = false;
    UpdateTitle();
}

void CEditWndWx::OnOpen(wxCommandEvent&) {
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

void CEditWndWx::OnClose(wxCommandEvent&) {
    // 新規ファイルに戻る（タブ実装前の仮実装）
    OnNew(*(wxCommandEvent*)nullptr);
}

void CEditWndWx::OnQuit(wxCommandEvent&) { Close(); }

void CEditWndWx::OnUndo(wxCommandEvent&)     { m_editView->Undo(); }
void CEditWndWx::OnRedo(wxCommandEvent&)     { m_editView->Redo(); }
void CEditWndWx::OnCut(wxCommandEvent&)      { m_editView->Cut(); }
void CEditWndWx::OnCopy(wxCommandEvent&)     { m_editView->Copy(); }
void CEditWndWx::OnPaste(wxCommandEvent&)    { m_editView->Paste(); }
void CEditWndWx::OnSelectAll(wxCommandEvent&){ m_editView->SelectAll(); }

void CEditWndWx::OnFind(wxCommandEvent&) {
    auto* dlg = new CDlgSearchWx(this, m_editView);
    dlg->Show(true);
}

void CEditWndWx::OnToggleLineNumbers(wxCommandEvent& event) {
    m_editView->SetLineNumbersVisible(event.IsChecked());
}

void CEditWndWx::OnFont(wxCommandEvent&) {
    wxFontDialog dlg(this);
    if (dlg.ShowModal() == wxID_OK) {
        m_editView->SetEditorFont(dlg.GetFontData().GetChosenFont());
    }
}

void CEditWndWx::OnCloseWindow(wxCloseEvent& event) {
    if (m_modified) {
        auto ret = wxMessageBox(
            wxT("変更が保存されていません。保存しますか?"),
            wxT("確認"), wxYES_NO | wxCANCEL | wxICON_QUESTION);
        if (ret == wxCANCEL) { event.Veto(); return; }
        if (ret == wxYES) SaveFile();
    }
    event.Skip(); // ウィンドウを閉じる
}

void CEditWndWx::OnUpdateUI(wxUpdateUIEvent& event) {
    if (event.GetId() == wxID_SAVE) {
        event.Enable(m_modified);
    }
}
```

---

## Task 2-4: テキストエディタビュー（CEditViewWx）

wxStyledTextCtrl（Scintillaベース）を使って実装せよ。

### `sakura_wx/view/CEditViewWx.h`

```cpp
#pragma once
#include <wx/wx.h>
#include <wx/stc/stc.h>
#include <filesystem>

/**
 * CEditViewWx - テキスト編集ビュー（wxWidgets版）
 *
 * wxStyledTextCtrl (Scintilla) をベースにした高機能テキストエディタ。
 * - シンタックスハイライト（C/C++, Python, その他）
 * - 行番号表示
 * - 折り返し表示
 * - 日本語テキスト対応（UTF-8, Shift-JIS）
 * - IME入力対応（Phase 4 で詳細調整）
 */
class CEditViewWx : public wxStyledTextCtrl {
public:
    CEditViewWx(wxWindow* parent, wxWindowID id);

    // --- ファイル操作 ---
    bool LoadFile(const wxString& path);
    bool SaveFile(const wxString& path);

    // --- 表示設定 ---
    void SetLineNumbersVisible(bool visible);
    void SetEditorFont(const wxFont& font);

    // --- 文字コード ---
    enum class Encoding { UTF8, SJIS, EUCJP, UTF16LE, UTF16BE, Unknown };
    Encoding GetCurrentEncoding() const { return m_encoding; }

private:
    void InitializeEditor();
    void SetupSyntaxHighlighting(const wxString& filePath);
    void ApplyColorScheme();
    void UpdateStatusBar();

    // 文字コード判定
    Encoding DetectEncoding(const std::vector<char>& bytes);
    wxString ConvertToWxString(const std::vector<char>& bytes, Encoding enc);
    std::vector<char> ConvertFromWxString(const wxString& text, Encoding enc);

    // イベントハンドラ
    void OnModified(wxStyledTextEvent& event);
    void OnUpdateUI(wxStyledTextEvent& event);
    void OnCharAdded(wxStyledTextEvent& event);

    // --- メンバ変数 ---
    Encoding m_encoding = Encoding::UTF8;
    wxString m_currentPath;

    wxDECLARE_EVENT_TABLE();
};
```

### `sakura_wx/view/CEditViewWx.cpp`

```cpp
#include "CEditViewWx.h"
#include <wx/filename.h>
#include <fstream>
#include <vector>

wxBEGIN_EVENT_TABLE(CEditViewWx, wxStyledTextCtrl)
    EVT_STC_MODIFIED(wxID_ANY,   CEditViewWx::OnModified)
    EVT_STC_UPDATEUI(wxID_ANY,   CEditViewWx::OnUpdateUI)
    EVT_STC_CHARADDED(wxID_ANY,  CEditViewWx::OnCharAdded)
wxEND_EVENT_TABLE()

CEditViewWx::CEditViewWx(wxWindow* parent, wxWindowID id)
    : wxStyledTextCtrl(parent, id)
{
    InitializeEditor();
}

void CEditViewWx::InitializeEditor() {
    // --- 基本設定 ---
    SetMarginType(0, wxSTC_MARGIN_NUMBER);   // 行番号マージン
    SetMarginWidth(0, 50);                    // 行番号幅
    SetMarginType(1, wxSTC_MARGIN_SYMBOL);   // シンボルマージン
    SetMarginWidth(1, 0);                     // 初期は非表示

    // タブ設定
    SetTabWidth(4);
    SetUseTabs(false);          // スペースを使用
    SetIndent(4);
    SetTabIndents(true);

    // 折り返し（初期はオフ）
    SetWrapMode(wxSTC_WRAP_NONE);

    // --- カーソル設定 ---
    SetCaretLineVisible(true);   // 現在行ハイライト

    // --- エンコーディング ---
    // Scintilla は UTF-8 で動作させる
    SetCodePage(wxSTC_CP_UTF8);

    // --- デフォルトフォント設定 ---
    wxFont font;
#if defined(__APPLE__)
    font = wxFont(13, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL, false, wxT("Osaka-Mono"));
    if (!font.IsOk())
        font = wxFont(13, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                      wxFONTWEIGHT_NORMAL, false, wxT("Menlo"));
#elif defined(__WXGTK__)
    font = wxFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL, false, wxT("Noto Sans Mono CJK JP"));
    if (!font.IsOk())
        font = wxFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                      wxFONTWEIGHT_NORMAL, false, wxT("IPAGothic"));
#endif
    if (font.IsOk()) SetEditorFont(font);

    // カラースキームを適用
    ApplyColorScheme();
}

void CEditViewWx::ApplyColorScheme() {
    // サクラエディタ風のカラースキーム（デフォルト）
    // 背景: 白, テキスト: 黒
    StyleSetBackground(wxSTC_STYLE_DEFAULT, *wxWHITE);
    StyleSetForeground(wxSTC_STYLE_DEFAULT, *wxBLACK);
    StyleClearAll();

    // 行番号マージンの色
    StyleSetBackground(wxSTC_STYLE_LINENUMBER,
        wxColour(240, 240, 240));
    StyleSetForeground(wxSTC_STYLE_LINENUMBER,
        wxColour(100, 100, 100));

    // 現在行ハイライト
    SetCaretLineBackground(wxColour(232, 242, 254));

    // キャレット（カーソル）の色
    SetCaretForeground(*wxBLACK);
}

bool CEditViewWx::LoadFile(const wxString& path) {
    // ファイルをバイト列として読み込む
    std::ifstream ifs(path.ToStdString(), std::ios::binary | std::ios::ate);
    if (!ifs) return false;

    auto fileSize = ifs.tellg();
    ifs.seekg(0);
    std::vector<char> bytes(static_cast<size_t>(fileSize));
    if (!ifs.read(bytes.data(), fileSize)) return false;

    // 文字コード判定
    m_encoding = DetectEncoding(bytes);

    // wxString に変換（UTF-8 として Scintilla に渡す）
    wxString text = ConvertToWxString(bytes, m_encoding);

    // Scintilla にテキストをセット
    SetValue(text);
    EmptyUndoBuffer();  // ロード直後は Undo 履歴をクリア
    SetSavePoint();     // 保存済みマークを設定

    m_currentPath = path;

    // ファイル拡張子に応じてシンタックスハイライトを設定
    SetupSyntaxHighlighting(path);

    return true;
}

bool CEditViewWx::SaveFile(const wxString& path) {
    wxString text = GetValue();
    auto bytes = ConvertFromWxString(text, m_encoding);

    std::ofstream ofs(path.ToStdString(), std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(bytes.data(), bytes.size());
    if (!ofs) return false;

    SetSavePoint();
    return true;
}

void CEditViewWx::SetLineNumbersVisible(bool visible) {
    SetMarginWidth(0, visible ? 50 : 0);
}

void CEditViewWx::SetEditorFont(const wxFont& font) {
    StyleSetFont(wxSTC_STYLE_DEFAULT, font);
    StyleClearAll();  // デフォルトスタイルを全スタイルに適用
    ApplyColorScheme();
}

void CEditViewWx::SetupSyntaxHighlighting(const wxString& filePath) {
    wxString ext = wxFileName(filePath).GetExt().Lower();

    if (ext == wxT("cpp") || ext == wxT("cxx") || ext == wxT("cc") ||
        ext == wxT("c")   || ext == wxT("h")   || ext == wxT("hpp")) {
        SetLexer(wxSTC_LEX_CPP);
        // C/C++ キーワード
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

        // C++ ハイライト色
        StyleSetForeground(wxSTC_C_COMMENT,      wxColour(  0, 128,   0)); // 緑: コメント
        StyleSetForeground(wxSTC_C_COMMENTLINE,  wxColour(  0, 128,   0));
        StyleSetForeground(wxSTC_C_STRING,       wxColour(  0,   0, 255)); // 青: 文字列
        StyleSetForeground(wxSTC_C_CHARACTER,    wxColour(  0,   0, 255));
        StyleSetForeground(wxSTC_C_WORD,         wxColour(  0,   0, 128)); // 濃青: キーワード
        StyleSetForeground(wxSTC_C_NUMBER,       wxColour(128,   0,   0)); // 茶: 数値
        StyleSetForeground(wxSTC_C_PREPROCESSOR, wxColour(128,   0, 128)); // 紫: プリプロセッサ
        StyleSetBold(wxSTC_C_WORD, true);

    } else if (ext == wxT("py")) {
        SetLexer(wxSTC_LEX_PYTHON);
        SetKeyWords(0, wxT(
            "False None True and as assert async await break class continue "
            "def del elif else except finally for from global if import in "
            "is lambda nonlocal not or pass raise return try while with yield"));

    } else if (ext == wxT("md") || ext == wxT("markdown")) {
        SetLexer(wxSTC_LEX_MARKDOWN);
    } else {
        SetLexer(wxSTC_LEX_NULL); // ハイライトなし
    }
}

CEditViewWx::Encoding CEditViewWx::DetectEncoding(const std::vector<char>& bytes) {
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return Encoding::UTF8; // UTF-8 BOM
    }
    if (bytes.size() >= 2) {
        unsigned char b0 = bytes[0], b1 = bytes[1];
        if (b0 == 0xFF && b1 == 0xFE) return Encoding::UTF16LE;
        if (b0 == 0xFE && b1 == 0xFF) return Encoding::UTF16BE;
    }

    // Shift-JIS の簡易判定（0x81-0x9F, 0xE0-0xFC が第1バイト）
    // 詳細な文字コード判定は sakura_core/charset/ の実装を後で利用
    int sjisCount = 0, eucCount = 0;
    for (size_t i = 0; i + 1 < bytes.size(); ++i) {
        unsigned char c = bytes[i];
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
            sjisCount++;
        } else if (c == 0x8E || (c >= 0xA1 && c <= 0xFE)) {
            eucCount++;
        }
    }
    if (sjisCount > eucCount && sjisCount > 0) return Encoding::SJIS;
    if (eucCount > sjisCount && eucCount > 0) return Encoding::EUCJP;
    return Encoding::UTF8;
}

wxString CEditViewWx::ConvertToWxString(const std::vector<char>& bytes, Encoding enc) {
    // BOM をスキップ
    size_t offset = 0;
    if (enc == Encoding::UTF8 && bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF) {
        offset = 3;
    }

    switch (enc) {
    case Encoding::UTF8:
        return wxString::FromUTF8(bytes.data() + offset, bytes.size() - offset);

    case Encoding::SJIS:
        return wxString(bytes.data(), wxCSConv(wxT("CP932")), bytes.size());

    case Encoding::EUCJP:
        return wxString(bytes.data(), wxCSConv(wxT("EUC-JP")), bytes.size());

    default:
        // フォールバック: UTF-8 として試みる
        return wxString::FromUTF8(bytes.data(), bytes.size());
    }
}

std::vector<char> CEditViewWx::ConvertFromWxString(const wxString& text, Encoding enc) {
    switch (enc) {
    case Encoding::SJIS: {
        auto mb = text.mb_str(wxCSConv(wxT("CP932")));
        return std::vector<char>(mb.data(), mb.data() + mb.length());
    }
    case Encoding::EUCJP: {
        auto mb = text.mb_str(wxCSConv(wxT("EUC-JP")));
        return std::vector<char>(mb.data(), mb.data() + mb.length());
    }
    default: {
        // UTF-8 (BOMなし)
        auto utf8 = text.ToUTF8();
        return std::vector<char>(utf8.data(), utf8.data() + utf8.length());
    }
    }
}

void CEditViewWx::OnModified(wxStyledTextEvent& event) {
    // 親ウィンドウに変更通知（タイトルの * 表示用）
    // GetModify() で現在の変更状態を取得できる
    event.Skip();
}

void CEditViewWx::OnUpdateUI(wxStyledTextEvent& event) {
    // ステータスバーに行番号・列番号を更新
    // 親ウィンドウへのアクセスは GetParent() 経由
    auto* parent = GetParent();
    if (parent) {
        auto* frame = dynamic_cast<wxFrame*>(parent);
        if (frame && frame->GetStatusBar()) {
            int pos  = GetCurrentPos();
            int line = LineFromPosition(pos) + 1;
            int col  = GetColumn(pos) + 1;
            frame->SetStatusText(
                wxString::Format(wxT("行 %d  列 %d"), line, col), 1);

            // 文字コード表示
            const char* encName[] = {"UTF-8", "Shift-JIS", "EUC-JP",
                                     "UTF-16LE", "UTF-16BE", "不明"};
            frame->SetStatusText(
                wxString::FromUTF8(encName[static_cast<int>(m_encoding)]), 2);
        }
    }
    event.Skip();
}

void CEditViewWx::OnCharAdded(wxStyledTextEvent& event) {
    // 自動インデント（Enterキー押下時）
    int ch = event.GetKey();
    if (ch == '\n') {
        int line = GetCurrentLine();
        if (line > 0) {
            wxString indent = GetLineText(line - 1);
            wxString spaces;
            for (auto c : indent) {
                if (c == ' ' || c == '\t') spaces += c;
                else break;
            }
            if (!spaces.IsEmpty()) InsertText(GetCurrentPos(), spaces);
        }
    }
    event.Skip();
}
```

---

## Task 2-5: 検索ダイアログ（最小版）

### `sakura_wx/dialog/CDlgSearchWx.h` / `.cpp`

Ctrl+F で開く基本的な検索ダイアログを実装せよ：

要件：
- 検索文字列入力フィールド
- 「次を検索」「前を検索」「すべてハイライト」ボタン
- 大文字小文字区別チェックボックス
- 正規表現チェックボックス（wxSTC の組み込みサポートを使用）
- CEditViewWx への参照を保持して検索を実行
- モードレスダイアログ（非モーダル）として実装

---

## Task 2-6: ビルドと動作確認

```bash
# ビルド
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# 起動テスト
./build/bin/sakura_wx

# ファイルを引数に指定して起動テスト
./build/bin/sakura_wx /path/to/test.cpp
./build/bin/sakura_wx /path/to/japanese_text.txt
```

以下を確認してスクリーンショット相当の情報を `docs/porting/phase2_result.md` に記録せよ：
- ウィンドウが正常に開くか
- 日本語ファイル（UTF-8、Shift-JIS）が文字化けなく表示されるか
- C++ファイルでシンタックスハイライトが機能するか
- Ctrl+S で保存できるか
- Ctrl+Z/Y で Undo/Redo できるか

---

## Phase 2 完了チェックリスト

- [ ] `sakura_wx/` ディレクトリ構成が存在する
- [ ] `cmake --build build` が通る
- [ ] `./build/bin/sakura_wx` が起動する
- [ ] 日本語テキストファイルが文字化けなく表示される
- [ ] C++ファイルでシンタックスハイライトが動作する
- [ ] ファイルの読み込み・保存が機能する
- [ ] Undo/Redo が機能する
- [ ] 行番号・列番号がステータスバーに表示される
- [ ] `docs/porting/phase2_result.md` に動作確認結果が記録されている
