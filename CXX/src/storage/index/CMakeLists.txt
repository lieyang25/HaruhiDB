add_library(haruhi_index
    b_plus_tree.cxx
    index_iterator.cxx
)

target_link_libraries(haruhi_index PUBLIC
    compile_version_cxx23
    haruhi_page
    haruhi_buffer_pool_manager
)

target_include_directories(haruhi_index PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)
