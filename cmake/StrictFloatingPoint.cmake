# shewchuk_strict_fp: floating-point flags that every exactness-critical
# target links.
#
# Target compile options are placed after CMAKE_<LANG>_FLAGS and
# CMAKE_<LANG>_FLAGS_<CONFIG> on the command line, so these win over flags
# from the user or the build type.
#
# -ffp-contract=off  No fused multiply-add may replace a rounded product.
#                    GCC's GNU-mode default (fast) makes predicates.c return
#                    wrong signs; clang's default (on) changes returned values.
# -fno-fast-math     Undo -ffast-math / -Ofast. Listed first because clang
#                    resets the contraction mode when it sees this flag.
# /fp:precise        MSVC: value-safe model, no contraction.
add_library(shewchuk_strict_fp INTERFACE)
if(MSVC)
  target_compile_options(shewchuk_strict_fp INTERFACE /fp:precise)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(shewchuk_strict_fp INTERFACE -fno-fast-math -ffp-contract=off)
else()
  message(WARNING
    "Unknown compiler ${CMAKE_C_COMPILER_ID}: no floating-point flags are "
    "enforced. The exactness tests will show whether the build is usable.")
endif()
