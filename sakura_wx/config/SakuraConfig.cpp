#include "SakuraConfig.h"

#include <wx/fontutil.h>

namespace {
constexpr const wchar_t* kSection = L"general";

// 既定フォント名（プラットフォーム別）
wxString DefaultFontFace() {
#if defined(__APPLE__)
    return wxT("Menlo");
#elif defined(__WXGTK__)
    return wxT("Noto Sans Mono CJK JP");
#else
    return wxT("Monospace");
#endif
}
} // namespace

SakuraConfig::SakuraConfig()
    : m_store(SakuraPlatform::CreateConfigStore(kSection))
    , m_fontFace(DefaultFontFace())
{
}

SakuraConfig::~SakuraConfig() = default;

void SakuraConfig::Load() {
    if (!m_store) return;

    SakuraPlatform::NativeString s;
    if (m_store->GetString(L"font_face", s) && !s.empty()) {
        m_fontFace = wxString(s);
    }
    int v = 0;
    if (m_store->GetInt(L"font_size", v) && v > 0)        m_fontSize = v;
    if (m_store->GetInt(L"tab_width", v) && v > 0)        m_tabWidth = v;
    bool b = false;
    if (m_store->GetBool(L"show_line_numbers", b))        m_showLineNumbers = b;
    if (m_store->GetBool(L"word_wrap", b))                m_wordWrap = b;

    int x = 0, y = 0, w = 0, h = 0;
    if (m_store->GetInt(L"window_x", x) && m_store->GetInt(L"window_y", y)) {
        m_windowPos = wxPoint(x, y);
    }
    if (m_store->GetInt(L"window_w", w) && w > 0 &&
        m_store->GetInt(L"window_h", h) && h > 0) {
        m_windowSize = wxSize(w, h);
    }

    // 最近使ったファイル
    m_recentFiles.Clear();
    for (int i = 0; i < kMaxRecentFiles; ++i) {
        SakuraPlatform::NativeString recent;
        std::wstring key = L"recent_" + std::to_wstring(i);
        if (m_store->GetString(key, recent) && !recent.empty()) {
            m_recentFiles.Add(wxString(recent));
        } else {
            break;
        }
    }
}

void SakuraConfig::Save() {
    if (!m_store) return;

    m_store->SetString(L"font_face", m_fontFace.ToStdWstring());
    m_store->SetInt(L"font_size", m_fontSize);
    m_store->SetInt(L"tab_width", m_tabWidth);
    m_store->SetBool(L"show_line_numbers", m_showLineNumbers);
    m_store->SetBool(L"word_wrap", m_wordWrap);

    if (m_windowPos != wxDefaultPosition) {
        m_store->SetInt(L"window_x", m_windowPos.x);
        m_store->SetInt(L"window_y", m_windowPos.y);
    }
    m_store->SetInt(L"window_w", m_windowSize.GetWidth());
    m_store->SetInt(L"window_h", m_windowSize.GetHeight());

    // 最近使ったファイル
    for (int i = 0; i < kMaxRecentFiles; ++i) {
        std::wstring key = L"recent_" + std::to_wstring(i);
        if (i < static_cast<int>(m_recentFiles.GetCount())) {
            m_store->SetString(key, m_recentFiles[i].ToStdWstring());
        } else {
            m_store->DeleteKey(key);
        }
    }

    m_store->Flush();
}

