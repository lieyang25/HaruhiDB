#   CXX/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)

project(HaruhiDB 
        VERSION 1.0
        DESCRIPTION "a DB project"
        LANGUAGES C CXX
        )

enable_testing()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(compile_version_cxx23 INTERFACE)
target_compile_features(compile_version_cxx23 INTERFACE cxx_std_23)

add_subdirectory(src)

option(EXAMPLE "Bggin example" OFF)
if(EXAMPLE)
        add_subdirectory(example)
endif()

option(TEST "Begin test" OFF)
if(TEST)
        add_subdirectory(test)
endif()
