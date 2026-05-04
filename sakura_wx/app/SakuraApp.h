#pragma once
#include <wx/wx.h>
#include <wx/cmdline.h>

#include <memory>

class SakuraConfig;
namespace sakura_wx::ipc { class SakuraSharedData; }

/**
 * SakuraApp - wxWidgets アプリケーションクラス
 *
 * wxApp のサブクラス。アプリケーションの初期化と
 * メインウィンドウの生成、設定（SakuraConfig）と
 * プロセス間共有データ（SakuraSharedData）の所有も担当する。
 */
class SakuraApp : public wxApp {
public:
    bool OnInit() override;
    int  OnExit() override;

    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

    SakuraConfig*                       Config()     { return m_config.get(); }
    sakura_wx::ipc::SakuraSharedData*   SharedData() { return m_shared.get(); }

private:
    wxString    m_startupFile;
    wxLocale*   m_locale = nullptr;

    std::unique_ptr<SakuraConfig>                       m_config;
    std::unique_ptr<sakura_wx::ipc::SakuraSharedData>   m_shared;
};

wxDECLARE_APP(SakuraApp);
