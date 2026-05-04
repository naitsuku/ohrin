#pragma once
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/thread.h>

#include <atomic>
#include <memory>
#include <regex>
#include <string>
#include <vector>

class CEditWndWx;
class wxCheckBox;
class wxComboBox;
class wxDirPickerCtrl;
class wxGauge;
class wxStaticText;
class wxTextCtrl;

namespace sakura_wx::dialog {

/**
 * GrepHit — 1 件のヒットを表す軽量データ。
 * Grep スレッドからメインスレッドへ wxCommandEvent + GetClientData 経由で渡される。
 */
struct GrepHit {
    wxString path;
    int      line;
    int      column;
    wxString text;       // 該当行（最大 200 文字程度に切り詰め）
};

class GrepThread;

/**
 * CDlgGrepWx — フォルダ再帰検索ダイアログ（モードレス）。
 *
 * 機能:
 *   - 検索文字列入力、検索フォルダ指定
 *   - ファイルパターン（"*.cpp;*.h" 等のセミコロン区切り glob）
 *   - 大小区別 / 単語単位 / 正規表現 / 再帰
 *   - 進捗ゲージ＋ステータス
 *   - 結果一覧（wxListCtrl）：ファイル名／行／列／該当行
 *   - 結果ダブルクリックで該当ファイルを CEditWndWx で開いてその行へジャンプ
 *
 * Grep 実行は wxThread + std::filesystem 上で行い、UI スレッドへは
 * wxQueueEvent でヒットを送る。停止／完了でスレッドが Detached 終了する。
 */
class CDlgGrepWx : public wxFrame {
public:
    CDlgGrepWx(wxWindow* parent, CEditWndWx* mainWnd);
    ~CDlgGrepWx() override;

    // GrepThread から呼ばれる（メインスレッド側で受信）
    void OnGrepHit(GrepHit hit);
    void OnGrepDone(int totalScanned, int totalHits, bool aborted);

    // GrepThread から DispatchHit / 終了通知用にアクセス
    friend class GrepThread;

private:
    void BuildUI();
    void OnStart(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event);
    void OnCloseWindow(wxCloseEvent& event);
    void OnResultActivated(wxListEvent& event);
    void HandleHitEvent(wxCommandEvent& event);
    void HandleDoneEvent(wxCommandEvent& event);

    void StopThread();

    CEditWndWx*  m_mainWnd = nullptr;

    wxTextCtrl*  m_needle  = nullptr;
    wxComboBox*  m_pattern = nullptr;
    wxDirPickerCtrl* m_folder = nullptr;
    wxCheckBox*  m_caseCB    = nullptr;
    wxCheckBox*  m_wholeCB   = nullptr;
    wxCheckBox*  m_regexCB   = nullptr;
    wxCheckBox*  m_recurseCB = nullptr;

    wxButton*    m_startBtn = nullptr;
    wxButton*    m_stopBtn  = nullptr;

    wxListCtrl*  m_results  = nullptr;
    wxStaticText* m_status  = nullptr;
    wxGauge*     m_gauge    = nullptr;

    GrepThread*  m_thread = nullptr;
    std::atomic<bool> m_running{false};

    enum {
        ID_GREP_START = wxID_HIGHEST + 300,
        ID_GREP_STOP,
        ID_GREP_RESULTS,
        ID_GREP_HIT_EVENT,
        ID_GREP_DONE_EVENT,
    };

    wxDECLARE_EVENT_TABLE();
};

} // namespace sakura_wx::dialog
