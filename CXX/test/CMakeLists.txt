#   CXX/test/CMakeLists.txt
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)
add_subdirectory(storage)
add_subdirectory(buffer)