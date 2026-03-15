#   CXX/src/storage/CMakeLists.txt

add_subdirectory(disk)
add_subdirectory(page)
add_subdirectory(index)
add_subdirectory(wal)

add_library(haruhi_storage  INTERFACE)

target_link_libraries(haruhi_storage    INTERFACE
    haruhi_disk_manager
    haruhi_page
    haruhi_index
    haruhi_wal
)
