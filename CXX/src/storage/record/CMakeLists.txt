add_library(haruhi_record
    tuple_codec.cxx
)

target_link_libraries(haruhi_record PUBLIC
    compile_version_cxx23
    haruhi_catalog
    haruhi_type
)

target_include_directories(haruhi_record PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)

