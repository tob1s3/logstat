cmake_minimum_required(VERSION 3.20)
project(logstat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOGSTAT_BUILD_TESTS "Build unit tests" ON)
option(LOGSTAT_BUILD_BENCH "Build benchmark" ON)
option(LOGSTAT_BUILD_FUZZ "Build libFuzzer target (clang only)" OFF)
option(LOGSTAT_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
set(LOGSTAT_SANITIZE "" CACHE STRING "Sanitizers: address;undefined | thread")

find_package(Threads REQUIRED)

add_library(logstat_warnings INTERFACE)
target_compile_options(logstat_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor
  -Wold-style-cast -Wcast-align -Wdouble-promotion -Wformat=2)
if(LOGSTAT_WARNINGS_AS_ERRORS)
  target_compile_options(logstat_warnings INTERFACE -Werror)
endif()

if(LOGSTAT_SANITIZE)
  string(REPLACE ";" "," _san "${LOGSTAT_SANITIZE}")
  add_compile_options(-fsanitize=${_san} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${_san})
endif()

add_library(logstat_core
  src/parser.cpp
  src/histogram.cpp
  src/normalize.cpp
  src/aggregator.cpp
  src/analyze.cpp
  src/report.cpp)
target_include_directories(logstat_core PUBLIC include)
target_link_libraries(logstat_core PUBLIC Threads::Threads PRIVATE logstat_warnings)

add_executable(logstat src/main.cpp)
target_compile_definitions(logstat PRIVATE LOGSTAT_VERSION="${PROJECT_VERSION}")
target_link_libraries(logstat PRIVATE logstat_core logstat_warnings)
install(TARGETS logstat RUNTIME DESTINATION bin)

if(LOGSTAT_BUILD_BENCH)
  add_executable(logstat_bench bench/bench.cpp)
  target_link_libraries(logstat_bench PRIVATE logstat_core logstat_warnings)
endif()

if(LOGSTAT_BUILD_FUZZ)
  add_executable(fuzz_parser fuzz/fuzz_parser.cpp)
  target_compile_options(fuzz_parser PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_parser PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_libraries(fuzz_parser PRIVATE logstat_core)
endif()

if(LOGSTAT_BUILD_TESTS)
  enable_testing()
  find_package(GTest 1.14 QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    # Закреплено по коммиту тега v1.17.0, а не по имени тега: тег можно переписать.
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
  add_subdirectory(tests)
endif()
