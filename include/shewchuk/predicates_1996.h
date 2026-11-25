/* Opt-in compatibility header: the names of the 1996 predicates.c.
 *
 * Lets code written against predicates.c (exactinit(); orient2d(pa, pb, pc);
 * ...) compile unchanged against this library. Include it INSTEAD of
 * declaring the 1996 functions yourself, and do not link the 1996
 * predicates.c (or the copies inside Triangle and TetGen) into the same
 * program under the same names.
 *
 * The functions are static inline wrappers, so this header adds no external
 * symbol called orient2d, exactinit, ...: only the including file sees the
 * names. shewchuk/predicates.h itself never defines unprefixed names.
 *
 * exactinit() does nothing: the constants it used to compute are compile-time
 * constants here. It is kept so that existing calls still compile.
 *
 * Only the four adaptive predicates are provided. The *fast, *exact and *slow
 * variants of predicates.c are not part of this library. */
#ifndef SHEWCHUK_PREDICATES_1996_H
#define SHEWCHUK_PREDICATES_1996_H

#include "predicates.h"

static inline void exactinit(void) {}

static inline double orient2d(const double *pa, const double *pb, const double *pc) {
  return shewchuk_orient2d(pa, pb, pc);
}
static inline double orient3d(const double *pa, const double *pb, const double *pc,
                              const double *pd) {
  return shewchuk_orient3d(pa, pb, pc, pd);
}
static inline double incircle(const double *pa, const double *pb, const double *pc,
                              const double *pd) {
  return shewchuk_incircle(pa, pb, pc, pd);
}
static inline double insphere(const double *pa, const double *pb, const double *pc,
                              const double *pd, const double *pe) {
  return shewchuk_insphere(pa, pb, pc, pd, pe);
}

#endif
