add_executable(execution_test
    executor_smoke_test.cxx
    executor_functional_test.cxx
    database_runtime_test.cxx
)

target_link_libraries(execution_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(execution_test)
