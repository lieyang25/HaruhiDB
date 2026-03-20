add_library(haruhidb_capi SHARED
    haruhidb_capi.cxx
)

target_link_libraries(haruhidb_capi PUBLIC
    compile_version_cxx23
    HaruhiDB
)

target_include_directories(haruhidb_capi PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)

set_target_properties(haruhidb_capi PROPERTIES
    OUTPUT_NAME haruhidb_capi
)
