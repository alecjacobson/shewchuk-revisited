/* Declarations of the test-only build of predicates.cpp.
 *
 * When predicates.cpp is compiled with SHEWCHUK_PREDICATES_TEST_HOOKS (only
 * the test suite does that, see tests/CMakeLists.txt) it
 *   - exports the four predicates as shewchuk_hooked_<name> instead of
 *     shewchuk_<name>, so that it can be linked next to the shipped library;
 *   - records in shewchuk_hooked_last_stage which stage decided each call;
 *   - exports the internal expansion routines for component-level tests.
 * The shipped library contains none of this.
 *
 * Private header: not installed, not part of the public interface. */
#ifndef SHEWCHUK_SRC_TEST_HOOKS_H
#define SHEWCHUK_SRC_TEST_HOOKS_H

/* Stage numbers. tests/stages.h uses the same values (checked there). */
enum {
  /* orient2d only: the two products have opposite signs or one is zero. The
   * 1996 code returns on that before its first filter. The library has one
   * test for A0 and A; it reports A0 when that test decides a call of this
   * class and A when it decides any other call. */
  SHEWCHUK_STAGE_A0 = 0,
  SHEWCHUK_STAGE_A = 1,  /* first floating-point filter (errboundA) */
  SHEWCHUK_STAGE_B = 2,  /* second filter (errboundB) */
  SHEWCHUK_STAGE_BX = 3, /* all coordinate-difference tails are zero */
  SHEWCHUK_STAGE_C = 4,  /* third filter (errboundC) */
  SHEWCHUK_STAGE_D = 5,  /* fully exact (expansions) */
  SHEWCHUK_STAGE_I = 6   /* exact integer stage (not in the 1996 code) */
};

#ifdef __cplusplus
extern "C" {
#endif

/* Stage that decided the most recent predicate call. Not thread-safe. */
extern int shewchuk_hooked_last_stage;

double shewchuk_hooked_orient2d(const double *pa, const double *pb, const double *pc);
double shewchuk_hooked_orient3d(const double *pa, const double *pb, const double *pc,
                                const double *pd);
double shewchuk_hooked_incircle(const double *pa, const double *pb, const double *pc,
                                const double *pd);
double shewchuk_hooked_insphere(const double *pa, const double *pb, const double *pc,
                                const double *pd, const double *pe);

/* The expansion routines the predicates are built from. */
int shewchuk_hooked_fast_expansion_sum_zeroelim(int elen, const double *e, int flen,
                                                const double *f, double *h);
int shewchuk_hooked_scale_expansion_zeroelim(int elen, const double *e, double b, double *h);
double shewchuk_hooked_estimate(int elen, const double *e);

#ifdef __cplusplus
}
#endif

#endif
