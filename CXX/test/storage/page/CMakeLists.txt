# CXX/test/storage/page/CMakeLists.txt

add_executable(table_page_test
    table_page_test.cxx
    b_plus_tree_page_test.cxx
    b_plus_tree_leaf_page_test.cxx
    b_plus_tree_internal_page_test.cxx
)

target_link_libraries(table_page_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(table_page_test)
