/**
 * platform_impl_wx.cpp
 * wxWidgets / POSIX 向け PAL 実装
 * コンパイル条件: _WIN32 が定義されていない場合
 */
#ifndef _WIN32

#include "platform.h"

#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/fileconf.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>

namespace SakuraPlatform {

// --- File ---
namespace File {
    bool ReadBytes(const std::filesystem::path& path, std::vector<char>& buffer) {
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs) return false;
        auto size = ifs.tellg();
        ifs.seekg(0);
        buffer.resize(static_cast<size_t>(size));
        return static_cast<bool>(ifs.read(buffer.data(), size));
    }
    bool WriteBytes(const std::filesystem::path& path, const std::vector<char>& buffer) {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        return static_cast<bool>(ofs.write(buffer.data(), buffer.size()));
    }
    bool Exists(const std::filesystem::path& path) {
        return std::filesystem::exists(path);
    }
    std::uintmax_t GetSize(const std::filesystem::path& path) {
        return std::filesystem::file_size(path);
    }
    std::filesystem::path GetTempFilePath(const std::wstring& prefix) {
        auto tmp = std::filesystem::temp_directory_path();
        std::string narrow_prefix(prefix.begin(), prefix.end());
        auto tmplPath = tmp / (narrow_prefix + "_XXXXXX");
        std::string s = tmplPath.string();
        std::vector<char> tmpl(s.begin(), s.end());
        tmpl.push_back('\0');
        int fd = mkstemp(tmpl.data());
        if (fd >= 0) {
            close(fd);
            return std::filesystem::path(tmpl.data());
        }
        return {};
    }
} // namespace File

// --- ConfigStore (wxFileConfig / INI) ---
class ConfigStoreIni : public IConfigStore {
    std::unique_ptr<wxFileConfig> m_cfg;
    wxString m_section;
public:
    explicit ConfigStoreIni(NativeStringView section) {
        auto cfgDir = System::GetConfigDir();
        std::filesystem::create_directories(cfgDir);
        auto cfgPath = cfgDir / "sakura.ini";
        m_cfg = std::make_unique<wxFileConfig>(
            wxT("sakura"), wxEmptyString,
            wxString(cfgPath.string()),
            wxEmptyString,
            wxCONFIG_USE_LOCAL_FILE);
        m_section = wxString(std::wstring(section));
    }

    wxString FullKey(NativeStringView key) const {
        return wxT("/") + m_section + wxT("/") + wxString(std::wstring(key));
    }

    bool GetString(NativeStringView key, NativeString& value) override {
        wxString v;
        if (m_cfg->Read(FullKey(key), &v)) {
            value = v.ToStdWstring(); return true;
        }
        return false;
    }
    bool SetString(NativeStringView key, NativeStringView val) override {
        return m_cfg->Write(FullKey(key), wxString(std::wstring(val)));
    }
    bool GetInt(NativeStringView key, int& value) override {
        long tmp = 0;
        if (m_cfg->Read(FullKey(key), &tmp)) {
            value = static_cast<int>(tmp);
            return true;
        }
        return false;
    }
    bool SetInt(NativeStringView key, int value) override {
        return m_cfg->Write(FullKey(key), static_cast<long>(value));
    }
    bool GetBool(NativeStringView key, bool& value) override {
        return m_cfg->Read(FullKey(key), &value);
    }
    bool SetBool(NativeStringView key, bool value) override {
        return m_cfg->Write(FullKey(key), value);
    }
    bool DeleteKey(NativeStringView key) override {
        return m_cfg->DeleteEntry(FullKey(key));
    }
    bool Flush() override { return m_cfg->Flush(); }
};

std::unique_ptr<IConfigStore> CreateConfigStore(NativeStringView section) {
    return std::make_unique<ConfigStoreIni>(section);
}

// --- Clipboard (wxClipboard) ---
namespace Clipboard {
    bool HasText() {
        if (!wxTheClipboard->Open()) return false;
        bool has = wxTheClipboard->IsSupported(wxDF_TEXT)
                || wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
        wxTheClipboard->Close();
        return has;
    }
    bool GetText(NativeString& text) {
        if (!wxTheClipboard->Open()) return false;
        bool ok = false;
        if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT)
            || wxTheClipboard->IsSupported(wxDF_TEXT)) {
            wxTextDataObject data;
            if (wxTheClipboard->GetData(data)) {
                text = data.GetText().ToStdWstring();
                ok = true;
            }
        }
        wxTheClipboard->Close();
        return ok;
    }
    bool SetText(NativeStringView text) {
        if (!wxTheClipboard->Open()) return false;
        wxTheClipboard->SetData(new wxTextDataObject(wxString(std::wstring(text))));
        wxTheClipboard->Close();
        return true;
    }
    void Clear() {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->Clear();
            wxTheClipboard->Close();
        }
    }
} // namespace Clipboard

