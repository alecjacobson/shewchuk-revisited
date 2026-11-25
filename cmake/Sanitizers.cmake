# shewchuk_sanitize: the sanitizers selected with -DSHEWCHUK_SANITIZE=<list>
# (for example address,undefined or thread), as an interface target.
#
# The library and the test executables link it. The pristine reference
# deliberately does not: predicates.c reads one element past the end of an
# array in fast_expansion_sum_zeroelim (line 1061; the value is unused), which
# AddressSanitizer rightly reports, and that file is never edited. This is
# why the sanitizers are a project option instead of global compiler flags.
add_library(shewchuk_sanitize INTERFACE)
if(SHEWCHUK_SANITIZE)
  if(MSVC)
    message(FATAL_ERROR "SHEWCHUK_SANITIZE is only supported with GCC and clang")
  endif()
  target_compile_options(shewchuk_sanitize INTERFACE
    -fsanitize=${SHEWCHUK_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
  target_link_options(shewchuk_sanitize INTERFACE -fsanitize=${SHEWCHUK_SANITIZE})
  message(STATUS "Sanitizers for the library and the tests: ${SHEWCHUK_SANITIZE}")
endif()
