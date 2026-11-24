# ============================================================
# sakura_mac — sakura_core フォークを winport 上でビルドする
#
# sakura_core_mac : 上流 sakura_core/*.cpp と src/main/cpp/*.cpp の全体（STATIC）
# （アプリ本体の .app ターゲットは Week 3 以降にここへ追加する）
# ============================================================

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SAKURA_MAC_GEN_DIR ${CMAKE_BINARY_DIR}/generated/sakura_mac)

# ---- Funccode_enum.h / Funccode_define.h（上流 HeaderMake の Python 移植） ----
set(_hsrc ${CMAKE_SOURCE_DIR}/sakura_core/Funccode_x.hsrc)
set(_gen  ${CMAKE_SOURCE_DIR}/tools/porting/gen_funccode.py)
add_custom_command(
    OUTPUT ${SAKURA_MAC_GEN_DIR}/Funccode_enum.h ${SAKURA_MAC_GEN_DIR}/Funccode_define.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SAKURA_MAC_GEN_DIR}
    COMMAND ${Python3_EXECUTABLE} ${_gen} --in ${_hsrc} --out ${SAKURA_MAC_GEN_DIR}/Funccode_enum.h --mode enum --enum EFunctionCode
    COMMAND ${Python3_EXECUTABLE} ${_gen} --in ${_hsrc} --out ${SAKURA_MAC_GEN_DIR}/Funccode_define.h --mode define
    DEPENDS ${_hsrc} ${_gen}
    COMMENT "Generating Funccode headers (sakura_mac)"
    VERBATIM
)

# ---- version.h（上流 version.h.in を最小限の値で展開） ----
set(BUILD_ENV_NAME "winport")
set(BUILD_VERSION 0)
# 版番号は上流の version.cmake から読む（上流を取り込むと自動で上がる）
file(STRINGS ${CMAKE_SOURCE_DIR}/src/main/cmake/version.cmake _sakura_ver REGEX "^set\\(SAKURA_(MAJOR|MINOR|PATCH)_VERSION")
foreach(_part MAJOR MINOR PATCH)
    string(REGEX MATCH "SAKURA_${_part}_VERSION \"([0-9]+)\"" _m "${_sakura_ver}")
    if(NOT CMAKE_MATCH_1)
        message(FATAL_ERROR "SAKURA_${_part}_VERSION not found in version.cmake")
    endif()
    set(SAKURA_${_part}_VERSION ${CMAKE_MATCH_1})
endforeach()
message(STATUS "Sakura version: ${SAKURA_MAJOR_VERSION}.${SAKURA_MINOR_VERSION}.${SAKURA_PATCH_VERSION}")
# バージョン情報に出すコミット（不具合報告で版を特定するため）。git が無ければ (Undefined)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE _sakura_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_sakura_hash)
        set(GIT_SHORT_COMMIT_HASH_DEFINE "#define GIT_SHORT_COMMIT_HASH \"${_sakura_hash}\"")
    endif()
endif()
configure_file(${CMAKE_SOURCE_DIR}/src/main/cmake/version.h.in ${SAKURA_MAC_GEN_DIR}/version.h @ONLY)

# ---- 外部ヘッダ（上流 cmake はビルドディレクトリの include/ にコピーして参照する） ----
file(COPY ${CMAKE_SOURCE_DIR}/externals/cmigemo/src/migemo.h DESTINATION ${SAKURA_MAC_GEN_DIR}/cmigemo)

