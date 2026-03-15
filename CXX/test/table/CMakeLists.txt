add_executable(table_test
    table_heap_test.cxx
    table_heap_wal_test.cxx
    tuple_codec_test.cxx
)

target_link_libraries(table_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(table_test)