wxFont SakuraConfig::GetEditorFont() const {
    wxFont f(m_fontSize, wxFONTFAMILY_TELETYPE,
             wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
             /*underlined=*/false, m_fontFace);
    if (!f.IsOk()) {
        f = wxFont(m_fontSize, wxFONTFAMILY_TELETYPE,
                   wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    }
    return f;
}

void SakuraConfig::SetEditorFont(const wxFont& font) {
    if (!font.IsOk()) return;
    m_fontFace = font.GetFaceName();
    if (m_fontFace.IsEmpty()) m_fontFace = DefaultFontFace();
    m_fontSize = font.GetPointSize();
    if (m_fontSize <= 0) m_fontSize = 12;
}

void SakuraConfig::AddRecentFile(const wxString& path) {
    if (path.IsEmpty()) return;

    // 重複排除（同じパスがあれば一度削除して先頭に積む）
    int idx = m_recentFiles.Index(path);
    if (idx != wxNOT_FOUND) {
        m_recentFiles.RemoveAt(idx);
    }
    m_recentFiles.Insert(path, 0);

    while (m_recentFiles.GetCount() > kMaxRecentFiles) {
        m_recentFiles.RemoveAt(m_recentFiles.GetCount() - 1);
    }
}

void SakuraConfig::ClearRecentFiles() {
    m_recentFiles.Clear();
}

// === タイプ別シンタックス色設定 ===
namespace {
std::wstring SynKey(const wxString& type, const wxString& kind) {
    return L"syn_" + type.ToStdWstring() + L"_" + kind.ToStdWstring();
}
} // namespace

wxColour SakuraConfig::GetSyntaxColor(const wxString& type,
                                      const wxString& kind) const {
    if (!m_store) return wxNullColour;
    SakuraPlatform::NativeString s;
    if (!m_store->GetString(SynKey(type, kind), s) || s.empty()) {
        return wxNullColour;
    }
    // フォーマット: "R,G,B"
    wxString v(s);
    wxArrayString parts = wxSplit(v, ',');
    if (parts.GetCount() != 3) return wxNullColour;
    long r = 0, g = 0, b = 0;
    parts[0].ToLong(&r);
    parts[1].ToLong(&g);
    parts[2].ToLong(&b);
    return wxColour(static_cast<unsigned char>(r),
                    static_cast<unsigned char>(g),
                    static_cast<unsigned char>(b));
}

void SakuraConfig::SetSyntaxColor(const wxString& type, const wxString& kind,
                                  const wxColour& c) {
    if (!m_store) return;
    if (!c.IsOk()) {
        m_store->DeleteKey(SynKey(type, kind));
        return;
    }
    wxString s = wxString::Format(wxT("%d,%d,%d"),
                                  c.Red(), c.Green(), c.Blue());
    m_store->SetString(SynKey(type, kind), s.ToStdWstring());
}

void SakuraConfig::ClearSyntaxColors() {
    if (!m_store) return;
    static const wxString types[]  = {wxT("cpp"), wxT("py"),
                                      wxT("md"), wxT("default")};
    static const wxString kinds[]  = {kSynKindComment, kSynKindString,
                                      kSynKindKeyword, kSynKindNumber};
    for (auto& t : types) {
        for (auto& k : kinds) {
            m_store->DeleteKey(SynKey(t, k));
        }
    }
}

// === メタテーブル汎用ストア (Phase 12) ===
namespace {
std::wstring MetaKey(const wxString& tab, const wxString& field) {
    // 名前空間: meta_<tab>_<field>。既存キー (font_face, syn_*) と衝突しない。
    return L"meta_" + tab.ToStdWstring() + L"_" + field.ToStdWstring();
}
} // namespace

bool SakuraConfig::GetMetaBool(const wxString& tab, const wxString& field,
                               bool def) const {
    if (!m_store) return def;
    bool v = def;
    return m_store->GetBool(MetaKey(tab, field), v) ? v : def;
}

void SakuraConfig::SetMetaBool(const wxString& tab, const wxString& field,
                               bool value) {
    if (!m_store) return;
    m_store->SetBool(MetaKey(tab, field), value);
}

int SakuraConfig::GetMetaInt(const wxString& tab, const wxString& field,
                             int def) const {
    if (!m_store) return def;
    int v = def;
    return m_store->GetInt(MetaKey(tab, field), v) ? v : def;
}

void SakuraConfig::SetMetaInt(const wxString& tab, const wxString& field,
                              int value) {
    if (!m_store) return;
    m_store->SetInt(MetaKey(tab, field), value);
}
