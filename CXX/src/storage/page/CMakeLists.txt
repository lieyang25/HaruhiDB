# CXX/src/storage/page/page.cxx

add_library(haruhi_page
    page.cxx
    table_page.cxx
)

target_link_libraries(haruhi_page   PUBLIC
    compile_version_cxx23
)

target_include_directories(haruhi_page  PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)