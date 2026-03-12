# CXX/test/catalog/CMakeLists.txt
add_executable(catalog_test
    column_test.cxx
    schema_test.cxx
    catalog_table_info_test.cxx
)

target_link_libraries(catalog_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(catalog_test)
