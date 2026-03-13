add_executable(b_plus_tree_test
    b_plus_tree_test.cxx
)

target_link_libraries(b_plus_tree_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(b_plus_tree_test)
