cmake_minimum_required(VERSION 3.20)
project(tecflix_streaming LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(TECFLIX_COVERAGE "Build with gcov instrumentation" OFF)
option(TECFLIX_BUILD_TESTS "Build unit/integration tests" ON)

find_package(Threads REQUIRED)

add_library(tecflix_core STATIC
  src/Config.cpp src/Metrics.cpp src/VideoStorage.cpp src/Transcoder.cpp
  src/JobQueue.cpp src/AccessChecker.cpp src/Replicator.cpp
  src/StreamingService.cpp src/ApiServer.cpp)
target_include_directories(tecflix_core PUBLIC include third_party)
target_compile_options(tecflix_core PRIVATE -Wall -Wextra)
target_link_libraries(tecflix_core PUBLIC Threads::Threads)

add_executable(tecflix_streaming src/main.cpp)
target_link_libraries(tecflix_streaming PRIVATE tecflix_core)

if(TECFLIX_COVERAGE)
  target_compile_options(tecflix_core PUBLIC --coverage -O0 -g)
  target_link_options(tecflix_core PUBLIC --coverage)
endif()

if(TECFLIX_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  file(GLOB TEST_SOURCES tests/*.cpp)
  add_executable(tecflix_tests ${TEST_SOURCES})
  target_link_libraries(tecflix_tests PRIVATE tecflix_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(tecflix_tests DISCOVERY_TIMEOUT 60)
endif()
