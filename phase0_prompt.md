# Phase 0: 環境構築・コードベース分析

## このフェーズのゴール

1. サクラエディタのソースコードを取得し、Win32依存箇所を定量的に把握する
2. wxWidgets の開発環境を macOS / Linux で構築する
3. プラットフォーム抽象化レイヤー（PAL）の設計を完了する

タスクは上から順番に実行すること。各タスク完了後に `docs/porting/progress.md` を更新すること。

---

## Task 0-1: リポジトリ取得とディレクトリ構造の把握

```bash
# 作業ディレクトリで実行
git clone https://github.com/sakura-editor/sakura.git
cd sakura
```

以下を実行して結果を `docs/porting/codebase_overview.md` に出力せよ：

1. ディレクトリ構成の確認（tree コマンド または find で2階層まで）
2. C++ファイルの総数とコード行数（find + wc）
3. 既存のビルドファイル一覧（*.sln, *.vcxproj, Makefile）
4. 既存テストの場所と構成（tests/ ディレクトリの内容）

出力形式：
```markdown
# コードベース概要

## ディレクトリ構成
...

## コード規模
- .cpp ファイル数: N
- .h ファイル数: N
- 総コード行数（概算）: N

## ビルドシステム
...

## テスト
...
```

---

## Task 0-2: Win32 API 依存箇所の自動抽出

以下のスクリプトを `tools/analyze_win32_deps.py` として作成・実行せよ。

スクリプトの要件：
- `sakura_core/` 以下の全 `.cpp` と `.h` を対象
- 以下のカテゴリ別に「ファイルパス:行番号:マッチ内容」を集計
- 結果を `docs/porting/win32_dependency_analysis.md` に出力

カテゴリ定義：
```python
CATEGORIES = {
    "window_management": [
        "HWND", "CreateWindow", "WndProc", "DefWindowProc",
        "WM_CREATE", "WM_PAINT", "WM_COMMAND", "WM_DESTROY",
        "WM_SIZE", "WM_CLOSE", "WM_KEYDOWN", "WM_CHAR",
        "RegisterClass", "ShowWindow", "UpdateWindow",
        "GetDC", "ReleaseDC", "BeginPaint", "EndPaint",
    ],
    "ime": [
        "ImmGetContext", "ImmSetCompositionWindow", "ImmReleaseContext",
        "WM_IME_STARTCOMPOSITION", "WM_IME_COMPOSITION", "WM_IME_ENDCOMPOSITION",
        "ImmGetCompositionString", "ImmSetCandidateWindow",
    ],
    "file_io": [
        "CreateFile", "ReadFile", "WriteFile", "CloseHandle",
        "GetFileSize", "SetFilePointer", "FlushFileBuffers",
        "CreateDirectory", "DeleteFile", "MoveFile", "CopyFile",
    ],
    "registry": [
        "RegOpenKey", "RegCreateKey", "RegQueryValue", "RegSetValue",
        "RegDeleteKey", "RegDeleteValue", "RegCloseKey",
        "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE",
    ],
    "ipc_shared_memory": [
        "CreateFileMapping", "MapViewOfFile", "OpenFileMapping",
        "UnmapViewOfFile", "DLLSHAREDATA",
    ],
    "gdi_drawing": [
        "HDC", "HFONT", "HBRUSH", "HPEN", "HBITMAP",
        "CreateFont", "SelectObject", "DeleteObject",
        "TextOut", "ExtTextOut", "DrawText",
        "BitBlt", "StretchBlt", "GetTextMetrics",
    ],
    "com_ole": [
        "CoCreateInstance", "CoInitialize", "IDispatch",
        "IUnknown", "BSTR", "VARIANT", "SAFEARRAY",
    ],
    "process_thread": [
        "CreateProcess", "CreateThread", "WaitForSingleObject",
        "CreateMutex", "CreateEvent", "OpenMutex",
    ],
    "clipboard": [
        "OpenClipboard", "CloseClipboard", "GetClipboardData",
        "SetClipboardData", "EmptyClipboard", "CF_TEXT", "CF_UNICODETEXT",
    ],
    "shell_integration": [
        "ShellExecute", "SHGetSpecialFolderPath", "SHBrowseForFolder",
        "GetOpenFileName", "GetSaveFileName",
    ],
}
```

