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

add_library(winport STATIC
    src/bridge.cpp
    src/cocoa_backend.mm
    src/codepage.cpp
    src/controls.cpp
    src/crt.cpp
    src/gdi.cpp
    src/gdi_text.cpp
    src/commdlg.cpp
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
target_link_libraries(winport PUBLIC winport_headers)
target_include_directories(winport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_source_files_properties(src/cocoa_backend.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(winport PUBLIC "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreText" "-framework Cocoa" "-framework ImageIO" "-framework JavaScriptCore")
