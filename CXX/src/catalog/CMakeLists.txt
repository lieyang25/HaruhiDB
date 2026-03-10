# CXX/src/catalog/CMakeLists.txt

add_library(haruhi_catalog
    column.cxx
    schema.cxx
)

target_link_libraries(haruhi_catalog PUBLIC
    compile_version_cxx23
    haruhi_type
)

target_include_directories(haruhi_catalog PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)
