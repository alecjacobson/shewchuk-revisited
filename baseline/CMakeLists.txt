# The pristine 1996 predicates.c as a library with prefixed symbols.

# Refuse to configure if the reference file is not the pinned one.
file(STRINGS "${SHEWCHUK_REFERENCE_DIR}/SHA256SUMS" sums_line LIMIT_COUNT 1)
string(REGEX MATCH "^[0-9a-f]+" expected_sha256 "${sums_line}")
file(SHA256 "${SHEWCHUK_REFERENCE_DIR}/predicates.c" actual_sha256)
if(NOT actual_sha256 STREQUAL expected_sha256)
  message(FATAL_ERROR
    "reference/predicates.c does not match reference/SHA256SUMS\n"
    "  expected ${expected_sha256}\n  actual   ${actual_sha256}\n"
    "The reference file must never be edited.")
endif()

# shewchuk_add_reference(<target> <prefix> [<include dir>...])
#
# Builds reference/predicates.c (through the wrapper shewchuk_ref.c) as a
# static library whose external symbols all start with <prefix>. The caller
# chooses the floating-point flags: link shewchuk_strict_fp for an exact build.
function(shewchuk_add_reference target prefix)
  add_library(${target} STATIC "${PROJECT_SOURCE_DIR}/baseline/shewchuk_ref.c")
  target_compile_definitions(${target} PRIVATE SHEWCHUK_REF_PREFIX=${prefix})
  target_include_directories(${target}
    PRIVATE ${ARGN} "${SHEWCHUK_REFERENCE_DIR}"
    PUBLIC "${PROJECT_SOURCE_DIR}/baseline")
  if(MSVC)
    # MSVC's C compiler accepts the K&R definitions as is. It lacks
    # <sys/time.h>, which predicates.c includes without using.
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/cmake/msvc_compat")
    target_compile_options(${target} PRIVATE /w)
  else()
    # predicates.c uses K&R function definitions (removed in C23) and POSIX
    # random(), so it needs an old GNU dialect.
    set_target_properties(${target} PROPERTIES
      C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
    target_compile_options(${target} PRIVATE -w)
  endif()
endfunction()

shewchuk_add_reference(shewchuk_ref shewchuk_ref_)
target_link_libraries(shewchuk_ref PRIVATE shewchuk_strict_fp)
