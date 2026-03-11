# CXX/test/type/CMakeLists.txt
add_executable(type_test
    type_test.cxx
    value_test.cxx
)

target_link_libraries(type_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(type_test)