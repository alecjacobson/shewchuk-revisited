# cmake -DOBJECT=<object file> -DARCH=<processor> [-DOBJDUMP=<objdump>]
#       [-DOTOOL=<otool>] [-DDUMPBIN=<dumpbin>] [-DNM=<nm>] [-DEXPECT_FMA=ON]
#       -P CheckNoFma.cmake
#
# Disassembles one object file and fails if it contains a fused multiply-add
# instruction or a call to the C library's fma(). A fused multiply-add in the
# predicates' translation unit means the compiler contracted a*b+c, which
# breaks the error-free transformations the predicates are built from.
#
# With -DEXPECT_FMA=ON the verdict is inverted (negative control: an object
# that was deliberately built with contraction must be caught).
#
# Prints a line starting with "SKIPPED:" and exits successfully when the check
# cannot be made (unknown architecture, no disassembler); the test that runs
# this script declares that line as its skip marker, so it is reported as
# skipped, never as passed.

if(ARCH MATCHES "^(aarch64|arm64|ARM64)$")
  # Scalar and vector fused multiply-add / multiply-subtract.
  set(fma_regex "[ \t](fmadd|fmsub|fnmadd|fnmsub|fmla|fmls)[ \t]")
  set(multiply_regex "[ \t]fmul[ \t]")
elseif(ARCH MATCHES "^(x86_64|AMD64|amd64|x64)$")
  # FMA3 (vfmadd132sd, vfnmsub231pd, ...) and FMA4 (vfmaddsd, ...).
  set(fma_regex "[ \t]vfn?m(add|sub)(add|sub)?[0-9]*[sp][sd][ \t]")
  set(multiply_regex "[ \t]v?mul[sp]d[ \t]")
else()
  message(STATUS "SKIPPED: no fused multiply-add patterns are defined for the architecture '${ARCH}'")
  return()
endif()

if(OBJDUMP)
  set(command "${OBJDUMP}" -d "${OBJECT}")
elseif(OTOOL)
  set(command "${OTOOL}" -tV "${OBJECT}")
elseif(DUMPBIN)
  set(command "${DUMPBIN}" /nologo /disasm "${OBJECT}")
else()
  message(STATUS "SKIPPED: no disassembler (objdump, llvm-objdump, otool or dumpbin) was found")
  return()
endif()

execute_process(COMMAND ${command}
  OUTPUT_VARIABLE disassembly ERROR_VARIABLE errors RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "disassembler failed on ${OBJECT}: ${errors}")
endif()
# Semicolons would split the text into list elements.
string(REPLACE ";" ":" disassembly "${disassembly}")

# The disassembly must be understood before "no match" means anything: the
# predicates contain hundreds of floating-point multiplications.
string(REGEX MATCHALL "${multiply_regex}" multiplies "${disassembly}")
list(LENGTH multiplies multiply_count)
string(REGEX MATCHALL "${fma_regex}" fused "${disassembly}")
list(LENGTH fused fused_count)
math(EXPR recognized "${multiply_count} + ${fused_count}")
if(recognized LESS 100)
  message(FATAL_ERROR
    "only ${recognized} floating-point multiplications found in ${OBJECT}: "
    "the disassembly was not understood (wrong file, bitcode object, or unknown syntax)")
endif()

# A call to the C library's fma() / fmaf() / fmal().
set(libm_calls 0)
if(NM)
  execute_process(COMMAND "${NM}" -u "${OBJECT}"
    OUTPUT_VARIABLE undefined RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${NM} failed on ${OBJECT}")
  endif()
  string(REGEX MATCHALL "[ \t_]fma[fl]?[ \t\r]*\n" calls " ${undefined}")
  list(LENGTH calls libm_calls)
endif()

message(STATUS "${OBJECT}")
message(STATUS "${multiply_count} floating-point multiplications, "
  "${fused_count} fused multiply-add instructions, ${libm_calls} references to fma()")

math(EXPR total "${fused_count} + ${libm_calls}")
if(EXPECT_FMA)
  if(total EQUAL 0)
    message(FATAL_ERROR
      "negative control was NOT detected: an object built with contraction shows no fused multiply-add")
  endif()
  message(STATUS "negative control detected, as expected")
elseif(NOT total EQUAL 0)
  message(FATAL_ERROR
    "the object contains fused multiply-add: the compiler contracted floating-point "
    "expressions, and the predicates are not exact in this build")
endif()
