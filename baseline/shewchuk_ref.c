/* Compiles reference/predicates.c without editing it.
 *
 * The pristine file is included below after (1) compile-time guards against
 * floating-point modes that are known to break it and (2) a header that
 * renames all of its external symbols with SHEWCHUK_REF_PREFIX.
 *
 * The guards are an extra layer only. The safety argument is that the build
 * system compiles this translation unit with contraction disabled
 * (cmake/StrictFloatingPoint.cmake): -ffp-contract=fast defines no macro in
 * GCC or clang, so it cannot be detected here. */
#if defined(__FAST_MATH__) || defined(__ASSOCIATIVE_MATH__) || \
    defined(__RECIPROCAL_MATH__) || defined(_M_FP_FAST)
#error "predicates.c is not exact under fast-math style floating-point flags"
#endif

#include <float.h>
#if FLT_RADIX != 2 || DBL_MANT_DIG != 53
#error "predicates.c is only validated for IEEE 754 binary64 doubles"
#endif
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "predicates.c needs double arithmetic without extended precision"
#endif

/* System headers go first so that the renaming below cannot touch them. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
/* MSVC has neither <sys/time.h> (an empty stand-in is on the include path)
 * nor random(). random() is only called by the random-input helpers
 * (doublerand() and friends), never by a predicate. */
#define random rand
#else
#include <sys/time.h>
#endif

#include "shewchuk_ref_rename.h"

#ifdef SHEWCHUK_REF_STAGED
/* Test-only build: predicates_staged.c is generated from the pristine file by
 * tools/instrument_stages.cpp, which records the deciding stage before each
 * return of the four adaptive predicates. */
int shewchuk_staged_last_stage = -1;
#define SHEWCHUK_STAGE_HIT(stage) (shewchuk_staged_last_stage = (stage))
#include "predicates_staged.c"
#else
#include "predicates.c"
#endif
