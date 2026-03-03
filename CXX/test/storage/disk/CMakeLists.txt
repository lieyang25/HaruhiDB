# CXX/test/storage/disk/CMakeLists.txt

add_executable(disk_manager_test
    disk_manager_test.cxx
)

target_link_libraries(disk_manager_test PRIVATE
    HaruhiDB
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(disk_manager_test)