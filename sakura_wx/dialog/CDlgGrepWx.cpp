#include "CDlgGrepWx.h"
#include "view/CEditViewWx.h"
#include "window/CEditWndWx.h"

#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/filepicker.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/thread.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;

namespace sakura_wx::dialog {

// ============================================================
// 内部 GrepThread
// ============================================================
class GrepThread : public wxThread {
public:
    struct Params {
        wxString rootDir;
        wxString needle;
        std::vector<wxString> patterns;  // glob ("*.cpp" 等)
        bool caseSensitive = false;
        bool wholeWord     = false;
        bool useRegex      = false;
        bool recursive     = true;
    };

    GrepThread(CDlgGrepWx* dlg, Params params)
        : wxThread(wxTHREAD_DETACHED)
        , m_dlg(dlg)
        , m_params(std::move(params))
    {}

    void RequestStop() { m_stop.store(true, std::memory_order_release); }

protected:
    ExitCode Entry() override;

private:
    bool MatchesPattern(const fs::path& p) const;
    void ProcessFile(const fs::path& p, int& hitCount);
    bool ContainsLiteral(const std::string& haystack, const std::string& needle) const;
    bool ContainsWord(const std::string& haystack, const std::string& needle) const;
    void DispatchHit(GrepHit hit);

    CDlgGrepWx* m_dlg;
    Params m_params;
    std::atomic<bool> m_stop{false};
};

namespace {

// glob (* ?) を std::regex に変換する簡易ヘルパ
std::wregex GlobToRegex(const wxString& glob) {
    std::wstring re = L"^";
    for (size_t i = 0; i < glob.length(); ++i) {
        wchar_t c = glob[i].GetValue();
        switch (c) {
        case L'*': re += L".*"; break;
        case L'?': re += L"."; break;
        case L'.': case L'+': case L'(': case L')':
        case L'[': case L']': case L'{': case L'}':
        case L'^': case L'$': case L'|': case L'\\':
            re += L'\\'; re += c; break;
        default: re += c;
        }
    }
    re += L"$";
    return std::wregex(re, std::regex_constants::icase);
}

} // namespace

bool GrepThread::MatchesPattern(const fs::path& p) const {
    if (m_params.patterns.empty()) return true;
    auto name = p.filename().wstring();
    for (auto& pat : m_params.patterns) {
        try {
            auto re = GlobToRegex(pat);
            if (std::regex_match(name, re)) return true;
        } catch (...) { /* 不正な glob はスキップ */ }
    }
    return false;
}

bool GrepThread::ContainsLiteral(const std::string& haystack, const std::string& needle) const {
    if (m_params.caseSensitive) {
        return haystack.find(needle) != std::string::npos;
    }
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != haystack.end();
}

bool GrepThread::ContainsWord(const std::string& haystack, const std::string& needle) const {
    auto isword = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    size_t pos = 0;
    while (pos <= haystack.size()) {
        auto found = m_params.caseSensitive
            ? haystack.find(needle, pos)
            : std::distance(haystack.begin(),
                std::search(haystack.begin() + pos, haystack.end(),
                    needle.begin(), needle.end(),
                    [](char a, char b) { return std::tolower(a) == std::tolower(b); }));
        if (found == static_cast<size_t>(haystack.size())
            || found == std::string::npos) return false;
        bool leftOk  = (found == 0) || !isword(haystack[found - 1]);
        bool rightOk = (found + needle.size() >= haystack.size())
                       || !isword(haystack[found + needle.size()]);
        if (leftOk && rightOk) return true;
        pos = found + 1;
    }
    return false;
}

void GrepThread::ProcessFile(const fs::path& p, int& hitCount) {
    if (m_stop.load(std::memory_order_acquire)) return;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return;

    std::string needleUtf8(wxString(m_params.needle).ToUTF8());
    std::regex needleRegex;
    bool regexOk = false;
    if (m_params.useRegex) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!m_params.caseSensitive) flags |= std::regex_constants::icase;
            needleRegex = std::regex(needleUtf8, flags);
            regexOk = true;
        } catch (...) {
            return; // 不正 regex は当該ファイルのみスキップ
        }
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(ifs, line)) {
        ++lineNo;
        if (m_stop.load(std::memory_order_acquire)) return;
        // 末尾 \r 除去
        if (!line.empty() && line.back() == '\r') line.pop_back();

        bool match = false;
        int matchCol = 0;
        if (m_params.useRegex && regexOk) {
            std::smatch m;
            if (std::regex_search(line, m, needleRegex)) {
                match = true;
                matchCol = static_cast<int>(m.position(0));
            }
        } else if (m_params.wholeWord) {
            match = ContainsWord(line, needleUtf8);
        } else {
            match = ContainsLiteral(line, needleUtf8);
            if (match) {
                if (m_params.caseSensitive) {
                    matchCol = static_cast<int>(line.find(needleUtf8));
                }
            }
        }
        if (!match) continue;

        GrepHit hit;
        hit.path   = wxString::FromUTF8(p.string());
        hit.line   = lineNo;
        hit.column = matchCol + 1;
        std::string truncated = line.substr(0, std::min<size_t>(line.size(), 200));
        hit.text   = wxString::FromUTF8(truncated);
        DispatchHit(std::move(hit));
        ++hitCount;
    }
}

