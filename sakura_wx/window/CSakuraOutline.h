#pragma once
#include <wx/panel.h>
#include <wx/treectrl.h>

#include <vector>

class CEditWndWx;
class CEditViewWx;

namespace sakura_wx::ui {

/**
 * CSakuraOutline — 関数定義／クラス／見出しを抽出するアウトラインパネル。
 *
 * Phase 8 では言語別の簡易 regex 抽出のみ。
 * 詳細な構文木解析は Phase 9 以降で sakura_core/outline/ の取り込みを検討。
 *
 * 対応:
 *   - C/C++ (.c .cpp .cxx .cc .h .hpp): 関数定義・class/struct
 *   - Python (.py):                       def / class
 *   - Markdown (.md):                     見出し # 〜 ######
 *
 * 使い方:
 *   1. CEditWndWx 作成時に CSakuraOutline をサイドパネルとして配置
 *   2. タブ切替時に Refresh(view) を呼ぶ
 *   3. ツリーアイテムダブルクリック → 該当行へジャンプ
 */
class CSakuraOutline : public wxPanel {
public:
    explicit CSakuraOutline(wxWindow* parent, CEditWndWx* mainWnd);

    /// 現在のアクティブビューの内容で再構築。view が nullptr ならクリア。
    void Rebuild(CEditViewWx* view);

private:
    void OnItemActivated(wxTreeEvent& event);

    CEditWndWx*  m_mainWnd = nullptr;
    wxTreeCtrl*  m_tree    = nullptr;
    CEditViewWx* m_currentView = nullptr;

    struct Hit { int line; wxString label; };
    std::vector<Hit> ExtractCpp(const wxString& text) const;
    std::vector<Hit> ExtractPython(const wxString& text) const;
    std::vector<Hit> ExtractMarkdown(const wxString& text) const;
};

} // namespace sakura_wx::ui
