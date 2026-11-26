cmake_minimum_required(VERSION 3.20)
project(shewchuk_revisited LANGUAGES C CXX)

# Is this the top-level project, or is it being consumed with
# add_subdirectory() / FetchContent?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(shewchuk_is_top_level ON)
else()
  set(shewchuk_is_top_level OFF)
endif()

if(shewchuk_is_top_level AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests are off by default for a consumer.
option(SHEWCHUK_BUILD_TESTS "Build the exactness test suite" ${shewchuk_is_top_level})
option(SHEWCHUK_DEEP_TESTS "Also register the deep (slow) test tier" OFF)
option(SHEWCHUK_REQUIRE_GMP "Fail the configuration if GMP is not found" OFF)
option(SHEWCHUK_BUILD_BENCH "Build the benchmark harness (needs the test suite)"
  ${shewchuk_is_top_level})
set(SHEWCHUK_SANITIZE "" CACHE STRING
  "Sanitizers for the library and the tests, e.g. address,undefined or thread")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(StrictFloatingPoint)
include(Sanitizers)

# The pristine 1996 file lives here and is never edited.
set(SHEWCHUK_REFERENCE_DIR "${PROJECT_SOURCE_DIR}/reference")

if(shewchuk_is_top_level)
  add_subdirectory(baseline)
else()
  add_subdirectory(baseline EXCLUDE_FROM_ALL)
endif()

# The library: the modern port of the four adaptive predicates.
add_subdirectory(src)

if(SHEWCHUK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tools)
  add_subdirectory(tests)
  if(SHEWCHUK_BUILD_BENCH)
    add_subdirectory(bench)
  endif()
endif()
