# The library: the four adaptive predicates in one compiled translation unit.
#
# Exactness depends on the floating-point flags of predicates.cpp, so the
# project sets them here (shewchuk_strict_fp) and they come last on the
# command line, after any flags of the user or of the consuming project. Code
# that calls the library may be compiled with any flags.
add_library(shewchuk_predicates STATIC predicates.cpp)
add_library(shewchuk::predicates ALIAS shewchuk_predicates)
target_include_directories(shewchuk_predicates PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_features(shewchuk_predicates PRIVATE cxx_std_20)
target_link_libraries(shewchuk_predicates PRIVATE shewchuk_strict_fp shewchuk_sanitize)
if(MSVC)
  target_compile_options(shewchuk_predicates PRIVATE /W3)
else()
  target_compile_options(shewchuk_predicates PRIVATE -Wall -Wextra)
endif()
