cmake_minimum_required(VERSION 3.16)

# ============================================================
# 非 Windows 向け分岐
#
# macOS: SAKURA_MAC_NATIVE（既定 ON）で sakura_core フォーク + winport 互換層をビルド。
#        凍結した wxWidgets 版は SAKURA_BUILD_WX=ON のときだけビルドする。
# Linux: wxWidgets 版のみ。
# いずれも return() で以下の Win32 向けロジックは一切実行しない。
# ============================================================
if(NOT WIN32)
    cmake_minimum_required(VERSION 3.20)
    project(SakuraEditor VERSION 2.5.0 LANGUAGES CXX)

    # Phase 6: sakura_core/util/string_ex.h が std::span を使うため C++20 が必要。
    # wxWidgets 3.3 / Apple Clang 17 / GCC 11+ いずれも C++20 をサポートする。
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    if(APPLE)
        option(SAKURA_MAC_NATIVE "Build sakura_core fork on the winport Win32 layer" ON)
        option(SAKURA_BUILD_WX "Build the frozen wxWidgets port" OFF)
    else()
        set(SAKURA_MAC_NATIVE OFF)
        option(SAKURA_BUILD_WX "Build the frozen wxWidgets port" ON)
    endif()

    # GoogleTest はテスト用。FetchContent で取得して in-tree でビルド。
    include(FetchContent)
    set(FETCHCONTENT_QUIET FALSE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    if(SAKURA_MAC_NATIVE)
        enable_language(OBJCXX)
        message(STATUS "Platform: macOS (native, winport)")
        add_subdirectory(winport)
        add_subdirectory(sakura_mac)
        add_subdirectory(tests/mac_tests)
    endif()

    if(SAKURA_BUILD_WX)
        if(APPLE)
            add_compile_definitions(SAKURA_PLATFORM_MACOS SAKURA_PLATFORM_WX)
            message(STATUS "Platform: macOS (wxWidgets port, frozen)")
        else()
            add_compile_definitions(SAKURA_PLATFORM_LINUX SAKURA_PLATFORM_WX)
            message(STATUS "Platform: Linux (wxWidgets port)")
        endif()
        find_package(wxWidgets REQUIRED COMPONENTS core base stc aui xml net)
        include(${wxWidgets_USE_FILE})
        message(STATUS "wxWidgets found: ${wxWidgets_VERSION}")

        add_subdirectory(sakura_core)
        add_subdirectory(sakura_wx)
        add_subdirectory(tests/wx_tests)
    endif()

    return()
endif()
# ============================================================
# 以下は upstream の Win32 向けビルド定義（変更禁止）
# ============================================================

# Define primary project
project(sakura LANGUAGES CXX)

# Include sakura.cmake for sub-targets
include(${CMAKE_SOURCE_DIR}/src/main/cmake/sakura.cmake)

# Create main executable
add_executable(${PROJECT_NAME} WIN32 ${CMAKE_SOURCE_DIR}/sakura_core/StdAfx.cpp)

# link libraries
target_link_libraries(${PROJECT_NAME}
  PRIVATE
    sakura_core
)

add_custom_command(TARGET ${PROJECT_NAME} PRE_LINK
  COMMAND ${CMAKE_COMMAND} -E remove -f $<TARGET_FILE:${PROJECT_NAME}>
)

add_dependencies(${PROJECT_NAME}
  generate_sakura_exe_manifest
)

if(MINGW)
  target_sources(${PROJECT_NAME}
    PRIVATE
      "${SAKURA_MANIFEST_RC}"
  )
endif(MINGW)

# Create language DLL projects
# en-US: 1033 (0x0409) - English (United States)
# zh-CN: 2052 (0x0804) - Chinese (Simplified, PRC)
create_language_dll(en-US 1033)
create_language_dll(zh-CN 2052)

# Include ctags.cmake for ctags command
include(${CMAKE_SOURCE_DIR}/src/main/cmake/ctags.cmake)

# Include diffutils.cmake for diff command
include(${CMAKE_SOURCE_DIR}/src/main/cmake/diffutils.cmake)

# Enable ctest command
enable_testing()

# Convert paths to native Windows format (backslashes) for OpenCppCoverage
file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" CMAKE_SOURCE_DIR_NATIVE)
file(TO_NATIVE_PATH "${OUTPUT_DIRECTORY}" OUTPUT_DIRECTORY_NATIVE)

# Include tests1 definition
include(${CMAKE_SOURCE_DIR}/src/test/cmake/tests1.cmake)

if(OpenCppCoverage_EXECUTABLE)
  add_test(NAME CoverageTest
    COMMAND ${OpenCppCoverage_EXECUTABLE}
      --export_type cobertura:${CMAKE_SOURCE_DIR_NATIVE}\\tests1-coverage.xml
      --modules ${OUTPUT_DIRECTORY_NATIVE}\\tests1.exe
      --sources ${CMAKE_SOURCE_DIR_NATIVE}
      --excluded_sources ${CMAKE_SOURCE_DIR_NATIVE}\\build
      --working_dir ${OUTPUT_DIRECTORY_NATIVE}
      --cover_children
      --
      $<TARGET_FILE:tests1>
      --gtest_output=xml:${CMAKE_SOURCE_DIR}/tests1-googletest.xml
    CONFIGURATIONS Debug
  )
endif()

add_test(NAME UnitTest
  COMMAND $<TARGET_FILE:tests1>
    --gtest_output=xml:${CMAKE_SOURCE_DIR}/tests1-googletest.xml
  WORKING_DIRECTORY ${OUTPUT_DIRECTORY}
  CONFIGURATIONS Release
)

add_test(NAME pytest
  COMMAND
    "${UV_EXECUTABLE}"
    run
    python
    -m pytest
    --ignore=externals
    --ignore=tools/vcpkg
    --cov
    --cov-config=src/test/py/.coveragerc
    --cov-report=xml:pytest-coverage.xml
    --cov-fail-under=80
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