void GrepThread::DispatchHit(GrepHit hit) {
    auto* event = new wxCommandEvent(wxEVT_COMMAND_TEXT_UPDATED,
                                     CDlgGrepWx::ID_GREP_HIT_EVENT);
    event->SetClientObject(new wxStringClientData(/*placeholder*/ ""));
    // 専用の構造体を heap で持たせる
    auto* heapHit = new GrepHit(std::move(hit));
    event->SetClientData(heapHit);
    wxQueueEvent(m_dlg, event);
}

wxThread::ExitCode GrepThread::Entry() {
    int scanned = 0;
    int hits    = 0;
    bool aborted = false;

    fs::path root(m_params.rootDir.utf8_string());
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        // 終了通知
        auto* ev = new wxCommandEvent(wxEVT_COMMAND_TEXT_UPDATED,
                                      CDlgGrepWx::ID_GREP_DONE_EVENT);
        ev->SetInt(0);
        ev->SetExtraLong(0);
        ev->SetString(wxT("invalid root"));
        wxQueueEvent(m_dlg, ev);
        return nullptr;
    }

    auto handle = [&](const fs::path& p) {
        if (m_stop.load(std::memory_order_acquire)) { aborted = true; return false; }
        if (!fs::is_regular_file(p, ec)) return true;
        if (!MatchesPattern(p)) return true;
        ++scanned;
        ProcessFile(p, hits);
        return true;
    };

    if (m_params.recursive) {
        for (auto it = fs::recursive_directory_iterator(root,
                fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); ++it) {
            if (!handle(it->path())) break;
        }
    } else {
        for (auto it = fs::directory_iterator(root, ec);
             it != fs::directory_iterator(); ++it) {
            if (!handle(it->path())) break;
        }
    }

    auto* ev = new wxCommandEvent(wxEVT_COMMAND_TEXT_UPDATED,
                                  CDlgGrepWx::ID_GREP_DONE_EVENT);
    ev->SetInt(scanned);
    ev->SetExtraLong(hits);
    ev->SetString(aborted ? wxT("aborted") : wxT("done"));
    wxQueueEvent(m_dlg, ev);
    return nullptr;
}

// ============================================================
// CDlgGrepWx
// ============================================================
wxBEGIN_EVENT_TABLE(CDlgGrepWx, wxFrame)
    EVT_BUTTON(ID_GREP_START, CDlgGrepWx::OnStart)
    EVT_BUTTON(ID_GREP_STOP,  CDlgGrepWx::OnStop)
    EVT_BUTTON(wxID_CLOSE,    CDlgGrepWx::OnClose)
    EVT_CLOSE(                CDlgGrepWx::OnCloseWindow)
    EVT_LIST_ITEM_ACTIVATED(ID_GREP_RESULTS, CDlgGrepWx::OnResultActivated)
    EVT_COMMAND(ID_GREP_HIT_EVENT,  wxEVT_COMMAND_TEXT_UPDATED, CDlgGrepWx::HandleHitEvent)
    EVT_COMMAND(ID_GREP_DONE_EVENT, wxEVT_COMMAND_TEXT_UPDATED, CDlgGrepWx::HandleDoneEvent)
wxEND_EVENT_TABLE()

CDlgGrepWx::CDlgGrepWx(wxWindow* parent, CEditWndWx* mainWnd)
    : wxFrame(parent, wxID_ANY, wxT("Grep（フォルダ検索）"),
              wxDefaultPosition, wxSize(720, 500))
    , m_mainWnd(mainWnd)
{
    BuildUI();
    Centre();
}

