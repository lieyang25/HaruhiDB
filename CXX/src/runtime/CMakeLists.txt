add_library(haruhi_runtime
    database_runtime.cxx
)

target_link_libraries(haruhi_runtime PUBLIC
    compile_version_cxx23
    haruhi_buffer
    haruhi_catalog
    haruhi_execution
    haruhi_wal
)

target_include_directories(haruhi_runtime PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)