// --- Dialog (wxWidgets ダイアログ) ---
namespace Dialog {
    void ShowMessage(const NativeString& msg, const NativeString& title, MessageType type) {
        long style = wxOK;
        switch (type) {
            case MessageType::Warning:  style |= wxICON_WARNING;     break;
            case MessageType::Error:    style |= wxICON_ERROR;       break;
            case MessageType::Question: style |= wxICON_QUESTION;    break;
            default:                    style |= wxICON_INFORMATION; break;
        }
        wxMessageBox(wxString(msg), wxString(title), style);
    }
    bool ShowQuestion(const NativeString& msg, const NativeString& title) {
        return wxMessageBox(wxString(msg), wxString(title),
                            wxYES_NO | wxICON_QUESTION) == wxYES;
    }
    std::filesystem::path ShowOpenFileDialog(const NativeString& title,
                                              const NativeString& filter) {
        wxFileDialog dlg(nullptr, wxString(title), wxEmptyString, wxEmptyString,
                         wxString(filter), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK)
            return std::filesystem::path(dlg.GetPath().ToStdWstring());
        return {};
    }
    std::filesystem::path ShowSaveFileDialog(const NativeString& title,
                                              const NativeString& filter,
                                              const std::filesystem::path& defaultPath) {
        wxString defPath = defaultPath.empty()
            ? wxString(wxEmptyString)
            : wxString(defaultPath.wstring());
        wxFileDialog dlg(nullptr, wxString(title), wxEmptyString, defPath,
                         wxString(filter), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() == wxID_OK)
            return std::filesystem::path(dlg.GetPath().ToStdWstring());
        return {};
    }
} // namespace Dialog

// --- System ---
namespace System {
    std::filesystem::path GetConfigDir() {
#if defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (!home) home = "/tmp";
        return std::filesystem::path(home) / "Library" / "Application Support" / "SakuraEditor";
#else
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg) return std::filesystem::path(xdg) / "sakura";
        const char* home = std::getenv("HOME");
        if (!home) home = "/tmp";
        return std::filesystem::path(home) / ".config" / "sakura";
#endif
    }
    std::filesystem::path GetExecutableDir() {
        return std::filesystem::path(
            wxStandardPaths::Get().GetExecutablePath().ToStdString()).parent_path();
    }
    std::string GetOSName() {
#if defined(__APPLE__)
        return "macOS";
#else
        return "Linux";
#endif
    }
} // namespace System

// --- SharedMemory (POSIX) ---
class SharedMemoryPosix : public ISharedMemory {
    std::string m_name;
    void* m_ptr = nullptr;
    std::size_t m_size = 0;
    bool m_created;
public:
    SharedMemoryPosix(const std::string& name, std::size_t size, bool createNew)
        : m_name("/" + name), m_size(size), m_created(createNew) {
        int flags = O_RDWR;
        if (createNew) flags |= O_CREAT | O_TRUNC;
        int fd = shm_open(m_name.c_str(), flags, 0600);
        if (fd < 0) return;
        if (createNew) {
            if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
                close(fd);
                return;
            }
        }
        void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p != MAP_FAILED) m_ptr = p;
    }
    ~SharedMemoryPosix() override {
        if (m_ptr) munmap(m_ptr, m_size);
        if (m_created) shm_unlink(m_name.c_str());
    }
    void* GetPtr() override { return m_ptr; }
    const void* GetPtr() const override { return m_ptr; }
    std::size_t GetSize() const override { return m_size; }
    bool IsValid() const override { return m_ptr != nullptr; }
};

std::unique_ptr<ISharedMemory> CreateSharedMemory(const std::string& name,
                                                   std::size_t size, bool createNew) {
    return std::make_unique<SharedMemoryPosix>(name, size, createNew);
}

} // namespace SakuraPlatform

#endif // !_WIN32
