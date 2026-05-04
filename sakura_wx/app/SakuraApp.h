#pragma once
#include <wx/wx.h>
#include <wx/cmdline.h>

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

    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    wxString  m_startupFile;
    wxLocale* m_locale = nullptr;
};

wxDECLARE_APP(SakuraApp);
