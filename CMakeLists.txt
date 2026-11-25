cmake_minimum_required(VERSION 3.20)
project(shewchuk_revisited LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SHEWCHUK_BUILD_TESTS "Build the exactness test suite" ON)
option(SHEWCHUK_DEEP_TESTS "Also register the deep (slow) test tier" OFF)
option(SHEWCHUK_REQUIRE_GMP "Fail the configuration if GMP is not found" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(StrictFloatingPoint)

# The pristine 1996 file lives here and is never edited.
set(SHEWCHUK_REFERENCE_DIR "${PROJECT_SOURCE_DIR}/reference")

add_subdirectory(baseline)

if(SHEWCHUK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tools)
  add_subdirectory(tests)
endif()
