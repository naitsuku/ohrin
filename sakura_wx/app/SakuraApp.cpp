#include "SakuraApp.h"
#include "config/SakuraConfig.h"
#include "ipc/SakuraSharedData.h"
#include "window/CEditWndWx.h"

#include <wx/filename.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/log.h>

wxIMPLEMENT_APP(SakuraApp);

namespace {
const wxCmdLineEntryDesc kCmdLineDesc[] = {
    { wxCMD_LINE_SWITCH, "h", "help",
      "ヘルプを表示", wxCMD_LINE_VAL_NONE,
      wxCMD_LINE_OPTION_HELP | wxCMD_LINE_PARAM_OPTIONAL },
    { wxCMD_LINE_PARAM, nullptr, nullptr,
      "起動時に開くファイル", wxCMD_LINE_VAL_STRING,
      wxCMD_LINE_PARAM_OPTIONAL | wxCMD_LINE_PARAM_MULTIPLE },
    wxCMD_LINE_DESC_END
};
} // anonymous namespace

void SakuraApp::OnInitCmdLine(wxCmdLineParser& parser) {
    parser.SetDesc(kCmdLineDesc);
    parser.SetSwitchChars(wxT("-"));
}

bool SakuraApp::OnCmdLineParsed(wxCmdLineParser& parser) {
    if (parser.GetParamCount() > 0) {
        wxString arg = parser.GetParam(0);
        if (wxFileName::FileExists(arg)) {
            m_startupFile = arg;
        } else {
            wxLogWarning(wxT("ファイルが見つかりません（無視）: %s"), arg);
        }
    }
    return true;
}

bool SakuraApp::OnInit() {
    if (!wxApp::OnInit()) return false;

    wxInitAllImageHandlers();

    m_locale = new wxLocale(wxLANGUAGE_JAPANESE, wxLOCALE_DONT_LOAD_DEFAULT);
    if (!m_locale->IsOk()) {
        delete m_locale;
        m_locale = nullptr;
    }

    // 設定の読み込み
    m_config = std::make_unique<SakuraConfig>();
    m_config->Load();

    // プロセス間共有データの初期化
    m_shared = std::make_unique<sakura_wx::ipc::SakuraSharedData>();
    if (!m_shared->Open()) {
        wxLogWarning(wxT("共有メモリの初期化に失敗しました（無視して続行）"));
    }

    auto* frame = new CEditWndWx(m_startupFile);
    frame->Show(true);
    SetTopWindow(frame);

    return true;
}

int SakuraApp::OnExit() {
    if (m_config) {
        m_config->Save();
        m_config.reset();
    }
    if (m_shared) {
        m_shared->Close();
        m_shared.reset();
    }
    delete m_locale;
    m_locale = nullptr;
    return wxApp::OnExit();
}
