add_executable(wal_manager_test
    wal_manager_test.cxx
)

target_link_libraries(wal_manager_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(wal_manager_test)
