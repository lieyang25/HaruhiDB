#   CXX/src/CMakeLists.txt
add_subdirectory(storage)

add_library(HaruhiDB INTERFACE)

target_link_libraries(HaruhiDB INTERFACE
    haruhi_storage
)