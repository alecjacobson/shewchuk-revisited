# cmake -DCXX=<C++ compiler> -DFLAGS=<the build's C++ flags> -DROOT=<project source dir>
#       -DWORK_DIR=<scratch directory> -P CheckRefusesX87.cmake
#
# The library must refuse to compile for x87 arithmetic: with extended
# precision every double operation is rounded twice, and the error-free
# transformations the predicates are built from no longer hold. This script
# compiles the library's source for x87 (the build's flags plus -mfpmath=387)
# and fails unless the compiler stops at the #error on FLT_EVAL_METHOD.
#
# Prints a line starting with "SKIPPED:" and exits successfully when the
# compiler cannot target x87 with these flags (not x86, or a compiler that
# keeps double arithmetic in SSE registers regardless); the test that runs
# this script declares that line as its skip marker.

separate_arguments(flags NATIVE_COMMAND "${FLAGS}")
set(x87_flags ${flags} -mfpmath=387 -std=c++20 -fsyntax-only)

# Can this compiler evaluate double expressions in extended precision at all?
file(MAKE_DIRECTORY "${WORK_DIR}")
set(probe "${WORK_DIR}/x87_probe.cpp")
file(WRITE "${probe}"
  "#include <cfloat>\n"
  "#include <cmath>\n"
  "#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD == 0\n"
  "#error \"double arithmetic is not evaluated in extended precision\"\n"
  "#endif\n")
execute_process(COMMAND "${CXX}" ${x87_flags} "${probe}"
  OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_errors RESULT_VARIABLE probe_result)
if(NOT probe_result EQUAL 0)
  message(STATUS "SKIPPED: ${CXX} with '${FLAGS} -mfpmath=387' does not evaluate double "
    "arithmetic in x87 extended precision:\n${probe_errors}")
  return()
endif()

set(SOURCE "${ROOT}/src/predicates.cpp")
execute_process(COMMAND "${CXX}" ${x87_flags} "-I${ROOT}/include" "-I${ROOT}/src" "${SOURCE}"
  OUTPUT_VARIABLE output ERROR_VARIABLE errors RESULT_VARIABLE result)
if(result EQUAL 0)
  message(FATAL_ERROR
    "${SOURCE} compiled for x87 extended-precision arithmetic: the guard on FLT_EVAL_METHOD "
    "did not stop the compiler, and the predicates are not exact in such a build")
endif()
if(NOT errors MATCHES "the predicates need double arithmetic without extended precision")
  message(FATAL_ERROR
    "the x87 compilation failed, but not at the guard on FLT_EVAL_METHOD:\n${errors}")
endif()
message(STATUS "the library refuses to compile for x87 extended precision, as it must")