CDlgGrepWx::~CDlgGrepWx() {
    StopThread();
}

void CDlgGrepWx::BuildUI() {
    auto* panel = new wxPanel(this);
    auto* top = new wxBoxSizer(wxVERTICAL);

    // 検索文字列
    auto* row1 = new wxBoxSizer(wxHORIZONTAL);
    row1->Add(new wxStaticText(panel, wxID_ANY, wxT("検索:")), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_needle = new wxTextCtrl(panel, wxID_ANY);
    row1->Add(m_needle, 1, wxEXPAND);
    top->Add(row1, 0, wxEXPAND | wxALL, 6);

    // フォルダ＋パターン
    auto* row2 = new wxBoxSizer(wxHORIZONTAL);
    row2->Add(new wxStaticText(panel, wxID_ANY, wxT("フォルダ:")), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_folder = new wxDirPickerCtrl(panel, wxID_ANY, wxGetCwd());
    row2->Add(m_folder, 1, wxEXPAND);
    top->Add(row2, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    auto* row3 = new wxBoxSizer(wxHORIZONTAL);
    row3->Add(new wxStaticText(panel, wxID_ANY, wxT("パターン:")), 0,
              wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    wxArrayString defaults;
    defaults.Add(wxT("*.cpp;*.h;*.hpp;*.cc"));
    defaults.Add(wxT("*.txt;*.md"));
    defaults.Add(wxT("*.py"));
    defaults.Add(wxT("*"));
    m_pattern = new wxComboBox(panel, wxID_ANY, wxT("*.cpp;*.h"),
                               wxDefaultPosition, wxDefaultSize, defaults);
    row3->Add(m_pattern, 1, wxEXPAND);
    top->Add(row3, 0, wxEXPAND | wxALL, 6);

    // オプション
    auto* opts = new wxBoxSizer(wxHORIZONTAL);
    m_caseCB    = new wxCheckBox(panel, wxID_ANY, wxT("大小区別"));
    m_wholeCB   = new wxCheckBox(panel, wxID_ANY, wxT("単語単位"));
    m_regexCB   = new wxCheckBox(panel, wxID_ANY, wxT("正規表現"));
    m_recurseCB = new wxCheckBox(panel, wxID_ANY, wxT("再帰"));
    m_recurseCB->SetValue(true);
    opts->Add(m_caseCB,    0, wxRIGHT, 12);
    opts->Add(m_wholeCB,   0, wxRIGHT, 12);
    opts->Add(m_regexCB,   0, wxRIGHT, 12);
    opts->Add(m_recurseCB, 0, wxRIGHT, 12);
    top->Add(opts, 0, wxLEFT | wxRIGHT, 6);

    // ボタン
    auto* btns = new wxBoxSizer(wxHORIZONTAL);
    m_startBtn = new wxButton(panel, ID_GREP_START, wxT("検索開始"));
    m_stopBtn  = new wxButton(panel, ID_GREP_STOP,  wxT("中止"));
    m_stopBtn->Enable(false);
    btns->Add(m_startBtn);
    btns->Add(m_stopBtn, 0, wxLEFT, 6);
    btns->AddStretchSpacer(1);
    btns->Add(new wxButton(panel, wxID_CLOSE, wxT("閉じる")), 0, wxLEFT, 6);
    top->Add(btns, 0, wxEXPAND | wxALL, 6);

    // 結果リスト
    m_results = new wxListCtrl(panel, ID_GREP_RESULTS,
                               wxDefaultPosition, wxDefaultSize,
                               wxLC_REPORT | wxLC_SINGLE_SEL);
    m_results->AppendColumn(wxT("ファイル"), wxLIST_FORMAT_LEFT, 320);
    m_results->AppendColumn(wxT("行"),       wxLIST_FORMAT_RIGHT, 60);
    m_results->AppendColumn(wxT("列"),       wxLIST_FORMAT_RIGHT, 50);
    m_results->AppendColumn(wxT("該当行"),    wxLIST_FORMAT_LEFT, 280);
    top->Add(m_results, 1, wxEXPAND | wxALL, 6);

    // 進捗
    auto* progRow = new wxBoxSizer(wxHORIZONTAL);
    m_gauge  = new wxGauge(panel, wxID_ANY, 100, wxDefaultPosition, wxSize(150, -1));
    m_status = new wxStaticText(panel, wxID_ANY, wxT(""));
    progRow->Add(m_gauge, 0, wxRIGHT, 8);
    progRow->Add(m_status, 1, wxALIGN_CENTER_VERTICAL);
    top->Add(progRow, 0, wxEXPAND | wxALL, 6);

    panel->SetSizerAndFit(top);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(panel, 1, wxEXPAND);
    SetSizer(outer);
}

void CDlgGrepWx::OnStart(wxCommandEvent&) {
    if (m_running.load()) return;

    GrepThread::Params params;
    params.rootDir       = m_folder->GetPath();
    params.needle        = m_needle->GetValue();
    params.caseSensitive = m_caseCB->IsChecked();
    params.wholeWord     = m_wholeCB->IsChecked();
    params.useRegex      = m_regexCB->IsChecked();
    params.recursive     = m_recurseCB->IsChecked();

    if (params.needle.IsEmpty()) {
        m_status->SetLabel(wxT("検索文字列を入力してください"));
        return;
    }

    auto patStr = m_pattern->GetValue();
    wxArrayString patArr = wxSplit(patStr, ';');
    for (auto& p : patArr) {
        p.Trim().Trim(false);
        if (!p.IsEmpty()) params.patterns.push_back(p);
    }

    m_results->DeleteAllItems();
    m_status->SetLabel(wxT("検索中..."));
    m_gauge->Pulse();
    m_startBtn->Enable(false);
    m_stopBtn->Enable(true);
    m_running.store(true);

    m_thread = new GrepThread(this, std::move(params));
    if (m_thread->Run() != wxTHREAD_NO_ERROR) {
        m_status->SetLabel(wxT("スレッド開始失敗"));
        m_thread = nullptr;
        m_running.store(false);
        m_startBtn->Enable(true);
        m_stopBtn->Enable(false);
    }
}

void CDlgGrepWx::OnStop(wxCommandEvent&) {
    StopThread();
}

void CDlgGrepWx::StopThread() {
    if (m_thread) {
        m_thread->RequestStop();
        // detached thread なので delete は不要、スレッドが自分で消える
        m_thread = nullptr;
    }
}

void CDlgGrepWx::HandleHitEvent(wxCommandEvent& event) {
    auto* hit = static_cast<GrepHit*>(event.GetClientData());
    if (!hit) return;

    long row = m_results->InsertItem(m_results->GetItemCount(), hit->path);
    m_results->SetItem(row, 1, wxString::Format(wxT("%d"), hit->line));
    m_results->SetItem(row, 2, wxString::Format(wxT("%d"), hit->column));
    m_results->SetItem(row, 3, hit->text);
    delete hit;
}

void CDlgGrepWx::HandleDoneEvent(wxCommandEvent& event) {
    int scanned = event.GetInt();
    int hits    = static_cast<int>(event.GetExtraLong());
    wxString state = event.GetString();
    wxString msg = wxString::Format(
        wxT("完了 — 走査 %d ファイル / ヒット %d 件 (%s)"),
        scanned, hits, state);
    m_status->SetLabel(msg);
    m_gauge->SetValue(0);
    m_startBtn->Enable(true);
    m_stopBtn->Enable(false);
    m_running.store(false);
    m_thread = nullptr;
}

void CDlgGrepWx::OnGrepHit(GrepHit /*hit*/)   { /* 直呼び出し版（未使用） */ }
void CDlgGrepWx::OnGrepDone(int, int, bool)   { /* 直呼び出し版（未使用） */ }

void CDlgGrepWx::OnResultActivated(wxListEvent& event) {
    if (!m_mainWnd) return;
    long row = event.GetIndex();
    wxString path = m_results->GetItemText(row, 0);
    wxString lineStr = m_results->GetItemText(row, 1);
    long line = 0;
    lineStr.ToLong(&line);

    if (m_mainWnd->OpenFile(path)) {
        // 1-based 行番号 → 0-based + GotoLine
        // CEditViewWx は wxStyledTextCtrl 派生なので GotoLine が使える。
        // ただしここからは frame 経由で view に到達する必要があるが、
        // OpenFile 後に親フレームが view を持っているので強引に再キャストせず、
        // メニュー経由ではなく view メソッドを呼ぶための shortcut を後日整備する。
    }
    (void)line; // Phase 7 で行ジャンプを実装
}

void CDlgGrepWx::OnClose(wxCommandEvent&) { Close(); }

void CDlgGrepWx::OnCloseWindow(wxCloseEvent& event) {
    StopThread();
    event.Skip();
}

} // namespace sakura_wx::dialog
