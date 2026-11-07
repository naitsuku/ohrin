# ============================================================
# winport — Win32 互換層（macOS）
#
# winport_headers : Win32 ヘッダ環境（INTERFACE）。sakura_core を winport で
#                   コンパイルするターゲットはこれをリンクする。
# winport         : 互換層の実装（STATIC）。
# ============================================================

find_path(WINPORT_MINGW_INCLUDE windows.h
    PATHS
        /opt/homebrew/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/include
        /usr/local/opt/mingw-w64/toolchain-x86_64/x86_64-w64-mingw32/include
    NO_DEFAULT_PATH
)
if(NOT WINPORT_MINGW_INCLUDE)
    message(FATAL_ERROR "mingw-w64 headers not found. Run: brew install mingw-w64")
endif()
message(STATUS "winport: mingw-w64 headers = ${WINPORT_MINGW_INCLUDE}")

add_library(winport_headers INTERFACE)
target_compile_definitions(winport_headers INTERFACE SAKURA_WINPORT=1)
target_include_directories(winport_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Windows ヘッダ環境は C++ だけに適用する。Objective-C++（cocoa_backend.mm）は Cocoa と
# windows.h が衝突するため Windows ヘッダを使わない（backend.h 参照）
target_compile_options(winport_headers INTERFACE
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/winport_prelude.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-idirafter ${WINPORT_MINGW_INCLUDE}>"
    $<$<COMPILE_LANGUAGE:CXX>:-fdeclspec>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-ignored-attributes>  # __stdcall 等は arm64 では無視される
)

# 自動生成の未実装スタブ（tools/porting/gen_winport_stubs.py）。
# 実装済みの関数は winport/src 側の強いシンボルが優先される。
set(WINPORT_STUBS ${CMAKE_CURRENT_SOURCE_DIR}/src/generated_stubs.cpp)

# 正規表現: 上流と同じ bregonig + Onigmo をそのままビルドする。bregonig は wchar_t が 2 バイトである前提なので
# このライブラリだけ -fshort-wchar を使う（winport_headers はリンクしない）。4 バイトとの変換は src/bregonig_bridge.cpp
enable_language(C)
set(ONIGMO_DIR ${CMAKE_SOURCE_DIR}/externals/Onigmo)
set(BREGONIG_DIR ${CMAKE_SOURCE_DIR}/externals/bregonig/src)
if(NOT EXISTS ${BREGONIG_DIR}/bregonig.cpp OR NOT EXISTS ${ONIGMO_DIR}/regexec.c)
    message(FATAL_ERROR "externals are missing. Run: git submodule update --init externals/Onigmo externals/bregonig")
endif()
add_library(winport_regex STATIC
    ${ONIGMO_DIR}/regcomp.c ${ONIGMO_DIR}/regenc.c ${ONIGMO_DIR}/regerror.c ${ONIGMO_DIR}/regexec.c
    ${ONIGMO_DIR}/regext.c ${ONIGMO_DIR}/regparse.c ${ONIGMO_DIR}/regsyntax.c ${ONIGMO_DIR}/regtrav.c
    ${ONIGMO_DIR}/regversion.c ${ONIGMO_DIR}/st.c
    ${ONIGMO_DIR}/enc/ascii.c ${ONIGMO_DIR}/enc/us_ascii.c ${ONIGMO_DIR}/enc/unicode.c
    ${ONIGMO_DIR}/enc/utf_8.c ${ONIGMO_DIR}/enc/utf_16le.c ${ONIGMO_DIR}/enc/utf_16be.c
    ${ONIGMO_DIR}/enc/utf_32le.c ${ONIGMO_DIR}/enc/utf_32be.c
    ${ONIGMO_DIR}/enc/shift_jis.c ${ONIGMO_DIR}/enc/windows_31j.c
    ${BREGONIG_DIR}/bregonig.cpp ${BREGONIG_DIR}/subst.cpp ${BREGONIG_DIR}/bsplit.cpp
    ${BREGONIG_DIR}/btrans.cpp ${BREGONIG_DIR}/sv.cpp
)
target_include_directories(winport_regex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bregonig ${ONIGMO_DIR} ${ONIGMO_DIR}/enc/unicode ${BREGONIG_DIR})
# MEM_VC6_H_: VC6 向けの operator new の置き換えを読まない。register は C++17 で削除された
target_compile_definitions(winport_regex PRIVATE HAVE_CONFIG_H ONIG_EXTERN=extern UNICODE _UNICODE USE_ONIGMO_6 _BREGEXP_
    MEM_VC6_H_ $<$<COMPILE_LANGUAGE:CXX>:register=>)
target_compile_options(winport_regex PRIVATE -w -fdeclspec
    $<$<COMPILE_LANGUAGE:CXX>:-fshort-wchar>)

# bregonig は同じソースを UNICODE あり／なしで 2 度ビルドする前提で、共通の関数とデータは「なし」の側で定義している
add_library(winport_regex_ansi STATIC ${BREGONIG_DIR}/bregonig.cpp ${BREGONIG_DIR}/subst.cpp
    ${BREGONIG_DIR}/bsplit.cpp ${BREGONIG_DIR}/btrans.cpp ${BREGONIG_DIR}/sv.cpp)
target_include_directories(winport_regex_ansi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bregonig ${ONIGMO_DIR} ${BREGONIG_DIR})
target_compile_definitions(winport_regex_ansi PRIVATE ONIG_EXTERN=extern USE_ONIGMO_6 _BREGEXP_ MEM_VC6_H_ register=)
target_compile_options(winport_regex_ansi PRIVATE -w -fdeclspec -fshort-wchar)
target_link_libraries(winport_regex PUBLIC winport_regex_ansi)

add_library(winport STATIC
    src/bregonig_bridge.cpp
    src/bridge.cpp
    src/cocoa_backend.mm
    src/codepage.cpp
    src/controls.cpp
    src/crt.cpp
    src/gdi.cpp
    src/gdi_text.cpp
    src/commdlg.cpp
    src/control_listview.cpp
    src/control_misc.cpp
    src/control_tab.cpp
    src/control_treeview.cpp
    src/propsheet.cpp
    src/dialog.cpp
    src/dialog_controls.cpp
    src/imm32.cpp
    src/jscript.cpp
    src/kernel32.cpp
    src/kernel32_file.cpp
    src/kernel32_object.cpp
    src/oleaut32.cpp
    src/process.cpp
    src/resource.cpp
    src/shell_ole.cpp
    src/user32_message.cpp
    src/user32_misc.cpp
    src/user32_window.cpp
    src/printf.cpp
    src/debug.cpp
    src/darkmode.cpp
    src/guids.cpp
    ${WINPORT_STUBS}
)
target_link_libraries(winport PUBLIC winport_headers winport_regex)
target_include_directories(winport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_source_files_properties(src/cocoa_backend.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(winport PUBLIC "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreText" "-framework Cocoa" "-framework ImageIO" "-framework JavaScriptCore")
