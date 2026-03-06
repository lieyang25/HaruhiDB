# CXX/src/buffer/CMakeLists.txt
add_subdirectory(replacer)
add_subdirectory(buffer_manager)

add_library(haruhi_buffer INTERFACE)

target_link_libraries(haruhi_buffer INTERFACE
    haruhi_replacer
    haruhi_buffer_pool_manager
)