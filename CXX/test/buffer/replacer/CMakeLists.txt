# CXX/test/buffer/replacer/CMakeLists.txt
add_executable(replacer_test
    lru_k_replacer_test.cxx
)

target_link_libraries(replacer_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(replacer_test)