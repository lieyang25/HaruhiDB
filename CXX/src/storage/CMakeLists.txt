#   CXX/src/storage/CMakeLists.txt

add_subdirectory(disk)

add_library(haruhi_storage INTERFACE)

target_link_libraries(haruhi_storage    INTERFACE
    haruhi_disk
)