# ---- リソース（sakura_rc.rc → Windows 形式のリソース表） ----
set(_rc_tool ${CMAKE_SOURCE_DIR}/tools/porting/rc2winport.py)
set(SAKURA_MAC_RESOURCES_CPP ${SAKURA_MAC_GEN_DIR}/sakura_rc_resources.cpp)
add_custom_command(
    OUTPUT ${SAKURA_MAC_RESOURCES_CPP}
    COMMAND ${Python3_EXECUTABLE} ${_rc_tool}
            --rc ${CMAKE_SOURCE_DIR}/sakura_core/sakura_rc.rc
            --out ${SAKURA_MAC_RESOURCES_CPP}
            -I ${CMAKE_SOURCE_DIR}/sakura_core
            -I ${CMAKE_SOURCE_DIR}/src/main/resources
            -I ${SAKURA_MAC_GEN_DIR}
            --mingw-include ${WINPORT_MINGW_INCLUDE}
    DEPENDS ${CMAKE_SOURCE_DIR}/sakura_core/sakura_rc.rc ${CMAKE_SOURCE_DIR}/sakura_core/sakura_rc.rc2
            ${CMAKE_SOURCE_DIR}/src/main/resources/MainMenu.ini
            ${_rc_tool} ${SAKURA_MAC_GEN_DIR}/Funccode_define.h
    COMMENT "Converting sakura_rc.rc to winport resources"
    VERBATIM
)

add_custom_target(sakura_mac_generated
    DEPENDS ${SAKURA_MAC_GEN_DIR}/Funccode_enum.h ${SAKURA_MAC_GEN_DIR}/Funccode_define.h)

# ---- sakura_core_mac ----
file(GLOB_RECURSE SAKURA_CORE_MAC_SOURCES
    ${CMAKE_SOURCE_DIR}/sakura_core/*.cpp
    ${CMAKE_SOURCE_DIR}/src/main/cpp/*.cpp
)
# wx 版の PAL は winport では使わない

add_library(sakura_core_mac STATIC ${SAKURA_CORE_MAC_SOURCES} ${SAKURA_MAC_RESOURCES_CPP})
set_source_files_properties(${SAKURA_MAC_RESOURCES_CPP} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
add_dependencies(sakura_core_mac sakura_mac_generated)
target_include_directories(sakura_core_mac PUBLIC
    ${SAKURA_MAC_GEN_DIR}
    ${CMAKE_SOURCE_DIR}/src/main/cpp
    ${CMAKE_SOURCE_DIR}/src/main/resources
    ${CMAKE_SOURCE_DIR}/sakura_core
    ${CMAKE_SOURCE_DIR}/winport/src   # winport_resource.h（生成リソース表の型）
)
# winport は WHOLE_ARCHIVE で全オブジェクトを取り込む（実装の強シンボルが弱スタブより確実に優先されるように）
target_link_libraries(sakura_core_mac PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,winport>")
# 上流コードの警告は当面抑止（winport 側の問題を見落とさないよう winport 自身は抑止しない）
# -Wno-c++11-narrowing: MSVC は定数式由来の縮小変換（CCSIZEOF_STRUCT 等）を許すため合わせる
target_compile_options(sakura_core_mac PRIVATE -w -Wno-c++11-narrowing)
target_precompile_headers(sakura_core_mac PRIVATE ${CMAKE_SOURCE_DIR}/sakura_core/StdAfx.h)

# ---- アプリのアイコン（上流の .ico → .icns） ----
set(_icns ${SAKURA_MAC_GEN_DIR}/sakura.icns)
add_custom_command(
    OUTPUT ${_icns}
    COMMAND ${CMAKE_SOURCE_DIR}/tools/porting/make_icns.sh ${CMAKE_SOURCE_DIR}/src/main/resources/images/icon_std.ico ${_icns}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/main/resources/images/icon_std.ico ${CMAKE_SOURCE_DIR}/tools/porting/make_icns.sh
    COMMENT "Generating sakura.icns"
    VERBATIM
)
set_source_files_properties(${_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources GENERATED TRUE)

# ---- アプリ本体（.app） ----
add_executable(sakura_mac MACOSX_BUNDLE main.mm ${_icns})
set_target_properties(sakura_mac PROPERTIES
    OUTPUT_NAME "Ohrin"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.naitsuku.ohrin"
    MACOSX_BUNDLE_BUNDLE_NAME "Ohrin"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${SAKURA_MAJOR_VERSION}.${SAKURA_MINOR_VERSION}.${SAKURA_PATCH_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "0"
)
target_include_directories(sakura_mac PRIVATE ${CMAKE_SOURCE_DIR}/winport/src)
target_compile_options(sakura_mac PRIVATE -fobjc-arc)
target_link_libraries(sakura_mac PRIVATE sakura_core_mac)
