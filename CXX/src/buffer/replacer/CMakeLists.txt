# CXX/src/buffer/replacer/CMakeLists.txt

add_library(haruhi_replacer
    lru_k_replacer.cxx
)

target_link_libraries(haruhi_replacer PUBLIC
    compile_version_cxx23
)

target_include_directories(haruhi_replacer PUBLIC
    "${CMAKE_SOURCE_DIR}/src/include"
)