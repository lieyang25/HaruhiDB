add_library(haruhi_wal
    wal_manager.cxx
)

target_link_libraries(haruhi_wal PUBLIC
    compile_version_cxx23
)

target_include_directories(haruhi_wal PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)
