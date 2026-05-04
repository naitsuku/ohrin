#pragma once
#include <wx/wx.h>
#include <wx/aui/auibook.h>

#include <unordered_map>

class CEditViewWx;
namespace sakura_wx::ui {
    class CSakuraRuler;
    class CSakuraMiniMap;
    class CSakuraOutline;
}
class wxSplitterWindow;

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

    // --- ビューアクセサ（アクティブタブの view を返す） ---
    CEditViewWx* GetEditView() { return ActiveView(); }
    CEditViewWx* ActiveView() const;

    // --- ファイルを開いて指定行へジャンプ（Grep 結果ダブルクリック等） ---
    bool OpenFileAtLine(const wxString& path, int line1Based);

    // --- アウトライン dirty 通知 (CEditViewWx::OnModified から呼ばれる) ---
    void NotifyOutlineDirty();

    // --- タブ管理 ---
    int  AddNewTab(const wxString& title = wxT("無題"));  // 新規タブ作成、index を返す
    bool ActivateTabForFile(const wxString& path);         // 既に開いているタブをアクティブ化

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

    // 印刷
    void OnPrint(wxCommandEvent& event);
    void OnPrintPreview(wxCommandEvent& event);
    void OnPageSetup(wxCommandEvent& event);

    // 編集メニュー
    void OnUndo(wxCommandEvent& event);
    void OnRedo(wxCommandEvent& event);
    void OnCut(wxCommandEvent& event);
    void OnCopy(wxCommandEvent& event);
    void OnPaste(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent& event);
    void OnFind(wxCommandEvent& event);
    void OnReplace(wxCommandEvent& event);
    void OnGrep(wxCommandEvent& event);

    // 「次を検索」「前を検索」「検索クリア」 — 最後の検索状態を使って再実行
    void OnFindNext(wxCommandEvent& event);
    void OnFindPrev(wxCommandEvent& event);
    void OnClearSearchMarks(wxCommandEvent& event);

public:
    // CDlgSearchWx から最後の検索状態を受け取る
    void StoreLastSearch(const wxString& needle, int searchFlags) {
        m_lastSearchNeedle = needle;
        m_lastSearchFlags  = searchFlags;
    }

