# Phase 1: CMake ビルドシステムと sakura_core の段階的ポータブル化

## 前提条件
- Phase 0 が完了していること（`docs/porting/progress.md` で確認）
- `sakura_core/platform/` の PAL ファイルが存在すること

## このフェーズのゴール
1. CMake でクロスプラットフォームビルドを構成する
2. `sakura_core` の Win32 依存度が低いモジュールを Linux/macOS でコンパイルできるようにする
3. 既存テスト（tests/unittests）が非Windows でもパスするようにする

---

## Task 1-1: ルート CMakeLists.txt の作成

リポジトリルートに `CMakeLists.txt` を新規作成せよ。
既存の `*.sln` や `*.vcxproj` は**一切変更しない**こと。

```cmake
cmake_minimum_required(VERSION 3.20)
project(SakuraEditor VERSION 2.5.0 LANGUAGES CXX)

# ============================================================
# グローバル設定
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ビルドタイプのデフォルト
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 出力ディレクトリ
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================
# プラットフォーム検出
# ============================================================
if(WIN32)
    add_compile_definitions(SAKURA_PLATFORM_WIN32)
    message(STATUS "Platform: Windows")
elseif(APPLE)
    add_compile_definitions(SAKURA_PLATFORM_MACOS SAKURA_PLATFORM_WX)
    message(STATUS "Platform: macOS")
else()
    add_compile_definitions(SAKURA_PLATFORM_LINUX SAKURA_PLATFORM_WX)
    message(STATUS "Platform: Linux")
endif()

# ============================================================
# 依存ライブラリの検索
# ============================================================
# wxWidgets (非Windows)
if(NOT WIN32)
    find_package(wxWidgets REQUIRED
        COMPONENTS core base aui stc xml net)
    include(${wxWidgets_USE_FILE})
    message(STATUS "wxWidgets found: ${wxWidgets_VERSION}")
endif()

# Google Test (テスト用)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

# ============================================================
# サブプロジェクト
# ============================================================
add_subdirectory(sakura_core)

if(NOT WIN32)
    add_subdirectory(sakura_wx)
endif()

# テスト
enable_testing()
add_subdirectory(tests/wx_tests)
```

---

## Task 1-2: sakura_core/CMakeLists.txt の作成

`sakura_core/CMakeLists.txt` を新規作成せよ。

方針：
- まず「確実にコンパイルできる」ファイルだけを対象にする
- Win32依存が強いファイルは除外リストに入れて後で対応
- コンパイルエラーが出たファイルは除外して `docs/porting/excluded_files.md` に記録

```cmake
# sakura_core/CMakeLists.txt

# ============================================================
# ソースファイルの収集
# ============================================================
# Win32 強依存のため現時点では除外するファイル
# （Phase 2以降で段階的に対応）
set(EXCLUDED_SOURCES_NONWIN
    # ウィンドウ管理（完全Win32依存）
    window/CEditWnd.cpp
    window/CTabWnd.cpp
    window/CSplitterWnd.cpp
    # GDIを直接使うビューレイヤー
    view/CEditView.cpp
    view/CEditView_Paint.cpp
    view/CEditView_Mouse.cpp
    view/CEditView_Paint_Bracket.cpp
    view/CTextDrawer.cpp
    view/CCaret.cpp
    # IPC（Phase 3で対応）
    env/CShareData.cpp
    env/CDllShareData.cpp
    # マクロシステム（COM依存）
    macro/CMacroMgr.cpp
    macro/CSMacroMgr.cpp
    # ダイアログ群（Phase 4で対応）
    dlg/CDlgSearch.cpp
    dlg/CDlgReplace.cpp
    dlg/CDlgGrep.cpp
    dlg/CDlgAbout.cpp
    # Shell連携
    util/shell.cpp
)

# 全ソースを収集してから除外リストを引く
file(GLOB_RECURSE ALL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT WIN32)
    # 非Windows: 除外リストを適用
    foreach(excluded ${EXCLUDED_SOURCES_NONWIN})
        list(REMOVE_ITEM ALL_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${excluded}")
    endforeach()
    
    # PAL実装の追加
    list(APPEND ALL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform_impl_wx.cpp")
else()
    # Windows: PAL実装の追加
    list(APPEND ALL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform_impl_win32.cpp")
endif()

# ============================================================
# ライブラリターゲット
# ============================================================
add_library(sakura_core STATIC ${ALL_SOURCES})

target_include_directories(sakura_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# コンパイル定義
target_compile_definitions(sakura_core PUBLIC
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# wxWidgets リンク（非Windows）
if(NOT WIN32)
    target_link_libraries(sakura_core PUBLIC ${wxWidgets_LIBRARIES})
    target_compile_options(sakura_core PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -Wno-deprecated-declarations)
endif()

# ============================================================
# コンパイルエラー対応スクリプト
# ============================================================
# ビルドが失敗したら以下を実行してエラーを確認:
# cmake --build build 2>&1 | grep "error:" | grep -o "[^/]*\.cpp" | sort -u
# 該当ファイルを EXCLUDED_SOURCES_NONWIN に追加して再試行
```

