#   CXX/src/CMakeLists.txt
add_subdirectory(storage)
add_subdirectory(buffer)
add_subdirectory(type)
add_subdirectory(catalog)
add_subdirectory(table)

add_library(HaruhiDB INTERFACE)

target_link_libraries(HaruhiDB INTERFACE
    haruhi_storage
    haruhi_buffer
    haruhi_type
    haruhi_catalog
    haruhi_table
)
