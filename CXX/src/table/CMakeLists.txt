# CXX/src/table/CMakeLists.txt

add_library(haruhi_table
    table_heap.cxx
    table_iterator.cxx
)

target_link_libraries(haruhi_table PUBLIC
    compile_version_cxx23
    haruhi_buffer
    haruhi_storage
)

target_include_directories(haruhi_table PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)
