# Exactness test suite. See docs/testing.md.

# --- Stage-instrumented reference (generated at build time, never checked in)
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(staged_source "${generated_dir}/predicates_staged.c")
add_custom_command(
  OUTPUT "${staged_source}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
  COMMAND instrument_stages "${SHEWCHUK_REFERENCE_DIR}/predicates.c" "${staged_source}"
  DEPENDS instrument_stages "${SHEWCHUK_REFERENCE_DIR}/predicates.c"
  COMMENT "Generating the stage-instrumented copy of predicates.c"
  VERBATIM)
add_custom_target(shewchuk_staged_source DEPENDS "${staged_source}")

shewchuk_add_reference(shewchuk_ref_staged shewchuk_staged_ "${generated_dir}")
target_compile_definitions(shewchuk_ref_staged PRIVATE SHEWCHUK_REF_STAGED)
target_link_libraries(shewchuk_ref_staged PRIVATE shewchuk_strict_fp)
add_dependencies(shewchuk_ref_staged shewchuk_staged_source)

# --- Negative control: the pristine file with cross-statement contraction
# This is what plain `gcc -O2` does in GNU mode and what `clang
# -ffp-contract=fast` does. It returns wrong signs, and the suite must notice.
# -O2 is forced because the miscompilation depends on the optimization level
# (clang happens to produce correct code at -O0 and -O3).
# The architecture being compiled for. In a macOS cross build (universal or
# Rosetta) CMAKE_SYSTEM_PROCESSOR is the host's.
set(target_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_OSX_ARCHITECTURES)
  set(target_arch "${CMAKE_OSX_ARCHITECTURES}")
