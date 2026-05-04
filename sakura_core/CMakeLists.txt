# ============================================================
# sakura_core/CMakeLists.txt （非 Windows 向け）
#
# Win32 では本ファイルは使われない（ルート CMakeLists.txt で return() される）。
# 非 Windows では sakura_core を STATIC ライブラリとしてビルドする。
#
# 段階的ポータブル化方針:
#   - 確実にコンパイルできるファイルを INITIAL_PORTABLE_SOURCES に追加
#   - 試行で error が出たファイルは docs/porting/excluded_files.md に
#     理由とともに記録し、リストから外す
#   - Phase 2/3 で対象を順次広げていく
# ============================================================

if(WIN32)
    # 念のためのガード。Win32 では何もせずに抜ける。
    return()
endif()

# ------------------------------------------------------------
# Phase 1 初期段階で含めるソース（基盤のみ）
#
# 方針: Win32 API 直接依存度ゼロ または PAL 経由で代替可能なものに限定。
# Win32 typedef や TCHAR を使うファイルは win32_types_stub.h でカバーされるが、
# それでもリンクしない関数や構造体に触れているケースが多いため、
# ヘッダ依存が浅くて済む小規模ファイルから着手する。
# ------------------------------------------------------------
set(SAKURA_CORE_SOURCES
    # PAL 実装
    platform/platform_impl_wx.cpp
)

# ------------------------------------------------------------
# ライブラリターゲット
# ------------------------------------------------------------
add_library(sakura_core STATIC ${SAKURA_CORE_SOURCES})

target_include_directories(sakura_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(sakura_core PUBLIC
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

target_link_libraries(sakura_core PUBLIC ${wxWidgets_LIBRARIES})

target_compile_options(sakura_core PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-deprecated-declarations
)

message(STATUS "sakura_core: ${CMAKE_PROJECT_NAME} non-Windows build, "
               "initial sources count = ${CMAKE_CURRENT_LIST_DIR}")
