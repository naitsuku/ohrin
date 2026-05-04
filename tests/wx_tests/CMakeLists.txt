# tests/wx_tests/CMakeLists.txt
#
# 非 Windows 向けの最小テストスイート。
# GoogleTest はルート CMakeLists.txt で FetchContent 済み（gtest / gtest_main ターゲット）。

add_executable(sakura_wx_tests
    test_platform_pal.cpp
)

target_link_libraries(sakura_wx_tests
    PRIVATE
        sakura_core
        gtest_main
)

target_compile_options(sakura_wx_tests PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
)

include(GoogleTest)
gtest_discover_tests(sakura_wx_tests
    PROPERTIES
        TIMEOUT 30
)
