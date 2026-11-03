# ============================================================
# mac_tests — 上流 src/test/cpp/tests1 の GoogleTest を winport 上で走らせる
#
# 上流テストは段階的に取り込む。TESTS1_ENABLED に載せたものだけをビルドする。
# 取り込めないもの・失敗するものは docs/porting/progress.md に理由を記録する。
# ============================================================

set(TESTS1_DIR ${CMAKE_SOURCE_DIR}/src/test/cpp/tests1)

set(TESTS1_ENABLED
    test-ccodebase.cpp
    test-cconvert.cpp
    test-cdecode.cpp
    test-cdocline.cpp
    test-cdoclinemgr.cpp
    test-ceol.cpp
    test-charcode.cpp
    test-clayoutint.cpp
    test-cmemory.cpp
    test-cnative.cpp
    test-codechecker.cpp
    test-cwordparse.cpp
    test-design_template.cpp
    test-editinfo.cpp
    test-format.cpp
    test-grepinfo.cpp
    test-int2dec.cpp
    test-is_mailaddress.cpp
    test-parameterized.cpp
    test-sample.cpp
    test-ssearchoption.cpp
    test-string_ex.cpp
)
# 除外中（理由）:
#   test-statictype.cpp … std::filesystem::path を wstring と直接比較するテストがあり mac ではコンパイル不可
list(TRANSFORM TESTS1_ENABLED PREPEND ${TESTS1_DIR}/)

add_executable(sakura_mac_tests
    mac_main.cpp
    ${CMAKE_SOURCE_DIR}/src/test/resources/printECodeType.cpp
    ${CMAKE_SOURCE_DIR}/src/test/resources/printEEolType.cpp
    ${CMAKE_SOURCE_DIR}/src/test/resources/printEFunctionCode.cpp
    ${TESTS1_ENABLED}
)
target_include_directories(sakura_mac_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/test/resources
    ${TESTS1_DIR}
)
target_compile_options(sakura_mac_tests PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/winport_test_prelude.h"
    -w
)
target_link_libraries(sakura_mac_tests PRIVATE sakura_core_mac gmock)

include(GoogleTest)
gtest_discover_tests(sakura_mac_tests DISCOVERY_MODE PRE_TEST)