endif()
set(contracted_flags "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  if(target_arch MATCHES "^(aarch64|arm64|ARM64)$")
    set(contracted_flags -O2 -ffp-contract=fast)
  elseif(target_arch MATCHES "^(x86_64|AMD64|amd64)$")
    # Baseline x86-64 has no fused multiply-add; enable it so that the
    # compiler has something to contract to. Checked at run time.
    set(contracted_flags -O2 -ffp-contract=fast -mfma)
  endif()
endif()

# --- The library's test-only build (src/test_hooks.h)
# The same source and the same flags as the shipped library, plus stage
# recording and exported expansion routines, under different symbol names.
add_library(shewchuk_predicates_hooked STATIC "${PROJECT_SOURCE_DIR}/src/predicates.cpp")
target_compile_definitions(shewchuk_predicates_hooked PRIVATE SHEWCHUK_PREDICATES_TEST_HOOKS)
target_include_directories(shewchuk_predicates_hooked PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(shewchuk_predicates_hooked PRIVATE shewchuk_strict_fp shewchuk_sanitize)

# --- Support library shared by the test executables
add_library(shewchuk_test_support STATIC
  generators.cpp implementations.cpp oracle.cpp oracle_gmp.cpp)
target_include_directories(shewchuk_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(shewchuk_test_support PUBLIC
  shewchuk_ref shewchuk_ref_staged shewchuk_predicates shewchuk_predicates_hooked
  shewchuk_strict_fp shewchuk_sanitize)
if(MSVC)
  target_compile_options(shewchuk_test_support PUBLIC /W3)
else()
  target_compile_options(shewchuk_test_support PUBLIC -Wall -Wextra)
endif()

if(contracted_flags)
  shewchuk_add_reference(shewchuk_ref_contracted shewchuk_contracted_)
  target_compile_options(shewchuk_ref_contracted PRIVATE ${contracted_flags})
  target_link_libraries(shewchuk_test_support PUBLIC shewchuk_ref_contracted)
  target_compile_definitions(shewchuk_test_support PRIVATE SHEWCHUK_HAVE_CONTRACTED_REFERENCE=1)
else()
  # The test is still registered and reports itself as skipped.
  message(STATUS "The contraction negative control is not available for "
    "${CMAKE_C_COMPILER_ID} on ${target_arch}: negative_control.fp_contract will be skipped")
  target_compile_definitions(shewchuk_test_support PRIVATE SHEWCHUK_HAVE_CONTRACTED_REFERENCE=0)
endif()

find_package(GMP)
if(GMP_FOUND)
  target_link_libraries(shewchuk_test_support PRIVATE GMP::GMP)
  target_compile_definitions(shewchuk_test_support PRIVATE SHEWCHUK_HAVE_GMP=1)
elseif(SHEWCHUK_REQUIRE_GMP)
  message(FATAL_ERROR "SHEWCHUK_REQUIRE_GMP is ON but GMP was not found")
else()
  message(STATUS "GMP not found: the oracle.gmp_crosscheck test will be skipped")
  target_compile_definitions(shewchuk_test_support PRIVATE SHEWCHUK_HAVE_GMP=0)
endif()

# --- Test executables
foreach(name test_oracle test_oracle_gmp test_stage_coverage differential
    test_constants test_expansions test_threads test_orient2d_first_test)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE shewchuk_test_support)
  if(MSVC)
    # insphereexact() alone needs about 180 KB of stack.
    target_link_options(${name} PRIVATE /STACK:8388608)
  endif()
endforeach()

find_package(Threads REQUIRED)
target_link_libraries(test_threads PRIVATE Threads::Threads)

# The public headers alone, from C++ and from C, with the default flags.
add_executable(test_api test_api.cpp)
target_link_libraries(test_api PRIVATE shewchuk::predicates shewchuk_sanitize)
add_executable(test_api_c test_api_c.c)
target_link_libraries(test_api_c PRIVATE shewchuk::predicates shewchuk_sanitize)
set_target_properties(test_api_c PROPERTIES LINKER_LANGUAGE CXX)

# --- Tests
# Exit status 77 means "skipped", and is reported as such.
function(shewchuk_add_test name)
  add_test(NAME ${name} COMMAND ${ARGN})
  set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
  if(SHEWCHUK_SANITIZE)
    # A small quarantine keeps AddressSanitizer's memory use around 100 MB per
    # test process; with the default (256 MB) the oracle's many small
    # allocations push it past 800 MB.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
      "ASAN_OPTIONS=quarantine_size_mb=16:detect_stack_use_after_return=1:strict_string_checks=1;UBSAN_OPTIONS=print_stacktrace=1;TSAN_OPTIONS=halt_on_error=1")
  endif()
endfunction()

shewchuk_add_test(reference.sha256
  "${CMAKE_COMMAND}" "-DSUMS=${SHEWCHUK_REFERENCE_DIR}/SHA256SUMS"
  -P "${PROJECT_SOURCE_DIR}/cmake/VerifySha256.cmake")

if(CMAKE_NM AND NOT MSVC)
  shewchuk_add_test(reference.symbol_prefix
    "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DLIBRARY=$<TARGET_FILE:shewchuk_ref>"
    -DPREFIX=shewchuk_ref_ -P "${PROJECT_SOURCE_DIR}/cmake/CheckSymbolPrefix.cmake")
endif()

shewchuk_add_test(oracle.unit test_oracle)
shewchuk_add_test(oracle.gmp_crosscheck test_oracle_gmp)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  shewchuk_add_test(oracle.python_crosscheck
    "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/oracle_check.py"
    "$<TARGET_FILE:differential>")
else()
  message(STATUS "Python 3 not found: the oracle.python_crosscheck test is not registered")
endif()

shewchuk_add_test(stages.coverage test_stage_coverage quick)
shewchuk_add_test(differential.reference
  differential --impl reference --tier quick --check-golden)
shewchuk_add_test(differential.reference_staged
  differential --impl reference_staged --tier quick --check-golden --require-identical)
shewchuk_add_test(negative_control.fp_contract
  differential --impl reference_contracted --tier quick --check-golden --expect-mismatch)

# --- Tests of the library (src/, include/)
shewchuk_add_test(library.constants test_constants)
shewchuk_add_test(library.expansions test_expansions)
shewchuk_add_test(library.api test_api)
shewchuk_add_test(library.api_c test_api_c)
shewchuk_add_test(library.threads test_threads)
shewchuk_add_test(library.stages test_stage_coverage quick library)
shewchuk_add_test(library.orient2d_first_test test_orient2d_first_test)
# The library is declared refactor-only: every value must equal the
# reference's, and the value hash must equal the recorded one.
shewchuk_add_test(differential.library
  differential --impl library --tier quick --check-golden --require-identical)
shewchuk_add_test(differential.library_hooked
  differential --impl library_hooked --tier quick --check-golden --require-identical)

# The shipped library exports the four predicates and nothing else. (Skipped
# in sanitizer builds, whose runtime adds symbols of its own.)
if(CMAKE_NM AND NOT MSVC AND NOT SHEWCHUK_SANITIZE)
  shewchuk_add_test(library.symbols
    "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DLIBRARY=$<TARGET_FILE:shewchuk_predicates>"
    -DPREFIX=shewchuk_ -DEXPECTED_COUNT=4 -P "${PROJECT_SOURCE_DIR}/cmake/CheckSymbolPrefix.cmake")
endif()

# No fused multiply-add in the library's object code (cmake/CheckNoFma.cmake).
set(disassembler_args "")
if(MSVC)
  get_filename_component(linker_dir "${CMAKE_LINKER}" DIRECTORY)
  find_program(SHEWCHUK_DUMPBIN dumpbin HINTS "${linker_dir}")
  if(SHEWCHUK_DUMPBIN)
    set(disassembler_args "-DDUMPBIN=${SHEWCHUK_DUMPBIN}")
  endif()
elseif(CMAKE_OBJDUMP)
  set(disassembler_args "-DOBJDUMP=${CMAKE_OBJDUMP}")
else()
  find_program(SHEWCHUK_OBJDUMP NAMES llvm-objdump objdump)
  find_program(SHEWCHUK_OTOOL otool)
  if(SHEWCHUK_OBJDUMP)
    set(disassembler_args "-DOBJDUMP=${SHEWCHUK_OBJDUMP}")
  elseif(SHEWCHUK_OTOOL)
    set(disassembler_args "-DOTOOL=${SHEWCHUK_OTOOL}")
  endif()
endif()
if(CMAKE_NM AND NOT MSVC)
  list(APPEND disassembler_args "-DNM=${CMAKE_NM}")
endif()
shewchuk_add_test(library.no_fma
  "${CMAKE_COMMAND}" "-DOBJECT=$<TARGET_OBJECTS:shewchuk_predicates>" "-DARCH=${target_arch}"
  ${disassembler_args} -P "${PROJECT_SOURCE_DIR}/cmake/CheckNoFma.cmake")
set_tests_properties(library.no_fma PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
if(contracted_flags)
  # The check must see the fused multiply-adds of the contracted reference.
  shewchuk_add_test(negative_control.no_fma
    "${CMAKE_COMMAND}" "-DOBJECT=$<TARGET_OBJECTS:shewchuk_ref_contracted>" "-DARCH=${target_arch}"
    ${disassembler_args} -DEXPECT_FMA=ON -P "${PROJECT_SOURCE_DIR}/cmake/CheckNoFma.cmake")
  set_tests_properties(negative_control.no_fma PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
endif()

# The library refuses to compile for x87 extended-precision arithmetic
# (cmake/CheckRefusesX87.cmake). Skipped where the compiler has no such mode.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  shewchuk_add_test(library.refuses_x87
    "${CMAKE_COMMAND}" "-DCXX=${CMAKE_CXX_COMPILER}" "-DFLAGS=${CMAKE_CXX_FLAGS}"
    "-DROOT=${PROJECT_SOURCE_DIR}"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/refuses_x87"
    -P "${PROJECT_SOURCE_DIR}/cmake/CheckRefusesX87.cmake")
  set_tests_properties(library.refuses_x87 PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:")
endif()

if(SHEWCHUK_DEEP_TESTS)
  shewchuk_add_test(deep.library.stages test_stage_coverage deep library)
  shewchuk_add_test(deep.library.expansions test_expansions 60000)
  shewchuk_add_test(deep.differential.library
    differential --impl library --tier deep --check-golden --require-identical)
  set_tests_properties(deep.library.stages deep.library.expansions deep.differential.library
    PROPERTIES TIMEOUT 3600 LABELS deep)
  shewchuk_add_test(deep.stages.coverage test_stage_coverage deep)
  shewchuk_add_test(deep.differential.reference
    differential --impl reference --tier deep --check-golden)
  shewchuk_add_test(deep.negative_control.fp_contract
    differential --impl reference_contracted --tier deep --check-golden --expect-mismatch)
  set_tests_properties(deep.stages.coverage deep.differential.reference
    deep.negative_control.fp_contract PROPERTIES TIMEOUT 3600 LABELS deep)
endif()
