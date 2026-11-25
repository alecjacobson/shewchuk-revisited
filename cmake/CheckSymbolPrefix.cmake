# cmake -DNM=<nm> -DLIBRARY=<static library> -DPREFIX=<prefix> -P CheckSymbolPrefix.cmake
# Fails if the library defines an external symbol that lacks the prefix, i.e.
# if baseline/shewchuk_ref_rename.h misses a name.
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
if(count LESS 40)
  message(FATAL_ERROR "only ${count} defined symbols found; nm output not understood")
endif()
message(STATUS "${count} external symbols, all prefixed with ${PREFIX}")
