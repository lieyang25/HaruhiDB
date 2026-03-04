# CXX/test/storage/page/CMakeLists.txt

add_executable(page_test
    page_test.cxx
)

target_link_libraries(page_test PRIVATE
    compile_version_cxx23
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(page_test)