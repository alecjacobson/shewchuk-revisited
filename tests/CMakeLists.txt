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
    # AVX2 is turned off because clang, when it may use AVX2 (for example
    # with -march=x86-64-v3 in the user's flags), contracts this file in a
    # way that changes thousands of returned values but not one sign on the
    # corpus: such a build would not be a negative control for the sign test.
    set(contracted_flags -O2 -ffp-contract=fast -mfma -mno-avx2)
  endif()
endif()

# --- The library's test-only build (src/test_hooks.h)
# The same source and the same flags as the shipped library, plus stage
# recording and exported expansion routines, under different symbol names.
add_library(shewchuk_predicates_hooked STATIC "${PROJECT_SOURCE_DIR}/src/predicates.cpp")
target_compile_definitions(shewchuk_predicates_hooked PRIVATE SHEWCHUK_PREDICATES_TEST_HOOKS)
target_include_directories(shewchuk_predicates_hooked PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(shewchuk_predicates_hooked PRIVATE shewchuk_strict_fp shewchuk_sanitize)

# --- Further test-only builds of the library under other symbol names
# The same source and flags as the shipped library plus the given compile
# definitions; the four predicates are exported as <prefix>orient2d etc.
function(shewchuk_add_library_variant target prefix)
  add_library(${target} STATIC "${PROJECT_SOURCE_DIR}/src/predicates.cpp")
  target_compile_definitions(${target} PRIVATE ${ARGN}
    shewchuk_orient2d=${prefix}orient2d shewchuk_orient3d=${prefix}orient3d
    shewchuk_incircle=${prefix}incircle shewchuk_insphere=${prefix}insphere)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(${target} PRIVATE shewchuk_strict_fp shewchuk_sanitize)
endfunction()
# Without the exact integer stage: must return the 1996 values bit for bit.
shewchuk_add_library_variant(shewchuk_predicates_expansion_only shewchuk_expansion_only_
  SHEWCHUK_PREDICATES_NO_INTEGER_STAGE)
# With the integer stage's portable arithmetic (the one compilers without a
# 128-bit integer and bit-counting builtins get) forced, so that it is tested
# on every platform.
shewchuk_add_library_variant(shewchuk_predicates_portable shewchuk_portable_
  SHEWCHUK_INTEGER_STAGE_PORTABLE)

# --- Support library shared by the test executables
add_library(shewchuk_test_support STATIC
  generators.cpp implementations.cpp oracle.cpp oracle_gmp.cpp)
target_include_directories(shewchuk_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(shewchuk_test_support PUBLIC
  shewchuk_ref shewchuk_ref_staged shewchuk_predicates shewchuk_predicates_hooked
  shewchuk_predicates_expansion_only shewchuk_predicates_portable
  shewchuk_strict_fp shewchuk_sanitize)
if(TARGET shewchuk_comparators)
  target_link_libraries(shewchuk_test_support PRIVATE shewchuk_comparators)
endif()
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

# --- Negative control: the pristine file with the compiler's default flags
# Plain `gcc -O2`: no floating-point flag at all. GCC's GNU dialects (the
# default, and what predicates.c needs) imply -ffp-contract=fast. Clang's
# default (-ffp-contract=on) contracts only within one expression; that changes
# returned values but has not been seen to change a sign, so there is no such
# control for clang.
set(default_flags "")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  if(target_arch MATCHES "^(aarch64|arm64|ARM64)$")
    set(default_flags -O2)
  elseif(target_arch MATCHES "^(x86_64|AMD64|amd64)$")
    # As above: baseline x86-64 has no fused multiply-add (-march=x86-64-v3
    # and -march=native usually do). Checked at run time.
    set(default_flags -O2 -mfma)
  endif()
endif()
if(default_flags)
  shewchuk_add_reference(shewchuk_ref_default_flags shewchuk_default_flags_)
  target_compile_options(shewchuk_ref_default_flags PRIVATE ${default_flags})
  target_link_libraries(shewchuk_test_support PUBLIC shewchuk_ref_default_flags)
  target_compile_definitions(shewchuk_test_support PRIVATE SHEWCHUK_HAVE_DEFAULT_FLAGS_REFERENCE=1)
else()
  # The test is still registered and reports itself as skipped.
  message(STATUS "The default-flags negative control is not available for "
    "${CMAKE_C_COMPILER_ID} on ${target_arch}: negative_control.default_flags will be skipped")
  target_compile_definitions(shewchuk_test_support PRIVATE SHEWCHUK_HAVE_DEFAULT_FLAGS_REFERENCE=0)
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
    test_constants test_expansions test_threads test_orient2d_first_test
    test_input_domain)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE shewchuk_test_support)
  if(MSVC)
    # insphereexact() alone needs about 180 KB of stack.
    target_link_options(${name} PRIVATE /STACK:8388608)
  endif()
