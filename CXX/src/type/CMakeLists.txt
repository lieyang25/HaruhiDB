# CXX/src/type/CMakeLists.txt

add_library(haruhi_type
    value.cxx
)

target_link_libraries(haruhi_type PUBLIC
    compile_version_cxx23
)

target_include_directories(haruhi_type PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)
