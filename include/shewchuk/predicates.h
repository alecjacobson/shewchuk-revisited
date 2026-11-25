/* Adaptive exact geometric predicates (after Shewchuk, 1996). C and C++.
 *
 * Each function returns a double whose SIGN is exact: it is the sign of the
 * determinant below evaluated without any roundoff. The magnitude is only an
 * approximation of the determinant and must not be relied on.
 *
 *   orient2d(a, b, c)        > 0 if a, b, c are in counterclockwise order,
 *                            < 0 if clockwise, 0 if collinear
 *   orient3d(a, b, c, d)     > 0 if d is below the plane through a, b, c
 *                              (a, b, c appear clockwise seen from d),
 *                            0 if coplanar
 *   incircle(a, b, c, d)     > 0 if d is inside the circle through a, b, c,
 *                              for a, b, c counterclockwise; 0 if cocircular
 *   insphere(a, b, c, d, e)  > 0 if e is inside the sphere through a..d,
 *                              for orient3d(a, b, c, d) > 0; 0 if cospherical
 *
 * Points are arrays of 2 (orient2d, incircle) or 3 (orient3d, insphere)
 * doubles. The functions never write through their arguments, keep no state,
 * need no initialization call and may be called from any number of threads.
 *
 * Domain: the result is exact as long as no intermediate operation overflows
 * or underflows. The 1996 paper guarantees that for coordinates that are zero
 * or have magnitudes in [2^-142, 2^201]. NaN or infinite coordinates give an
 * unspecified result.
 *
 * The functions are defined in the compiled library, which the project's
 * build compiles with the floating-point flags exactness depends on. The code
 * that includes this header may be compiled with any flags. */
#ifndef SHEWCHUK_PREDICATES_H
#define SHEWCHUK_PREDICATES_H

#ifdef __cplusplus
extern "C" {
#endif

double shewchuk_orient2d(const double *pa, const double *pb, const double *pc);
double shewchuk_orient3d(const double *pa, const double *pb, const double *pc,
                         const double *pd);
double shewchuk_incircle(const double *pa, const double *pb, const double *pc,
                         const double *pd);
double shewchuk_insphere(const double *pa, const double *pb, const double *pc,
                         const double *pd, const double *pe);

#ifdef __cplusplus
} /* extern "C" */

/* The same four functions as shewchuk::orient2d(...) etc. */
namespace shewchuk {

inline double orient2d(const double *pa, const double *pb, const double *pc) {
  return shewchuk_orient2d(pa, pb, pc);
}
inline double orient3d(const double *pa, const double *pb, const double *pc, const double *pd) {
  return shewchuk_orient3d(pa, pb, pc, pd);
}
inline double incircle(const double *pa, const double *pb, const double *pc, const double *pd) {
  return shewchuk_incircle(pa, pb, pc, pd);
}
inline double insphere(const double *pa, const double *pb, const double *pc, const double *pd,
                       const double *pe) {
  return shewchuk_insphere(pa, pb, pc, pd, pe);
}

} /* namespace shewchuk */
#endif

#endif