スクリプトは以下の集計も出力すること：
- カテゴリ別：影響ファイル数、総出現回数
- 「ポーティング難易度スコア」（IME=5点, IPC=5点, GDI=4点, Window=4点, File=2点, Registry=2点, Clipboard=2点, Shell=2点, COM=3点, Process=3点）を各ファイルについて計算
- 難易度スコア上位20ファイルのリスト

---

## Task 0-3: wxWidgets 開発環境のセットアップ

現在の OS を自動検出して適切な手順でインストールせよ。

### macOS の場合
```bash
# Homebrew 経由でインストール
brew install wxwidgets cmake ninja pkg-config

# インストール確認
wx-config --version
wx-config --cxxflags | head -c 200
```

### Linux (Ubuntu/Debian) の場合
```bash
sudo apt update
sudo apt install -y \
    libwxgtk3.2-dev libwxgtk-media3.2-dev \
    cmake ninja-build pkg-config \
    libgtk-3-dev \
    build-essential git

# インストール確認
wx-config --version
```

### Linux (Fedora/RHEL) の場合
```bash
sudo dnf install -y \
    wxGTK-devel cmake ninja-build \
    gtk3-devel gcc-c++ git

wx-config --version
```

インストール後、以下の Hello World で動作確認せよ：

`tools/wx_hello_test/main.cpp` として作成して実行：
```cpp
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
        
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(TestApp);
```

`tools/wx_hello_test/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(wx_hello_test)
set(CMAKE_CXX_STANDARD 17)
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
add_executable(wx_hello_test main.cpp)
target_link_libraries(wx_hello_test ${wxWidgets_LIBRARIES})
```

ビルドして起動確認。スクリーンショットが撮れないので、ビルド成功のログを `docs/porting/env_setup.md` に記録せよ。

---

## Task 0-4: プラットフォーム抽象化レイヤー（PAL）の作成

`sakura_core/platform/` ディレクトリを新規作成し、以下のファイルを作成せよ。

### `sakura_core/platform/platform.h`

