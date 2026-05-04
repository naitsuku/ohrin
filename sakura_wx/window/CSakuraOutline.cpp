#include "CSakuraOutline.h"
#include "CEditWndWx.h"
#include "view/CEditViewWx.h"

#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <regex>

namespace sakura_wx::ui {

CSakuraOutline::CSakuraOutline(wxWindow* parent, CEditWndWx* mainWnd)
    : wxPanel(parent)
    , m_mainWnd(mainWnd)
    , m_debounceTimer(this)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* label = new wxStaticText(this, wxID_ANY, wxT("アウトライン"));
    label->SetBackgroundColour(wxColour(239, 239, 239));
    label->SetForegroundColour(wxColour(0, 0, 0));
    sizer->Add(label, 0, wxEXPAND | wxALL, 4);

    m_tree = new wxTreeCtrl(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_HIDE_ROOT
            | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT);
    sizer->Add(m_tree, 1, wxEXPAND);
    SetSizer(sizer);

    m_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED,
                 &CSakuraOutline::OnItemActivated, this);
    Bind(wxEVT_TIMER, &CSakuraOutline::OnDebounceTimer, this);
}

void CSakuraOutline::NotifyModified() {
    // 連続編集中は最後のキー入力から 800ms 後に 1 回だけ Rebuild する。
    m_debounceTimer.Stop();
    m_debounceTimer.StartOnce(800);
}

void CSakuraOutline::OnDebounceTimer(wxTimerEvent& /*event*/) {
    if (m_currentView) Rebuild(m_currentView);
}

void CSakuraOutline::Rebuild(CEditViewWx* view) {
    m_currentView = view;
    m_tree->DeleteAllItems();
    auto root = m_tree->AddRoot(wxT("root"));
    if (!view) return;

    wxString text = view->GetText();

    // CEditViewWx::GetCurrentPath() で現在のファイルパスを取得（Phase 9 で正規化）。
    wxString path = view->GetCurrentPath();
    wxString ext = wxFileName(path).GetExt().Lower();

    std::vector<Hit> hits;
    if (ext == wxT("cpp") || ext == wxT("cxx") || ext == wxT("cc") ||
        ext == wxT("c")   || ext == wxT("h")   || ext == wxT("hpp") ||
        ext == wxT("hh")) {
        hits = ExtractCpp(text);
    } else if (ext == wxT("py")) {
        hits = ExtractPython(text);
    } else if (ext == wxT("md") || ext == wxT("markdown")) {
        hits = ExtractMarkdown(text);
    } else {
        // フォールバック: C/C++ regex で試す
        hits = ExtractCpp(text);
    }

    // depth に従って階層化挿入。直近の同 depth or 浅い深度の親を保持。
    std::vector<wxTreeItemId> stack; // index = depth
    stack.push_back(root);

    for (auto& h : hits) {
        int d = h.depth;
        if (d < 0) d = 0;
        if (d >= static_cast<int>(stack.size())) {
            // 親が無ければルート直下にフォールバック
            d = static_cast<int>(stack.size()) - 1;
            if (d < 0) d = 0;
        }
        wxTreeItemId parent = stack[d];
        wxString display = wxString::Format(wxT("%4d  %s"), h.line + 1, h.label);
        auto item = m_tree->AppendItem(parent, display);

        // この節を親候補として next-depth スロットに記録
        if (d + 1 < static_cast<int>(stack.size())) {
            stack[d + 1] = item;
            // それ以降は無効化（再代入で順次上書きされる）
            stack.resize(d + 2);
        } else {
            stack.push_back(item);
        }
    }

    m_tree->ExpandAll();
}

namespace {
struct LineDataItem : public wxTreeItemData {
    int lineZeroBased;
    explicit LineDataItem(int line) : lineZeroBased(line) {}
};
} // namespace

void CSakuraOutline::OnItemActivated(wxTreeEvent& event) {
    if (!m_currentView || !m_mainWnd) return;
    auto item = event.GetItem();
    wxString label = m_tree->GetItemText(item);
    long lineNum = 0;
    label.Trim(false).ToLong(&lineNum);
    if (lineNum > 0 && m_currentView) {
        // 現在のビューに対して直接ジャンプ（OpenFileAtLine はファイル開きを伴うので不適）
        m_currentView->GotoLine(static_cast<int>(lineNum) - 1);
        m_currentView->EnsureCaretVisible();
        m_currentView->SetFocus();
    }
}