実際にビルドを試み、エラーが出たファイルを EXCLUDED_SOURCES_NONWIN に追加しながら
「少なくとも sakura_core がコンパイルできる」状態を達成すること。
除外したファイルは `docs/porting/excluded_files.md` に理由とともに記録せよ。

---

## Task 1-3: sakura_core のコンパイルエラー修正（安全な変更のみ）

Task 1-2 のビルドで発生したエラーを修正する。
**ただし既存のロジックを一切変更しないこと**。

許可される変更のみ：

### パターン A: TCHAR → wchar_t の統一
```cpp
// Before（Win32のみ）
TCHAR szPath[MAX_PATH];
_tcscpy(szPath, text);

// After（クロスプラットフォーム）
#ifdef _WIN32
    TCHAR szPath[MAX_PATH];
    _tcscpy(szPath, text);
#else
    wchar_t szPath[MAX_PATH];
    wcscpy(szPath, text);
#endif
```

### パターン B: Win32 文字列関数の置換
```cpp
// Before
_wcsicmp(str1, str2);   // Win32のみ
_wtoi(str);              // Win32のみ

// After
#ifdef _WIN32
    _wcsicmp(str1, str2);
#else
    wcscasecmp(str1, str2);  // POSIX
#endif
```

### パターン C: windows.h インクルードの条件化
```cpp
// Before
#include <windows.h>

// After
#ifdef _WIN32
    #include <windows.h>
#else
    #include "platform/platform.h"
    // Win32型の最小限 stub
    #ifndef MAX_PATH
    #define MAX_PATH 260
    #endif
    using BOOL = int;
    #define TRUE 1
    #define FALSE 0
#endif
```

### パターン D: windows.h 必須の型 stub
`sakura_core/platform/win32_types_stub.h` を作成せよ：

```cpp
/**
 * win32_types_stub.h
 * 非Windowsで必要な Windows 型の最小限 stub 定義
 * 実装は提供しない（コンパイルを通すためだけ）
 */
#pragma once
#ifndef _WIN32

#include <cstdint>
#include <cstddef>

// 基本型
using BOOL    = int;
using DWORD   = uint32_t;
using WORD    = uint16_t;
using BYTE    = uint8_t;
using LONG    = int32_t;
using ULONG   = uint32_t;
using UINT    = unsigned int;
using WPARAM  = uintptr_t;
using LPARAM  = intptr_t;
using LRESULT = intptr_t;
using HANDLE  = void*;
using HWND    = void*;
using HDC     = void*;
using HFONT   = void*;
using HBRUSH  = void*;
using HPEN    = void*;
using HBITMAP = void*;
using HINSTANCE = void*;
using HMENU   = void*;
using HKEY    = void*;

// 文字列型
using WCHAR   = wchar_t;
using LPWSTR  = wchar_t*;
using LPCWSTR = const wchar_t*;
using TCHAR   = wchar_t;
using LPTSTR  = wchar_t*;
using LPCTSTR = const wchar_t*;
using LPSTR   = char*;
using LPCSTR  = const char*;

// 定数
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)(-1))
#endif

// 構造体
struct POINT { LONG x, y; };
struct SIZE  { LONG cx, cy; };
struct RECT  { LONG left, top, right, bottom; };

// 文字列関数のマッピング
#include <wchar.h>
#include <wctype.h>
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _wtoi(const wchar_t* s) { return (int)wcstol(s, nullptr, 10); }
inline long _wtol(const wchar_t* s) { return wcstol(s, nullptr, 10); }
inline wchar_t* _wcsupr(wchar_t* s) { /* TODO */ return s; }
inline wchar_t* _wcslwr(wchar_t* s) { /* TODO */ return s; }
inline int _tcscmp(const wchar_t* a, const wchar_t* b) { return wcscmp(a, b); }
inline int _tcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline size_t _tcslen(const wchar_t* s) { return wcslen(s); }

#endif // !_WIN32
```

修正作業のやり方：
1. `cmake --build build 2>&1 | grep "error:"` でエラーを確認
2. エラー1件ずつ、上記パターンを使って修正
3. 修正後に再ビルドして確認
4. 修正不可能な場合は除外リストに追加して `docs/porting/excluded_files.md` に理由を記録
5. 全エラーが解消したら次のタスクへ