```cpp
/**
 * platform.h - サクラエディタ プラットフォーム抽象化レイヤー
 *
 * Win32 API への直接依存を排除し、wxWidgets / POSIX 実装に
 * 切り替えられるようにするための抽象化インターフェース。
 *
 * 使用方法:
 *   #include "platform/platform.h"
 *   // プラットフォーム非依存のコードを記述
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace SakuraPlatform {

// ============================================================
// 文字列型の統一
// ============================================================
// Windows でも Linux/macOS でも wchar_t ベースの文字列を使用
using NativeString = std::wstring;
using NativeStringView = std::wstring_view;

// ============================================================
// ファイル操作
// ============================================================
namespace File {
    /**
     * ファイルの内容をバイト配列として読み込む
     * @return 成功時 true、失敗時 false（エラーは GetLastError/errno で取得）
     */
    bool ReadBytes(const std::filesystem::path& path,
                   std::vector<char>& buffer);

    /**
     * バイト配列をファイルに書き込む（上書き）
     */
    bool WriteBytes(const std::filesystem::path& path,
                    const std::vector<char>& buffer);

    /**
     * ファイルが存在するか確認
     */
    bool Exists(const std::filesystem::path& path);

    /**
     * ファイルサイズを取得（バイト数）
     */
    std::uintmax_t GetSize(const std::filesystem::path& path);

    /**
     * 一時ファイルのパスを生成
     */
    std::filesystem::path GetTempFilePath(const std::wstring& prefix = L"sakura");
}

// ============================================================
// 設定ストア（レジストリ / INIファイル の抽象化）
// ============================================================
class IConfigStore {
public:
    virtual ~IConfigStore() = default;

    virtual bool GetString(NativeStringView key, NativeString& value) = 0;
    virtual bool SetString(NativeStringView key, NativeStringView value) = 0;
    virtual bool GetInt(NativeStringView key, int& value) = 0;
    virtual bool SetInt(NativeStringView key, int value) = 0;
    virtual bool GetBool(NativeStringView key, bool& value) = 0;
    virtual bool SetBool(NativeStringView key, bool value) = 0;
    virtual bool DeleteKey(NativeStringView key) = 0;
    virtual bool Flush() = 0;
};

/**
 * プラットフォームに応じた IConfigStore 実装を返す
 * Windows: レジストリ実装
 * 非Windows: INIファイル実装（~/.config/sakura/sakura.ini）
 */
std::unique_ptr<IConfigStore> CreateConfigStore(NativeStringView section);

// ============================================================
// クリップボード
// ============================================================
namespace Clipboard {
    bool HasText();
    bool GetText(NativeString& text);
    bool SetText(NativeStringView text);
    void Clear();
}

// ============================================================
// メッセージ・ダイアログ
// ============================================================
namespace Dialog {
    enum class MessageType { Info, Warning, Error, Question };

    void ShowMessage(const NativeString& message,
                     const NativeString& title = L"サクラエディタ",
                     MessageType type = MessageType::Info);

    bool ShowQuestion(const NativeString& message,
                      const NativeString& title = L"確認");

    /**
     * ファイルを開くダイアログ
     * @return 選択されたファイルパス（キャンセル時は空）
     */
    std::filesystem::path ShowOpenFileDialog(
        const NativeString& title = L"ファイルを開く",
        const NativeString& filter = L"すべてのファイル (*.*)|*.*");

    std::filesystem::path ShowSaveFileDialog(
        const NativeString& title = L"名前を付けて保存",
        const NativeString& filter = L"すべてのファイル (*.*)|*.*",
        const std::filesystem::path& defaultPath = {});
}

// ============================================================
// システム情報
// ============================================================
namespace System {
    /**
     * ユーザー設定ディレクトリを取得
     * Windows: %APPDATA%\sakura
     * macOS: ~/Library/Application Support/SakuraEditor
     * Linux: ~/.config/sakura
     */
    std::filesystem::path GetConfigDir();

    /**
     * アプリケーションの実行ファイルのディレクトリ
     */
    std::filesystem::path GetExecutableDir();

    /**
     * OS名を返す（ログ用）
     */
    std::string GetOSName();
}

// ============================================================
// IPC（プロセス間通信）
// ============================================================
/**
 * 共有メモリの抽象化
 * Windows: CreateFileMapping
 * 非Windows: POSIX shm_open
 */
class ISharedMemory {
public:
    virtual ~ISharedMemory() = default;
    virtual void* GetPtr() = 0;
    virtual const void* GetPtr() const = 0;
    virtual std::size_t GetSize() const = 0;
    virtual bool IsValid() const = 0;
};

std::unique_ptr<ISharedMemory> CreateSharedMemory(
    const std::string& name,
    std::size_t size,
    bool createNew = true);

} // namespace SakuraPlatform
```

### `sakura_core/platform/platform_impl_win32.cpp`

```cpp
/**
 * platform_impl_win32.cpp
 * Windows 向け PAL 実装
 * コンパイル条件: _WIN32 が定義されている場合のみ
 */
#ifdef _WIN32

#include "platform.h"
#include <windows.h>
#include <shlobj.h>
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
}

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
    ~ConfigStoreRegistry() { if (m_hKey) RegCloseKey(m_hKey); }

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
    bool HasText() { return IsClipboardFormatAvailable(CF_UNICODETEXT); }
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
}

// --- Dialog ---
namespace Dialog {
    void ShowMessage(const NativeString& msg, const NativeString& title, MessageType type) {
        UINT flags = MB_OK;
        switch (type) {
            case MessageType::Warning: flags |= MB_ICONWARNING; break;
            case MessageType::Error:   flags |= MB_ICONERROR;   break;
            case MessageType::Question: flags |= MB_ICONQUESTION; break;
            default: flags |= MB_ICONINFORMATION; break;
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
}

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
}

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
    ~SharedMemoryWin32() {
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
```