// ============================================================
// 抽出ロジック (簡易版)
// ============================================================
std::vector<CSakuraOutline::Hit>
CSakuraOutline::ExtractCpp(const wxString& text) const {
    std::vector<Hit> out;
    // 関数定義: 戻り型 + 名前 + ( 引数 ) + { （またはクラス／構造体）
    // 簡易判定で誤検知は許容。
    static const std::wregex reFunc(
        LR"(^[A-Za-z_][\w\s\*&:<>,]+\s+([A-Za-z_~][\w]*)\s*\(.*\)\s*[^;]*$)",
        std::regex::optimize);
    static const std::wregex reClass(
        LR"(^\s*(class|struct|namespace)\s+([A-Za-z_][\w]*))",
        std::regex::optimize);

    std::wstring s = text.ToStdWstring();
    int line = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == L'\n') {
            std::wstring lineStr = s.substr(start, i - start);
            std::wsmatch m;
            if (std::regex_search(lineStr, m, reClass)) {
                // class/struct/namespace は depth 0 (ルート)
                out.push_back({line,
                    wxString::Format(wxT("[%s] %s"),
                        wxString(m[1].str()), wxString(m[2].str())),
                    /*depth=*/0});
            } else if (std::regex_search(lineStr, m, reFunc)) {
                if (lineStr.find(L';') == std::wstring::npos) {
                    // 関数定義: 行頭のスペース 4 = depth 1 と仮定
                    int indentSpaces = 0;
                    for (auto c : lineStr) {
                        if (c == L' ') ++indentSpaces;
                        else if (c == L'\t') indentSpaces += 4;
                        else break;
                    }
                    int depth = (indentSpaces >= 2) ? 1 : 0;
                    out.push_back({line, wxString(m[1].str()), depth});
                }
            }
            ++line;
            start = i + 1;
        }
    }
    return out;
}

std::vector<CSakuraOutline::Hit>
CSakuraOutline::ExtractPython(const wxString& text) const {
    std::vector<Hit> out;
    static const std::wregex reDef(
        LR"(^\s*(def|class|async\s+def)\s+([A-Za-z_][\w]*))",
        std::regex::optimize);

    std::wstring s = text.ToStdWstring();
    int line = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == L'\n') {
            std::wstring lineStr = s.substr(start, i - start);
            std::wsmatch m;
            if (std::regex_search(lineStr, m, reDef)) {
                wxString kind(m[1].str());
                wxString name(m[2].str());
                wxString prefix = (kind == wxT("class")) ? wxT("[class] ") : wxT("");
                // Python のインデント = 階層
                int indentSpaces = 0;
                for (auto c : lineStr) {
                    if (c == L' ') ++indentSpaces;
                    else if (c == L'\t') indentSpaces += 4;
                    else break;
                }
                int depth = indentSpaces / 4;
                out.push_back({line, prefix + name, depth});
            }
            ++line;
            start = i + 1;
        }
    }
    return out;
}

std::vector<CSakuraOutline::Hit>
CSakuraOutline::ExtractMarkdown(const wxString& text) const {
    std::vector<Hit> out;
    static const std::wregex reHead(LR"(^(#{1,6})\s+(.+)$)", std::regex::optimize);
    std::wstring s = text.ToStdWstring();
    int line = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == L'\n') {
            std::wstring lineStr = s.substr(start, i - start);
            // \r 削除
            if (!lineStr.empty() && lineStr.back() == L'\r') lineStr.pop_back();
            std::wsmatch m;
            if (std::regex_match(lineStr, m, reHead)) {
                size_t hashCount = m[1].length();  // # の数 = 見出しレベル
                int depth = static_cast<int>(hashCount) - 1;  // 0-based
                if (depth < 0) depth = 0;
                out.push_back({line, wxString(m[2].str()), depth});
            }
            ++line;
            start = i + 1;
        }
    }
    return out;
}

} // namespace sakura_wx::ui