---

## Task 1-4: GitHub Actions CI の設定

`.github/workflows/cmake-cross-platform.yml` を作成せよ：

```yaml
name: Cross-Platform Build

on:
  push:
    branches: [ main, porting/wxwidgets ]
  pull_request:
    branches: [ main ]

jobs:
  build-linux:
    name: Build (Ubuntu)
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y \
            libwxgtk3.2-dev libwxgtk-media3.2-dev \
            cmake ninja-build pkg-config \
            libgtk-3-dev

      - name: Configure
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --parallel

      - name: Test
        run: |
          cd build
          ctest --output-on-failure

  build-macos:
    name: Build (macOS)
    runs-on: macos-14
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          brew install wxwidgets cmake ninja

      - name: Configure
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --parallel

      - name: Test
        run: |
          cd build
          ctest --output-on-failure
```

---

## Task 1-5: 非 Windows 向け最小テストスイートの作成

`tests/wx_tests/` ディレクトリを作成し、以下を実装せよ：

### `tests/wx_tests/CMakeLists.txt`
```cmake
find_package(GTest REQUIRED)

add_executable(sakura_wx_tests
    test_platform_pal.cpp
    test_file_io.cpp
    test_string_utils.cpp
)

target_link_libraries(sakura_wx_tests
    sakura_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(sakura_wx_tests)
```

### `tests/wx_tests/test_platform_pal.cpp`
```cpp
#include <gtest/gtest.h>
#include "platform/platform.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace SakuraPlatform;

TEST(PlatformPAL, ConfigDirExists_OrCanBeCreated) {
    auto dir = System::GetConfigDir();
    fs::create_directories(dir);
    EXPECT_TRUE(fs::exists(dir));
}

TEST(PlatformPAL, GetOSName_NotEmpty) {
    EXPECT_FALSE(System::GetOSName().empty());
}

TEST(PlatformPAL, FileReadWrite_RoundTrip) {
    auto tmp = File::GetTempFilePath(L"sakura_test");
    std::vector<char> data = {'H','e','l','l','o',' ',
                               static_cast<char>(0xE3), // UTF-8: あ
                               static_cast<char>(0x81),
                               static_cast<char>(0x82)};
    ASSERT_TRUE(File::WriteBytes(tmp, data));
    
    std::vector<char> readback;
    ASSERT_TRUE(File::ReadBytes(tmp, readback));
    EXPECT_EQ(data, readback);
    
    fs::remove(tmp);
}

TEST(PlatformPAL, ConfigStore_StringRoundTrip) {
    auto store = CreateConfigStore(L"test_section");
    ASSERT_NE(store, nullptr);
    
    EXPECT_TRUE(store->SetString(L"test_key", L"テスト値"));
    NativeString value;
    EXPECT_TRUE(store->GetString(L"test_key", value));
    EXPECT_EQ(value, L"テスト値");
    
    EXPECT_TRUE(store->DeleteKey(L"test_key"));
}

TEST(PlatformPAL, ConfigStore_IntRoundTrip) {
    auto store = CreateConfigStore(L"test_section");
    ASSERT_NE(store, nullptr);
    
    EXPECT_TRUE(store->SetInt(L"int_key", 42));
    int value = 0;
    EXPECT_TRUE(store->GetInt(L"int_key", value));
    EXPECT_EQ(value, 42);
}

TEST(PlatformPAL, SharedMemory_CreateAndWrite) {
    const std::string name = "sakura_test_shm";
    const size_t size = 1024;
    
    auto shm = CreateSharedMemory(name, size, true);
    ASSERT_NE(shm, nullptr);
    EXPECT_TRUE(shm->IsValid());
    EXPECT_EQ(shm->GetSize(), size);
    
    // 書き込みテスト
    auto* ptr = static_cast<char*>(shm->GetPtr());
    strcpy(ptr, "SharedMemory test");
    EXPECT_STREQ(ptr, "SharedMemory test");
}
```

ビルドしてテストが通ることを確認せよ：
```bash
cmake --build build --parallel
cd build && ctest --output-on-failure -V
```

---

## Phase 1 完了チェックリスト

- [ ] `CMakeLists.txt` がリポジトリルートに存在する
- [ ] `sakura_core/CMakeLists.txt` が存在する
- [ ] `cmake --build build` が macOS/Linux でエラーなく完了する
- [ ] `docs/porting/excluded_files.md` に除外ファイルと理由が記録されている
- [ ] `tests/wx_tests/` が存在し、全テストがパスする
- [ ] `.github/workflows/cmake-cross-platform.yml` が存在する
- [ ] `docs/porting/progress.md` が更新されている
