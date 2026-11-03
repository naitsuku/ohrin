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
target_compile_options(winport_headers INTERFACE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/winport_prelude.h"
    "SHELL:-idirafter ${WINPORT_MINGW_INCLUDE}"
    -fdeclspec
    -Wno-ignored-attributes  # __stdcall 等は arm64 では無視される
)

# 自動生成の未実装スタブ（tools/porting/gen_winport_stubs.py）。
# 実装済みの関数は winport/src 側の強いシンボルが優先される。
set(WINPORT_STUBS ${CMAKE_CURRENT_SOURCE_DIR}/src/generated_stubs.cpp)

add_library(winport STATIC
    src/codepage.cpp
    src/crt.cpp
    src/kernel32.cpp
    src/kernel32_file.cpp
    src/kernel32_object.cpp
    src/resource.cpp
    src/printf.cpp
    src/debug.cpp
    src/darkmode.cpp
    src/guids.cpp
    ${WINPORT_STUBS}
)
target_link_libraries(winport PUBLIC winport_headers)
target_include_directories(winport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(winport PUBLIC "-framework CoreFoundation")
