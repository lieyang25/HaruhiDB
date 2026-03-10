#   CXX/src/storage/disk/CMakeLists.txt

add_library(haruhi_disk_manager 
    disk_manager.cxx
)

target_link_libraries(haruhi_disk_manager   PUBLIC
    compile_version_cxx23    
)

target_include_directories(haruhi_disk_manager  PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)