endforeach()

# The integer stage on its own (src/integer_stage.h is compiled into the test),
# in each configuration of its limb arithmetic, with and without the run-time
# verification of the proof's bounds.
function(shewchuk_add_integer_stage_test target)
  add_executable(${target} test_integer_stage.cpp)
  target_link_libraries(${target} PRIVATE shewchuk_test_support)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_compile_definitions(${target} PRIVATE SHEWCHUK_INTEGER_STAGE_TEST_HOOKS ${ARGN})
  if(MSVC)
    # It also calls the library, whose insphereexact() needs about 180 KB.
    target_link_options(${target} PRIVATE /STACK:8388608)
  endif()
endfunction()
shewchuk_add_integer_stage_test(test_integer_stage)
shewchuk_add_integer_stage_test(test_integer_stage_portable SHEWCHUK_INTEGER_STAGE_PORTABLE)
shewchuk_add_integer_stage_test(test_integer_stage_checked SHEWCHUK_INTEGER_STAGE_CHECKED)
shewchuk_add_integer_stage_test(test_integer_stage_checked_portable
  SHEWCHUK_INTEGER_STAGE_CHECKED SHEWCHUK_INTEGER_STAGE_PORTABLE)

find_package(Threads REQUIRED)
target_link_libraries(test_threads PRIVATE Threads::Threads)

# The public headers alone, from C++ and from C, with the default flags.
add_executable(test_api test_api.cpp)
target_link_libraries(test_api PRIVATE shewchuk::predicates shewchuk_sanitize)
add_executable(test_api_c test_api_c.c)
target_link_libraries(test_api_c PRIVATE shewchuk::predicates shewchuk_sanitize)
set_target_properties(test_api_c PROPERTIES LINKER_LANGUAGE CXX)

