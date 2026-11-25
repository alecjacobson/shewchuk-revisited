# cmake -DNM=<nm> -DLIBRARY=<static library> -DPREFIX=<prefix>
#       [-DEXPECTED_COUNT=<n>] -P CheckSymbolPrefix.cmake
# Fails if the library defines an external symbol that lacks the prefix (for
# the reference: if baseline/shewchuk_ref_rename.h misses a name), or, with
# EXPECTED_COUNT, if it does not define exactly that many external symbols.
execute_process(COMMAND "${NM}" -g "${LIBRARY}"
  OUTPUT_VARIABLE output RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "${NM} failed on ${LIBRARY}")
endif()
string(REGEX MATCHALL "[0-9a-fA-F]* [A-TV-Z] [^\n]+" defined "${output}")
set(count 0)
foreach(entry IN LISTS defined)
  string(REGEX REPLACE "^.* " "" symbol "${entry}")
  string(REGEX REPLACE "^_" "" symbol "${symbol}")  # Mach-O leading underscore
  if(NOT symbol MATCHES "^${PREFIX}")
    message(SEND_ERROR "unprefixed external symbol: ${symbol}")
  endif()
  math(EXPR count "${count} + 1")
endforeach()
if(DEFINED EXPECTED_COUNT)
  if(NOT count EQUAL EXPECTED_COUNT)
    message(FATAL_ERROR "${count} external symbols found, expected exactly ${EXPECTED_COUNT}:\n${output}")
  endif()
elseif(count LESS 40)
  message(FATAL_ERROR "only ${count} defined symbols found; nm output not understood")
endif()
message(STATUS "${count} external symbols, all prefixed with ${PREFIX}")
