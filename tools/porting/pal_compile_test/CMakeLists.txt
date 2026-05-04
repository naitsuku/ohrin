cmake_minimum_required(VERSION 3.20)
project(pal_compile_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

# PAL の wxWidgets / POSIX 実装だけを object library としてコンパイルする。
# このテストは実行用ではなく「ビルドが通るか」だけを確認するもの。
add_library(pal_wx OBJECT
    ${CMAKE_SOURCE_DIR}/../../../sakura_core/platform/platform_impl_wx.cpp
)
target_include_directories(pal_wx PRIVATE
    ${CMAKE_SOURCE_DIR}/../../../sakura_core
)
target_link_libraries(pal_wx PRIVATE ${wxWidgets_LIBRARIES})
