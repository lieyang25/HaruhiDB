# CXX/src/buffer/buffer_manager/CMakeLists.txt
add_library(haruhi_buffer_pool_manager
    buffer_pool_manager.cxx
)

target_link_libraries(haruhi_buffer_pool_manager PUBLIC
    compile_version_cxx23
)

target_include_directories(buffer_pool_manager PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)