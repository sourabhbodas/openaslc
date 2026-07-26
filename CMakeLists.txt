cmake_minimum_required(VERSION 3.20)
project(openaslc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# Include directories
include_directories(include)

# Core library
add_library(openaslc_core src/memory_map.cpp src/aslc_engine.cpp src/mock_driver.cpp src/logic_interpreter.cpp)
target_include_directories(openaslc_core PUBLIC include)
target_link_libraries(openaslc_core PUBLIC Threads::Threads nlohmann_json::nlohmann_json)

# Main Executable Entry Point
add_executable(openaslc_app src/main.cpp)
target_link_libraries(openaslc_app PRIVATE openaslc_core)
target_compile_definitions(openaslc_app PRIVATE OPENASLC_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")

# Testing setup with Catch2 via FetchContent
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

add_executable(openaslc_tests tests/test_placeholder.cpp tests/test_memory_map.cpp tests/test_aslc_engine.cpp tests/test_mock_driver.cpp tests/test_logic_interpreter.cpp)
target_link_libraries(openaslc_tests PRIVATE openaslc_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(openaslc_tests)
