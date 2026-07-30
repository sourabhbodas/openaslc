cmake_minimum_required(VERSION 3.20)
project(openaslc VERSION 0.1.0 LANGUAGES CXX C)

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

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3
)
set(HTTPLIB_COMPILE OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(httplib)

# SQLite amalgamation (Public Domain) -- single-file C source, no CMakeLists.txt of
# its own, so it's fetched by URL (not GIT_REPOSITORY) and built as a plain static
# library here. URL/hash verified against the live sqlite.org download page.
FetchContent_Declare(
  sqlite3
  URL      https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)
add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
target_link_libraries(sqlite3 PUBLIC Threads::Threads)
if(UNIX)
  target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS})
endif()

# Include directories
include_directories(include)

# Core library
add_library(openaslc_core src/memory_map.cpp src/aslc_engine.cpp src/mock_driver.cpp src/logic_interpreter.cpp src/web_server.cpp src/telemetry_server.cpp src/ring_buffer_logger.cpp src/config_archive.cpp)
target_include_directories(openaslc_core PUBLIC include)
target_link_libraries(openaslc_core PUBLIC Threads::Threads nlohmann_json::nlohmann_json httplib::httplib sqlite3)
if(WIN32)
  target_link_libraries(openaslc_core PUBLIC ws2_32)
endif()

# Main Executable Entry Point
add_executable(openaslc_app src/main.cpp)
target_link_libraries(openaslc_app PRIVATE openaslc_core)
target_compile_definitions(openaslc_app PRIVATE OPENASLC_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
target_compile_definitions(openaslc_app PRIVATE OPENASLC_WWW_DIR="${CMAKE_SOURCE_DIR}/www")
target_compile_definitions(openaslc_app PRIVATE OPENASLC_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# Testing setup with Catch2 via FetchContent
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

add_executable(openaslc_tests tests/test_placeholder.cpp tests/test_memory_map.cpp tests/test_aslc_engine.cpp tests/test_mock_driver.cpp tests/test_logic_interpreter.cpp tests/test_web_server.cpp tests/test_telemetry_server.cpp tests/test_ring_buffer_logger.cpp tests/test_config_archive.cpp)
target_link_libraries(openaslc_tests PRIVATE openaslc_core Catch2::Catch2WithMain)
if(WIN32)
  target_link_libraries(openaslc_tests PRIVATE ws2_32)
endif()

include(CTest)
include(Catch)
catch_discover_tests(openaslc_tests)
