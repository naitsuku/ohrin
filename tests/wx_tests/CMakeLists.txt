# tests/wx_tests/CMakeLists.txt
#
# 非 Windows 向けの最小テストスイート。
# GoogleTest はルート CMakeLists.txt で FetchContent 済み（gtest / gtest_main ターゲット）。

add_executable(sakura_wx_tests
    test_platform_pal.cpp
    test_encoding.cpp
    test_sakura_config.cpp
    test_shared_data.cpp
    test_funccode.cpp        # Phase 12: gen_funccode.py 生成ヘッダの smoke test
)

# Funccode_enum.h を確実に先に生成させ、include path も sakura_core 経由で取得する
add_dependencies(sakura_wx_tests sakura_funccode_headers)

target_link_libraries(sakura_wx_tests
    PRIVATE
        sakura_wx_core
        sakura_core      # generated/sakura_core を include path 経由で受け取るため
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
