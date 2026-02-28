#   CXX/src/storage/disk/CMakeLists.txt

add_library(haruhi_disk 
    disk_manager.cxx
)

target_link_libraries(haruhi_disk   PUBLIC
    compile_version_cxx23    
)

target_include_directories(haruhi_disk PUBLIC
    "${PROJECT_SOURCE_DIR}/src/include"
)