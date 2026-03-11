# CXX/test/buffer/buffer_pool_manager/CMakeLists.txt
add_executable(buffer_pool_manager_test
    buffer_pool_manager_test.cxx
)

target_link_libraries(buffer_pool_manager_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(buffer_pool_manager_test)