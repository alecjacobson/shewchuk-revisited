/* Declarations for the pristine 1996 predicates (reference/predicates.c),
 * built unmodified with locked floating-point flags and exported under the
 * prefix shewchuk_ref_. Usable from C and C++.
 *
 * shewchuk_ref_exactinit() must be called once before any predicate: it
 * computes the error-bound constants at run time. Without it every bound is
 * zero and the predicates silently lose robustness.
 *
 * Sign conventions (return value > 0, < 0, == 0):
 *   orient2d(a, b, c)        > 0 if a, b, c are in counterclockwise order
 *   orient3d(a, b, c, d)     > 0 if d is below the plane through a, b, c
 *                              (a, b, c appear clockwise seen from d)
 *   incircle(a, b, c, d)     > 0 if d is inside the circle through a, b, c,
 *                              for a, b, c counterclockwise
 *   insphere(a, b, c, d, e)  > 0 if e is inside the sphere through a..d,
 *                              for orient3d(a, b, c, d) > 0
 * The pointer parameters are not const because the 1996 definitions are not;
 * the functions do not write through them. */
#ifndef SHEWCHUK_REF_H
#define SHEWCHUK_REF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Declares the public functions of one renamed build of predicates.c. */
#define SHEWCHUK_REF_DECLARE(P)                                               \
  void P##exactinit(void);                                                    \
  double P##orient2d(double *pa, double *pb, double *pc);                     \
  double P##orient3d(double *pa, double *pb, double *pc, double *pd);         \
  double P##incircle(double *pa, double *pb, double *pc, double *pd);         \
  double P##insphere(double *pa, double *pb, double *pc, double *pd,          \
                     double *pe);                                             \
  double P##orient2dfast(double *pa, double *pb, double *pc);                 \
  double P##orient3dfast(double *pa, double *pb, double *pc, double *pd);     \
  double P##incirclefast(double *pa, double *pb, double *pc, double *pd);     \
  double P##inspherefast(double *pa, double *pb, double *pc, double *pd,      \
                         double *pe);                                         \
  double P##orient2dexact(double *pa, double *pb, double *pc);                \
  double P##orient3dexact(double *pa, double *pb, double *pc, double *pd);    \
  double P##incircleexact(double *pa, double *pb, double *pc, double *pd);    \
  double P##insphereexact(double *pa, double *pb, double *pc, double *pd,     \
                          double *pe);

SHEWCHUK_REF_DECLARE(shewchuk_ref_)

#ifdef __cplusplus
}
#endif

#endif
