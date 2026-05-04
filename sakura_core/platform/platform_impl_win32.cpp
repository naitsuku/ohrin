/**
 * platform_impl_win32.cpp
 * Windows 向け PAL 実装
 * コンパイル条件: _WIN32 が定義されている場合のみ
 */
#ifdef _WIN32

#include "platform.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cassert>

namespace SakuraPlatform {

// --- File ---
namespace File {
    bool ReadBytes(const std::filesystem::path& path, std::vector<char>& buffer) {
        HANDLE hFile = CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER size;
        if (!GetFileSizeEx(hFile, &size)) { CloseHandle(hFile); return false; }

        buffer.resize(static_cast<size_t>(size.QuadPart));
        DWORD bytesRead = 0;
        bool ok = ReadFile(hFile, buffer.data(),
                           static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)
                  && bytesRead == buffer.size();
        CloseHandle(hFile);
        return ok;
    }

    bool WriteBytes(const std::filesystem::path& path, const std::vector<char>& buffer) {
        HANDLE hFile = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        DWORD written = 0;
        bool ok = WriteFile(hFile, buffer.data(),
                            static_cast<DWORD>(buffer.size()), &written, nullptr)
                  && written == buffer.size();
        CloseHandle(hFile);
        return ok;
    }

    bool Exists(const std::filesystem::path& path) {
        return std::filesystem::exists(path);
    }

    std::uintmax_t GetSize(const std::filesystem::path& path) {
        return std::filesystem::file_size(path);
    }

    std::filesystem::path GetTempFilePath(const std::wstring& prefix) {
        wchar_t tmpDir[MAX_PATH], tmpFile[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpDir);
        GetTempFileNameW(tmpDir, prefix.c_str(), 0, tmpFile);
        return tmpFile;
    }
} // namespace File

// --- ConfigStore (Registry) ---
class ConfigStoreRegistry : public IConfigStore {
    std::wstring m_section;
    HKEY m_hKey = nullptr;
public:
    explicit ConfigStoreRegistry(NativeStringView section) : m_section(section) {
        std::wstring keyPath = L"Software\\sakura-editor\\" + m_section;
        RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr,
                        0, KEY_READ | KEY_WRITE, nullptr, &m_hKey, nullptr);
    }
    ~ConfigStoreRegistry() override { if (m_hKey) RegCloseKey(m_hKey); }

