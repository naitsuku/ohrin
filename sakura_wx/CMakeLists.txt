# sakura_wx/CMakeLists.txt — wxWidgets フロントエンド
#
# Phase 2 で新規追加。本ファイルは非 Windows ビルド時にのみルート
# CMakeLists.txt から add_subdirectory() で取り込まれる。

if(WIN32)
    return()
endif()

set(SAKURA_WX_SOURCES
    app/SakuraApp.cpp
    window/CEditWndWx.cpp
    view/CEditViewWx.cpp
    dialog/CDlgSearchWx.cpp
)

if(APPLE)
    add_executable(sakura_wx MACOSX_BUNDLE ${SAKURA_WX_SOURCES})
    set_target_properties(sakura_wx PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "jp.sourceforge.sakura-editor.wxport"
        MACOSX_BUNDLE_BUNDLE_NAME    "SakuraEditor"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    )
else()
    add_executable(sakura_wx ${SAKURA_WX_SOURCES})
endif()

target_include_directories(sakura_wx PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/sakura_core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sakura_wx PRIVATE
    sakura_core
    ${wxWidgets_LIBRARIES}
)

target_compile_options(sakura_wx PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-deprecated-declarations
)
