cmake_minimum_required(VERSION 3.20)
project(openaslc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(Threads REQUIRED)

# Include directories
include_directories(include)

# Core library
add_library(openaslc_core src/memory_map.cpp)
target_include_directories(openaslc_core PUBLIC include)
target_link_libraries(openaslc_core PUBLIC Threads::Threads)

# Main Executable Entry Point
add_executable(openaslc_app src/main.cpp)
target_link_libraries(openaslc_app PRIVATE openaslc_core)

# Testing setup with Catch2 via FetchContent
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

add_executable(openaslc_tests tests/test_placeholder.cpp tests/test_memory_map.cpp)
target_link_libraries(openaslc_tests PRIVATE openaslc_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(openaslc_tests)
