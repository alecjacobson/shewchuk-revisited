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
set(contracted_flags "")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(contracted_flags -O2 -ffp-contract=fast)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    # Baseline x86-64 has no fused multiply-add; enable it so that the
    # compiler has something to contract to. Checked at run time.
    set(contracted_flags -O2 -ffp-contract=fast -mfma)
  endif()
endif()

# --- Support library shared by the test executables
add_library(shewchuk_test_support STATIC
  generators.cpp implementations.cpp oracle.cpp oracle_gmp.cpp)
target_include_directories(shewchuk_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(shewchuk_test_support PUBLIC shewchuk_ref shewchuk_ref_staged shewchuk_strict_fp)
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
    "${CMAKE_C_COMPILER_ID} on ${CMAKE_SYSTEM_PROCESSOR}: negative_control.fp_contract will be skipped")
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
foreach(name test_oracle test_oracle_gmp test_stage_coverage differential)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE shewchuk_test_support)
  if(MSVC)
    # insphereexact() alone needs about 180 KB of stack.
    target_link_options(${name} PRIVATE /STACK:8388608)
  endif()
endforeach()

# --- Tests
# Exit status 77 means "skipped", and is reported as such.
function(shewchuk_add_test name)
  add_test(NAME ${name} COMMAND ${ARGN})
  set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
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

if(SHEWCHUK_DEEP_TESTS)
  shewchuk_add_test(deep.stages.coverage test_stage_coverage deep)
  shewchuk_add_test(deep.differential.reference
    differential --impl reference --tier deep --check-golden)
  shewchuk_add_test(deep.negative_control.fp_contract
    differential --impl reference_contracted --tier deep --check-golden --expect-mismatch)
  set_tests_properties(deep.stages.coverage deep.differential.reference
    deep.negative_control.fp_contract PROPERTIES TIMEOUT 3600 LABELS deep)
endif()