# --- A fast-math caller with link-time optimization (test_lto_caller.cpp)
# Two more builds of the library, each with a caller (lto_caller.cpp) compiled
# with fast-math flags, all link-time optimized and linked on a link line that
# carries the fast-math flags too:
#   exact       what a consumer gets who puts fast-math flags in
#               CMAKE_CXX_FLAGS and turns on interprocedural optimization: the
#               consumer's flags first, the project's strict flags last;
#   contracted  NEGATIVE CONTROL: the strict flags replaced by
#               -ffp-contract=fast.
# The predicates are renamed on the command line so that these builds can be
# linked next to the shipped library.
add_executable(test_lto_caller test_lto_caller.cpp)
target_link_libraries(test_lto_caller PRIVATE shewchuk_test_support)
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_output LANGUAGES CXX)
if(lto_supported)
  if(MSVC)
    set(fast_math_flags /fp:fast)
    set(lto_target_flags "")
  else()
    set(fast_math_flags -O3 -ffast-math)
    set(lto_target_flags "")
    if(target_arch MATCHES "^(x86_64|AMD64|amd64)$")
      # So that the compiler has an instruction to contract to. Checked at run
      # time.
      set(lto_target_flags -mfma)
      target_compile_definitions(test_lto_caller PRIVATE SHEWCHUK_LTO_CALLER_USES_FMA=1)
    endif()
    target_link_options(test_lto_caller PRIVATE ${fast_math_flags})
  endif()

  # shewchuk_add_lto_caller(<tag>): the library and its caller as two
  # link-time-optimized object libraries, added to test_lto_caller. The caller
  # is the function shewchuk_lto_caller_<tag>().
  function(shewchuk_add_lto_caller tag)
    set(library shewchuk_predicates_lto_${tag})
    set(caller shewchuk_lto_caller_${tag})
    add_library(${library} OBJECT "${PROJECT_SOURCE_DIR}/src/predicates.cpp")
    add_library(${caller} OBJECT lto_caller.cpp)
    target_compile_definitions(${caller} PRIVATE SHEWCHUK_LTO_CALLER=${caller})
    target_compile_options(${caller} PRIVATE ${fast_math_flags} ${lto_target_flags})
    foreach(target ${library} ${caller})
      foreach(predicate orient2d orient3d incircle insphere)
        target_compile_definitions(${target} PRIVATE
          shewchuk_${predicate}=shewchuk_lto_${tag}_${predicate})
      endforeach()
      target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
      set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
      target_sources(test_lto_caller PRIVATE $<TARGET_OBJECTS:${target}>)
    endforeach()
  endfunction()

  # A target's own options precede those of the interface libraries it links,
  # so the strict flags come last here, as they do in the shipped library.
  shewchuk_add_lto_caller(exact)
  target_compile_options(shewchuk_predicates_lto_exact PRIVATE
    ${fast_math_flags} ${lto_target_flags})
  target_link_libraries(shewchuk_predicates_lto_exact PRIVATE shewchuk_strict_fp)
  set_target_properties(test_lto_caller PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
  target_compile_definitions(test_lto_caller PRIVATE SHEWCHUK_HAVE_LTO_CALLER=1)

  if(contracted_flags)
    # No fast-math flags on this copy of the library: its #error guards would
    # reject them. Contraction defines no macro, so nothing rejects that.
    shewchuk_add_lto_caller(contracted)
    target_compile_options(shewchuk_predicates_lto_contracted PRIVATE ${contracted_flags})
    target_compile_definitions(test_lto_caller PRIVATE SHEWCHUK_HAVE_LTO_CALLER_CONTRACTED=1)
  endif()
else()
  # The tests are still registered and report themselves as skipped.
  message(STATUS "Link-time optimization is not available (${lto_output}): "
    "library.lto_fast_math_caller will be skipped")
endif()

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
shewchuk_add_test(negative_control.default_flags
  differential --impl reference_default_flags --tier quick --check-golden --expect-mismatch)

# --- Tests of the library (src/, include/)
shewchuk_add_test(library.constants test_constants)
shewchuk_add_test(library.expansions test_expansions)
shewchuk_add_test(library.api test_api)
shewchuk_add_test(library.api_c test_api_c)
shewchuk_add_test(library.threads test_threads)
shewchuk_add_test(library.stages test_stage_coverage quick library)
shewchuk_add_test(library.orient2d_first_test test_orient2d_first_test)
shewchuk_add_test(library.input_domain test_input_domain)
# Without its integer stage the library is a refactoring of the 1996 code:
# every value must equal the reference's, and the value hash must equal the
# recorded one.
shewchuk_add_test(differential.library_expansion_only
  differential --impl library_expansion_only --tier quick --check-golden --require-identical)
# With it (the shipped library): zero sign mismatches, and every value is the
# one the integer-stage contract fixes (differential.cpp): the reference's
# when the first filter or a 1996 stage decides, exactly -1, 0 or +1 when the
# integer stage does. The hash of those values is recorded too, and the build
# with the portable multiplication must reproduce it.
shewchuk_add_test(differential.library
  differential --impl library --tier quick --check-golden --integer-stage)
shewchuk_add_test(differential.library_hooked
  differential --impl library_hooked --tier quick --check-golden --integer-stage)
shewchuk_add_test(differential.library_portable
  differential --impl library_portable --tier quick --check-golden --integer-stage)

# --- Comparators (third-party code, docs/comparators.md)
# Every wrapper against the exact oracle on the whole quick corpus: this is
# what shows that its argument order and sign convention were adapted
# correctly. A comparator recorded as returning wrong signs is run with
# --known-wrong: the test prints them and fails if they disappear or if any
# appears on uniform random input.
if(GMP_FOUND)
  # About 50 microseconds per insphere call: a tenth of the quick corpus.
  shewchuk_add_test(comparator.gmp_rational
    differential --impl gmp_rational --cases-per-generator 1000 --comparator)
endif()
foreach(name geogram cgal indirect)
  if(TARGET shewchuk_comparator_${name})
    shewchuk_add_test(comparator.${name}
      differential --impl ${name} --tier quick --check-golden --comparator)
  endif()
endforeach()
if(TARGET shewchuk_comparator_geometric_predicates)
  shewchuk_add_test(comparator.geometric_predicates
    differential --impl geometric_predicates --tier quick --check-golden --comparator --known-wrong)
endif()

# The integer stage on its own; see test_integer_stage.cpp.
shewchuk_add_test(integer_stage.unit test_integer_stage quick)
shewchuk_add_test(integer_stage.portable test_integer_stage_portable quick)
shewchuk_add_test(integer_stage.checked test_integer_stage_checked quick)
shewchuk_add_test(integer_stage.checked_portable test_integer_stage_checked_portable quick)
if(Python3_FOUND)
  # The bit widths of docs/proofs/integer-stage.md, re-derived with exact
  # integers from the expressions and compared with the header's formulas.
  shewchuk_add_test(integer_stage.widths
    "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/integer_stage_widths.py"
    "${PROJECT_SOURCE_DIR}/src/integer_stage.h")
endif()

# The caller's flags and link-time optimization do not reach the library's
# arithmetic; and the test notices when they do.
shewchuk_add_test(library.lto_fast_math_caller test_lto_caller exact)
shewchuk_add_test(negative_control.lto_fast_math_caller test_lto_caller contracted)

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
  shewchuk_add_test(deep.library.input_domain test_input_domain deep)
  shewchuk_add_test(deep.library.expansions test_expansions 60000)
  shewchuk_add_test(deep.differential.library
    differential --impl library --tier deep --check-golden --integer-stage)
  shewchuk_add_test(deep.differential.library_expansion_only
    differential --impl library_expansion_only --tier deep --check-golden --require-identical)
  shewchuk_add_test(deep.differential.library_portable
    differential --impl library_portable --tier deep --check-golden --integer-stage)
  shewchuk_add_test(deep.integer_stage.checked test_integer_stage_checked deep)
  shewchuk_add_test(deep.integer_stage.checked_portable test_integer_stage_checked_portable deep)
  set_tests_properties(deep.library.stages deep.library.input_domain deep.library.expansions
    deep.differential.library
    deep.differential.library_expansion_only deep.differential.library_portable
    deep.integer_stage.checked deep.integer_stage.checked_portable
    PROPERTIES TIMEOUT 3600 LABELS deep)
  shewchuk_add_test(deep.stages.coverage test_stage_coverage deep)
  shewchuk_add_test(deep.differential.reference
    differential --impl reference --tier deep --check-golden)
  shewchuk_add_test(deep.negative_control.fp_contract
    differential --impl reference_contracted --tier deep --check-golden --expect-mismatch)
  set_tests_properties(deep.stages.coverage deep.differential.reference
    deep.negative_control.fp_contract PROPERTIES TIMEOUT 3600 LABELS deep)
endif()
