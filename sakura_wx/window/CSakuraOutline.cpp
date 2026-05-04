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
}

void CSakuraOutline::Rebuild(CEditViewWx* view) {
    m_currentView = view;
    m_tree->DeleteAllItems();
    auto root = m_tree->AddRoot(wxT("root"));
    if (!view) return;

    wxString text = view->GetText();

    // ファイル拡張子を wxAuiNotebook のタブタイトルから推定する簡易実装。
    // (CEditWndWx::TabState は private のため公開アクセサが無い)
    wxString ext;
    if (auto* nb = dynamic_cast<wxAuiNotebook*>(view->GetParent())) {
        int sel = nb->GetSelection();
        if (sel != wxNOT_FOUND) {
            wxString tabName = nb->GetPageText(sel);
            if (tabName.StartsWith(wxT("* "))) tabName = tabName.Mid(2);
            ext = wxFileName(tabName).GetExt().Lower();
        }
    }

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

    for (auto& h : hits) {
        wxString display = wxString::Format(wxT("%4d  %s"), h.line + 1, h.label);
        auto item = m_tree->AppendItem(root, display);
        // 行番号を ClientData に格納
        m_tree->SetItemData(item, new wxTreeItemData());
        // wxTreeItemData は基底クラスなので派生で行番号を持つ
        // 簡易に AppendItem の戻り値と並列の vector で管理する手もあるが、
        // ここでは tree item のラベル先頭の数字をパースする方式にする。
    }
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
                out.push_back({line,
                    wxString::Format(wxT("[%s] %s"),
                        wxString(m[1].str()), wxString(m[2].str()))});
            } else if (std::regex_search(lineStr, m, reFunc)) {
                // ; で終わる宣言はスキップ
                if (lineStr.find(L';') == std::wstring::npos) {
                    out.push_back({line, wxString(m[1].str())});
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
                out.push_back({line, prefix + name});
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
                size_t depth = m[1].length();
                wxString indent;
                for (size_t k = 1; k < depth; ++k) indent += wxT("  ");
                out.push_back({line, indent + wxString(m[2].str())});
            }
            ++line;
            start = i + 1;
        }
    }
    return out;
}

} // namespace sakura_wx::ui
