#include <wx/wx.h>

class TestApp : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY,
            wxT("wxWidgets 動作確認 - サクラエディタ移植プロジェクト"),
            wxDefaultPosition, wxSize(600, 400));

        auto* text = new wxTextCtrl(frame, wxID_ANY,
            wxT("wxWidgets が正常に動作しています。\n日本語テキストの表示確認。\nSakura Editor porting project."),
            wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE);
        (void)text;

        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(TestApp);
