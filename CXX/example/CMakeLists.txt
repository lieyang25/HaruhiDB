add_executable(haruhidb_quickstart
    quickstart.cxx
)

target_link_libraries(haruhidb_quickstart PRIVATE
    compile_version_cxx23
    HaruhiDB
)

target_include_directories(haruhidb_quickstart PRIVATE
    "${CMAKE_SOURCE_DIR}/src/include"
)

add_executable(haruhidb_inspect
    haruhidb_inspect.cxx
)

target_link_libraries(haruhidb_inspect PRIVATE
    compile_version_cxx23
    HaruhiDB
)

target_include_directories(haruhidb_inspect PRIVATE
    "${CMAKE_SOURCE_DIR}/src/include"
)