private:
    wxString m_lastSearchNeedle;
    int      m_lastSearchFlags = 0;

    // 表示メニュー
    void OnToggleLineNumbers(wxCommandEvent& event);
    void OnToggleWordWrap(wxCommandEvent& event);
    void OnToggleWhitespace(wxCommandEvent& event);
    void OnToggleIdeographicSpace(wxCommandEvent& event);
    void OnToggleCodeFolding(wxCommandEvent& event);
    void OnToggleRuler(wxCommandEvent& event);
    void OnToggleCurrentLineUnderline(wxCommandEvent& event);
    void OnToggleEofMark(wxCommandEvent& event);
    void OnToggleEolVisible(wxCommandEvent& event);
    void OnToggleOutline(wxCommandEvent& event);
    void OnRefreshOutline(wxCommandEvent& event);
    void OnFont(wxCommandEvent& event);

    // ブックマークメニュー
    void OnBookmarkToggle(wxCommandEvent& event);
    void OnBookmarkNext(wxCommandEvent& event);
    void OnBookmarkPrev(wxCommandEvent& event);
    void OnBookmarkClearAll(wxCommandEvent& event);

    // 文字コード／改行コード
    void OnEncodingChange(wxCommandEvent& event);   // ID_ENCODING_*
    void OnEolChange(wxCommandEvent& event);        // ID_EOL_*
    void OnReloadAsEncoding(wxCommandEvent& event);

    // 最近使ったファイル
    void OnRecentFile(wxCommandEvent& event);
    void RefreshRecentFilesMenu();

    // ヘルプメニュー
    void OnAbout(wxCommandEvent& event);

    // ウィンドウイベント
    void OnCloseWindow(wxCloseEvent& event);
    void OnUpdateUI(wxUpdateUIEvent& event);

    bool MaybeSaveBeforeAction();

    // Phase 13c: SettingsMeta.h のメタ値を全タブに適用する。
    //   設定ダイアログ OK 時 / 起動時に呼び出す。
    //   現在反映しているメタ ID:
    //     general/m_bIsINSMode   → SetOvertype
    //     general/m_nCaretType   → SetCaretStyle
    //     general/m_bExitConfirm → m_exitConfirmEnabled (OnCloseWindow で参照)
    //     window/m_bScrollBarHorz → SetUseHorizontalScrollBar
    void ApplyMetaSettingsToAllViews();

    // タブ毎のメタデータ
    struct TabState {
        wxString currentFile;
        bool     modified = false;
    };

    wxAuiNotebook* m_notebook = nullptr;
    std::unordered_map<CEditViewWx*, TabState> m_tabs;

    TabState* ActiveState();
    const TabState* ActiveState() const;
    void SyncMirrorFromActive();  // m_currentFile / m_modified を同期

    void OnTabClose(wxAuiNotebookEvent& event);
    void OnTabChanged(wxAuiNotebookEvent& event);

    // アクティブタブのミラー（既存コードからの参照を維持するため）
    CEditViewWx* m_editView    = nullptr;
    wxString     m_currentFile;
    bool         m_modified    = false;

    // Phase 13c: 終了時確認ダイアログを出すか (general/m_bExitConfirm)。
    //   ApplyMetaSettingsToAllViews で更新される。
    bool         m_exitConfirmEnabled = true;

    // ツールバー (Phase 14b: m_bDispTOOLBAR で表示切替するため保持)
    wxToolBar* m_toolBar = nullptr;

    // 単一ルーラー (アクティブビューに追従)
    sakura_wx::ui::CSakuraRuler* m_ruler = nullptr;

    // ミニマップ (Phase 15b: アクティブビューに追従)
    sakura_wx::ui::CSakuraMiniMap* m_miniMap     = nullptr;
    wxSplitterWindow*              m_rightSplit  = nullptr; // notebook | minimap
    int                            m_miniMapWidth = 110;

    // アウトラインサイドパネル (Phase 8 追加)
    wxSplitterWindow*              m_splitter  = nullptr;
    sakura_wx::ui::CSakuraOutline* m_outline   = nullptr;
    int                            m_outlineSash = 220;  // 表示時のサッシ位置

    wxMenu*   m_recentMenu = nullptr;

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_VIEW_LINE_NUMBERS = wxID_HIGHEST + 100,
    ID_VIEW_WORD_WRAP,
    ID_VIEW_WHITESPACE,
    ID_VIEW_IDEOGRAPHIC_SPACE,
    ID_VIEW_CODE_FOLDING,
    ID_VIEW_RULER,
    ID_VIEW_CURRENT_LINE_UNDERLINE,
    ID_VIEW_EOF_MARK,
    ID_VIEW_EOL_VISIBLE,
    ID_VIEW_OUTLINE,
    ID_VIEW_OUTLINE_REFRESH,
    ID_SEARCH_GREP,
    ID_SEARCH_FIND_NEXT,
    ID_SEARCH_FIND_PREV,
    ID_SEARCH_CLEAR_MARKS,

    // ブックマーク
    ID_BOOKMARK_TOGGLE = wxID_HIGHEST + 120,
    ID_BOOKMARK_NEXT,
    ID_BOOKMARK_PREV,
    ID_BOOKMARK_CLEAR_ALL,

    // 文字コード（再読み込み・変換）
    ID_ENCODING_BASE = wxID_HIGHEST + 140,
    ID_ENCODING_UTF8        = ID_ENCODING_BASE,
    ID_ENCODING_UTF8_BOM,
    ID_ENCODING_SJIS,
    ID_ENCODING_EUCJP,
    ID_ENCODING_UTF16LE,
    ID_ENCODING_UTF16BE,
    ID_ENCODING_RELOAD_BASE = wxID_HIGHEST + 160,  // 再読み込み版（同じ順）

    // 改行コード
    ID_EOL_CRLF = wxID_HIGHEST + 180,
    ID_EOL_LF,
    ID_EOL_CR,

    // 最近使ったファイル（最大 10 個分の予約 ID）
    ID_RECENT_FILE_BASE = wxID_HIGHEST + 200,
    ID_RECENT_FILE_LAST = ID_RECENT_FILE_BASE + 9,
    ID_RECENT_FILE_CLEAR = wxID_HIGHEST + 210,
};