    bool GetString(NativeStringView key, NativeString& value) override {
        wchar_t buf[4096]; DWORD size = sizeof(buf); DWORD type;
        std::wstring k(key);
        if (RegQueryValueExW(m_hKey, k.c_str(), nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
            value = buf; return true;
        }
        return false;
    }
    bool SetString(NativeStringView key, NativeStringView val) override {
        std::wstring k(key), v(val);
        return RegSetValueExW(m_hKey, k.c_str(), 0, REG_SZ,
            reinterpret_cast<const BYTE*>(v.c_str()),
            static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    bool GetInt(NativeStringView key, int& value) override {
        DWORD data; DWORD size = sizeof(data); DWORD type;
        std::wstring k(key);
        if (RegQueryValueExW(m_hKey, k.c_str(), nullptr, &type,
                             reinterpret_cast<LPBYTE>(&data), &size) == ERROR_SUCCESS) {
            value = static_cast<int>(data); return true;
        }
        return false;
    }
    bool SetInt(NativeStringView key, int value) override {
        DWORD data = static_cast<DWORD>(value);
        std::wstring k(key);
        return RegSetValueExW(m_hKey, k.c_str(), 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&data), sizeof(data)) == ERROR_SUCCESS;
    }
    bool GetBool(NativeStringView key, bool& value) override {
        int i; if (GetInt(key, i)) { value = (i != 0); return true; } return false;
    }
    bool SetBool(NativeStringView key, bool value) override {
        return SetInt(key, value ? 1 : 0);
    }
    bool DeleteKey(NativeStringView key) override {
        std::wstring k(key);
        return RegDeleteValueW(m_hKey, k.c_str()) == ERROR_SUCCESS;
    }
    bool Flush() override { return RegFlushKey(m_hKey) == ERROR_SUCCESS; }
};

std::unique_ptr<IConfigStore> CreateConfigStore(NativeStringView section) {
    return std::make_unique<ConfigStoreRegistry>(section);
}

// --- Clipboard ---
namespace Clipboard {
    bool HasText() { return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE; }
    bool GetText(NativeString& text) {
        if (!OpenClipboard(nullptr)) return false;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        bool ok = false;
        if (h) {
            auto* p = static_cast<wchar_t*>(GlobalLock(h));
            if (p) { text = p; GlobalUnlock(h); ok = true; }
        }
        CloseClipboard();
        return ok;
    }
    bool SetText(NativeStringView text) {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!h) { CloseClipboard(); return false; }
        auto* p = static_cast<wchar_t*>(GlobalLock(h));
        std::copy(text.begin(), text.end(), p);
        p[text.size()] = L'\0';
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
        CloseClipboard();
        return true;
    }
    void Clear() { if (OpenClipboard(nullptr)) { EmptyClipboard(); CloseClipboard(); } }
} // namespace Clipboard

// --- Dialog ---
namespace Dialog {
    void ShowMessage(const NativeString& msg, const NativeString& title, MessageType type) {
        UINT flags = MB_OK;
        switch (type) {
            case MessageType::Warning:  flags |= MB_ICONWARNING;     break;
            case MessageType::Error:    flags |= MB_ICONERROR;       break;
            case MessageType::Question: flags |= MB_ICONQUESTION;    break;
            default:                    flags |= MB_ICONINFORMATION; break;
        }
        MessageBoxW(nullptr, msg.c_str(), title.c_str(), flags);
    }
    bool ShowQuestion(const NativeString& msg, const NativeString& title) {
        return MessageBoxW(nullptr, msg.c_str(), title.c_str(),
                           MB_YESNO | MB_ICONQUESTION) == IDYES;
    }
    std::filesystem::path ShowOpenFileDialog(const NativeString& title,
                                              const NativeString& filter) {
        wchar_t buf[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = buf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = title.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) return buf;
        return {};
    }
    std::filesystem::path ShowSaveFileDialog(const NativeString& title,
                                              const NativeString& filter,
                                              const std::filesystem::path& defaultPath) {
        wchar_t buf[MAX_PATH] = {};
        if (!defaultPath.empty()) {
            auto s = defaultPath.wstring();
            std::copy(s.begin(), s.end(), buf);
        }
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = buf;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = title.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (GetSaveFileNameW(&ofn)) return buf;
        return {};
    }
} // namespace Dialog

// --- System ---
namespace System {
    std::filesystem::path GetConfigDir() {
        wchar_t buf[MAX_PATH];
        SHGetSpecialFolderPathW(nullptr, buf, CSIDL_APPDATA, TRUE);
        return std::filesystem::path(buf) / L"sakura";
    }
    std::filesystem::path GetExecutableDir() {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::filesystem::path(buf).parent_path();
    }
    std::string GetOSName() { return "Windows"; }
} // namespace System

// --- SharedMemory ---
class SharedMemoryWin32 : public ISharedMemory {
    HANDLE m_hMap = nullptr;
    void* m_ptr = nullptr;
    std::size_t m_size = 0;
public:
    SharedMemoryWin32(const std::string& name, std::size_t size, bool createNew) : m_size(size) {
        std::wstring wname(name.begin(), name.end());
        if (createNew) {
            m_hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0, static_cast<DWORD>(size),
                                        wname.c_str());
        } else {
            m_hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
        }
        if (m_hMap) m_ptr = MapViewOfFile(m_hMap, FILE_MAP_ALL_ACCESS, 0, 0, size);
    }
    ~SharedMemoryWin32() override {
        if (m_ptr) UnmapViewOfFile(m_ptr);
        if (m_hMap) CloseHandle(m_hMap);
    }
    void* GetPtr() override { return m_ptr; }
    const void* GetPtr() const override { return m_ptr; }
    std::size_t GetSize() const override { return m_size; }
    bool IsValid() const override { return m_ptr != nullptr; }
};

std::unique_ptr<ISharedMemory> CreateSharedMemory(const std::string& name,
                                                   std::size_t size, bool createNew) {
    return std::make_unique<SharedMemoryWin32>(name, size, createNew);
}

} // namespace SakuraPlatform

#endif // _WIN32
