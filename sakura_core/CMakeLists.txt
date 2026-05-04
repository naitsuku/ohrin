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
# Phase 11: Funccode_enum.h / Funccode_define.h を Python 生成器で出力。
# upstream HeaderMake.exe (Win32 限定) の Python 移植版を CMake から起動。
# 生成物は ${CMAKE_BINARY_DIR}/generated/sakura_core/ に置き、include path に追加。
# 入力 .hsrc が変わったら再生成される（add_custom_command の DEPENDS）。
# ------------------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SAKURA_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/sakura_core)
set(SAKURA_FUNCCODE_HSRC ${CMAKE_CURRENT_SOURCE_DIR}/Funccode_x.hsrc)
set(SAKURA_FUNCCODE_GEN  ${CMAKE_SOURCE_DIR}/tools/porting/gen_funccode.py)
set(SAKURA_FUNCCODE_ENUM_H   ${SAKURA_GENERATED_DIR}/Funccode_enum.h)
set(SAKURA_FUNCCODE_DEFINE_H ${SAKURA_GENERATED_DIR}/Funccode_define.h)

add_custom_command(
    OUTPUT  ${SAKURA_FUNCCODE_ENUM_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SAKURA_GENERATED_DIR}
    COMMAND ${Python3_EXECUTABLE} ${SAKURA_FUNCCODE_GEN}
            --in   ${SAKURA_FUNCCODE_HSRC}
            --out  ${SAKURA_FUNCCODE_ENUM_H}
            --mode enum
            --enum EFunctionCode
    DEPENDS ${SAKURA_FUNCCODE_HSRC} ${SAKURA_FUNCCODE_GEN}
    COMMENT "Generating Funccode_enum.h from Funccode_x.hsrc"
    VERBATIM
)
add_custom_command(
    OUTPUT  ${SAKURA_FUNCCODE_DEFINE_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SAKURA_GENERATED_DIR}
    COMMAND ${Python3_EXECUTABLE} ${SAKURA_FUNCCODE_GEN}
            --in   ${SAKURA_FUNCCODE_HSRC}
            --out  ${SAKURA_FUNCCODE_DEFINE_H}
            --mode define
    DEPENDS ${SAKURA_FUNCCODE_HSRC} ${SAKURA_FUNCCODE_GEN}
    COMMENT "Generating Funccode_define.h from Funccode_x.hsrc"
    VERBATIM
)
add_custom_target(sakura_funccode_headers
    DEPENDS ${SAKURA_FUNCCODE_ENUM_H} ${SAKURA_FUNCCODE_DEFINE_H}
)

# ------------------------------------------------------------
# Phase 1 初期段階で含めるソース（基盤のみ）
#
# 方針: Win32 API 直接依存度ゼロ または PAL 経由で代替可能なものに限定。
# Win32 typedef や TCHAR を使うファイルは win32_types_stub.h でカバーされるが、
# それでもリンクしない関数や構造体に触れているケースが多いため、
# ヘッダ依存が浅くて済む小規模ファイルから着手する。
# ------------------------------------------------------------
# ------------------------------------------------------------
# Phase 6: 段階的に portable 化に成功したソース
# 各ファイルを追加するたびに `cmake --build build` で確認し、
# error が出るものは即座に除外（docs/porting/excluded_files.md に記録）。
# ------------------------------------------------------------
set(SAKURA_CORE_SOURCES
    # PAL 実装（Phase 0）
    platform/platform_impl_wx.cpp
)

# ------------------------------------------------------------
# Phase 6 段階取り込み第 1 弾: basis/ の幾何・整数型派生クラス。
# StdAfx.h 経由で win32_types_stub.h の POINT/RECT/SIZE/DWORD だけで通る。
# ------------------------------------------------------------
list(APPEND SAKURA_CORE_SOURCES
    basis/CMyPoint.cpp
    basis/CMyRect.cpp
    basis/CMySize.cpp
    basis/CStrictPoint.cpp
    basis/CStrictRect.cpp
    basis/CStrictRange.cpp
    basis/CLaxInteger.cpp
    basis/CStrictInteger.cpp  # Phase 7: STRICTINT_LEFT_INT_CMP(LONG) を Win32 限定にして取り込み成功
)

# ------------------------------------------------------------
# Phase 10 試行ログ:
# win32_types_stub.h に LOGFONT / DeleteObject / SelectObject /
# IsValidCodePage / LF_FACESIZE 等 GDI no-op シムを追加して charset/ 取り込みを
# 試行したが、以下の追加の壁が判明：
#   - charset/codeutil.h: WideCharToMultiByte / MultiByteToWideChar
#     (Win32 API、PAL では iconv / ICU の実装が必要)
#   - charset/CEuc.h: <mbstring.h> (MSVC 専用ヘッダ)
#   - func/CKeyBind.h: Funccode_enum.h (HeaderMake で自動生成、Win32 ビルド限定)
#   - charset/codechecker.h:322 シンタックスエラー (WCODE 名前空間問題)
#
# これらの解決は大規模な PAL 実装が必要なため Phase 11+ に持ち越す。
# 本セッションのシム拡充は将来の取り込み土台として残す。
# ------------------------------------------------------------

# ------------------------------------------------------------
# 取り込み保留 (Phase 6 後半／Phase 7 で再挑戦):
#   basis/CMyString.cpp  — util/string_ex.h が ACHAR / _vswprintf / span 等を要求
#   basis/CEol.cpp       — charset/charset.h・charcode.h 経由の依存大、
#                          IsValidCodePage / cxx/ResourceHolder.hpp 等
# 詳細は docs/porting/excluded_files.md
# ------------------------------------------------------------

# ------------------------------------------------------------
# ライブラリターゲット
# ------------------------------------------------------------
add_library(sakura_core STATIC ${SAKURA_CORE_SOURCES})

# Funccode_enum.h / Funccode_define.h を確実に先に生成させる
add_dependencies(sakura_core sakura_funccode_headers)

# shim_win32/ を最優先で配置: <Windows.h> 参照を本シムにルーティングする。
target_include_directories(sakura_core BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/shim_win32
)
target_include_directories(sakura_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    # Phase 9: src/main/cpp の cxx/ResourceHolder.hpp 等を参照可能にする
    # (これらは portable な C++20 ヘッダであり、src/ 自体を変更せず利用)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/main/cpp
    # Phase 11: gen_funccode.py が生成するヘッダ
    ${SAKURA_GENERATED_DIR}
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
