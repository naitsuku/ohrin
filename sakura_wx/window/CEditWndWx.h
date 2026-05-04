#pragma once
#include <wx/wx.h>
#include <wx/aui/aui.h>

class CEditViewWx;

/**
 * CEditWndWx - サクラエディタ メインウィンドウ（wxWidgets版）
 *
 * Win32版の CEditWnd に相当するが、wxFrame ベースで独立実装。
 * 将来的に CEditDoc / CEditView の wxWidgets 版インスタンスを管理する。
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

    // --- 編集状態の通知（CEditViewWx から呼ばれる） ---
    void NotifyModified(bool modified);

    // --- ステータスバー アクセサ ---
    enum {
        kStatusFieldFile = 0,
        kStatusFieldCursor = 1,
        kStatusFieldEncoding = 2,
        kStatusFieldCount = 3,
    };

private:
    void CreateMenuBarImpl();
    void CreateStatusBarImpl();

    // ファイルメニュー
    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnSaveAs(wxCommandEvent& event);
    void OnCloseFile(wxCommandEvent& event);
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
    void OnToggleWordWrap(wxCommandEvent& event);
    void OnFont(wxCommandEvent& event);

    // ヘルプメニュー
    void OnAbout(wxCommandEvent& event);

    // ウィンドウイベント
    void OnCloseWindow(wxCloseEvent& event);
    void OnUpdateUI(wxUpdateUIEvent& event);

    bool MaybeSaveBeforeAction();

    CEditViewWx*  m_editView   = nullptr;
    wxString      m_currentFile;
    bool          m_modified   = false;

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_VIEW_LINE_NUMBERS = wxID_HIGHEST + 100,
    ID_VIEW_WORD_WRAP,
};
