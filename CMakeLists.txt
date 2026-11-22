cmake_minimum_required(VERSION 3.16)

# ============================================================
# macOS 向け分岐（Sakura Editor for Mac）
#
# sakura_core フォーク + winport 互換層（AppKit / CoreText）をビルドする。
# return() で以下の Win32 向けロジックは一切実行しない。
# ============================================================
if(APPLE)
    cmake_minimum_required(VERSION 3.20)
    # 対応 OS の下限（Info.plist の LSMinimumSystemVersion と揃える）。std::format の浮動小数点が 13.3 から。
    # 未指定だとビルド機の SDK の版になり、古い macOS で起動できなくなる。project() より前に置く
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS version")
    project(SakuraEditor LANGUAGES CXX)

    # sakura_core/util/string_ex.h が std::span を使うため C++20
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

    enable_language(OBJCXX)
    message(STATUS "Platform: macOS (native, winport)")
    add_subdirectory(winport)
    add_subdirectory(sakura_mac)
    add_subdirectory(tests/mac_tests)

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