### `sakura_core/platform/platform_impl_wx.cpp`

```cpp
/**
 * platform_impl_wx.cpp
 * wxWidgets / POSIX 向け PAL 実装
 * コンパイル条件: _WIN32 が定義されていない場合
 */
#ifndef _WIN32

#include "platform.h"
#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
        // ランダムなサフィックスを付与
        auto name = std::wstring(prefix) + L"_XXXXXX";
        std::string narrow(name.begin(), name.end());
        auto full = tmp / narrow;
        char tmpl[PATH_MAX];
        strncpy(tmpl, full.string().c_str(), PATH_MAX);
        int fd = mkstemp(tmpl);
        if (fd >= 0) close(fd);
        return tmpl;
    }
}

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
        return m_cfg->Read(FullKey(key), &value);
    }
    bool SetInt(NativeStringView key, int value) override {
        return m_cfg->Write(FullKey(key), value);
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
    bool HasText() { return wxTheClipboard->IsSupported(wxDF_TEXT); }
    bool GetText(NativeString& text) {
        if (!wxTheClipboard->Open()) return false;
        bool ok = false;
        if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
            wxTextDataObject data;
            wxTheClipboard->GetData(data);
            text = data.GetText().ToStdWstring();
            ok = true;
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
}

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
            ? wxEmptyString : wxString(defaultPath.wstring());
        wxFileDialog dlg(nullptr, wxString(title), wxEmptyString, defPath,
                         wxString(filter), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() == wxID_OK)
            return std::filesystem::path(dlg.GetPath().ToStdWstring());
        return {};
    }
}

// --- System ---
namespace System {
    std::filesystem::path GetConfigDir() {
#if defined(__APPLE__)
        auto home = std::filesystem::path(getenv("HOME"));
        return home / "Library" / "Application Support" / "SakuraEditor";
#else
        const char* xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg) return std::filesystem::path(xdg) / "sakura";
        return std::filesystem::path(getenv("HOME")) / ".config" / "sakura";
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
}

// --- SharedMemory (POSIX) ---
class SharedMemoryPosix : public ISharedMemory {
    std::string m_name;
    void* m_ptr = MAP_FAILED;
    std::size_t m_size = 0;
    bool m_created;
public:
    SharedMemoryPosix(const std::string& name, std::size_t size, bool createNew)
        : m_name("/" + name), m_size(size), m_created(createNew) {
        int flags = O_RDWR;
        if (createNew) flags |= O_CREAT | O_TRUNC;
        int fd = shm_open(m_name.c_str(), flags, 0600);
        if (fd < 0) return;
        if (createNew) ftruncate(fd, static_cast<off_t>(size));
        m_ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (m_ptr == MAP_FAILED) m_ptr = nullptr;
    }
    ~SharedMemoryPosix() {
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
```

上記3ファイルを作成後、以下を確認せよ：
- Linux/macOS で `platform_impl_wx.cpp` だけがコンパイルされること
- Windows で `platform_impl_win32.cpp` だけがコンパイルされること
- `platform.h` の全インターフェースが両実装でカバーされていること

`docs/porting/progress.md` に Phase 0 完了の記録を追記せよ。

---

## Phase 0 完了チェックリスト

- [ ] `docs/porting/codebase_overview.md` が存在する
- [ ] `docs/porting/win32_dependency_analysis.md` が存在する（カテゴリ別集計、難易度スコア含む）
- [ ] `docs/porting/env_setup.md` に wxWidgets ビルド成功ログが記録されている
- [ ] `sakura_core/platform/platform.h` が存在する
- [ ] `sakura_core/platform/platform_impl_win32.cpp` が存在する
- [ ] `sakura_core/platform/platform_impl_wx.cpp` が存在する
- [ ] Hello World wxApp がビルドできる
- [ ] `docs/porting/progress.md` が更新されている

全チェック完了後、Phase 1 に進んでよい。
