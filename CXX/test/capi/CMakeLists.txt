add_executable(capi_test
    capi_test.cxx
)

target_link_libraries(capi_test PRIVATE
    compile_version_cxx23
    haruhidb_capi
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(capi_test)
