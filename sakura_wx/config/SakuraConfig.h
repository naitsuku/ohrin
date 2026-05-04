#pragma once
#include "platform/platform.h"

#include <wx/arrstr.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/string.h>

#include <memory>

/**
 * SakuraConfig — wxWidgets 版サクラエディタの設定管理クラス。
 *
 * PAL の IConfigStore を使って設定を読み書きする。
 * 設定ファイルの場所:
 *   macOS: ~/Library/Application Support/SakuraEditor/sakura.ini
 *   Linux: ~/.config/sakura/sakura.ini
 *
 * 値はメンバ変数にキャッシュされ、Save() でストアに書き戻される。
 */
class SakuraConfig {
public:
    SakuraConfig();
    ~SakuraConfig();

    // ストアからの読み込み／書き戻し
    void Load();
    void Save();

    // --- フォント ---
    wxFont GetEditorFont() const;
    void   SetEditorFont(const wxFont& font);

    // --- 表示 ---
    bool GetShowLineNumbers() const     { return m_showLineNumbers; }
    void SetShowLineNumbers(bool v)     { m_showLineNumbers = v; }

    bool GetWordWrap() const            { return m_wordWrap; }
    void SetWordWrap(bool v)            { m_wordWrap = v; }

    int  GetTabWidth() const            { return m_tabWidth; }
    void SetTabWidth(int v)             { m_tabWidth = v; }

    // --- ウィンドウ位置・サイズ ---
    wxPoint GetWindowPosition() const   { return m_windowPos; }
    void    SetWindowPosition(wxPoint p){ m_windowPos = p; }

    wxSize  GetWindowSize() const       { return m_windowSize; }
    void    SetWindowSize(wxSize s)     { m_windowSize = s; }

    // --- 最近使ったファイル ---
    wxArrayString GetRecentFiles() const { return m_recentFiles; }
    void          AddRecentFile(const wxString& path);
    void          ClearRecentFiles();

    static constexpr int kMaxRecentFiles = 10;

private:
    std::unique_ptr<SakuraPlatform::IConfigStore> m_store;

    // フォント
    wxString m_fontFace;
    int      m_fontSize       = 12;

    // 表示
    bool     m_showLineNumbers = true;
    bool     m_wordWrap        = false;
    int      m_tabWidth        = 4;

    // ウィンドウ
    wxPoint  m_windowPos       = wxDefaultPosition;
    wxSize   m_windowSize      = wxSize(900, 700);

    // 最近使ったファイル
    wxArrayString m_recentFiles;
};
