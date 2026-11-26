// Geogram's PCK predicates (the standalone Predicates_psm distribution).
//
// The upstream source file is compiled as part of this translation unit, so
// the functions below are Geogram's entry points with a different return
// type and not one more call in front of them.
//
// Two of Geogram's predicates answer a different question from the 1996
// ones, and the wrappers do not paper over that (the harness is told instead,
// tests/implementations.cpp):
//   * in_circle_2d_SOS and in_sphere_3d_SOS break ties with a symbolic
//     perturbation and do not report a degenerate configuration. Geogram has
//     no in-circle or in-sphere test without it. orient_2d and orient_3d do
//     return zero;
//   * in_circle_2d_SOS is positive when the last point is inside the circle
//     through the others, whatever their orientation: the 1996 sign times the
//     sign of orient2d of the triangle;
//   * in_sphere_3d_SOS requires orient_3d(p0, p1, p2, p3) > 0. Its filter
//     assumes that orientation and its exact stage does not, so on other
//     tetrahedra the two disagree. The cost of a call does not depend on the
//     orientation.
#include "Predicates_psm.cpp"  // NOLINT: the fetched upstream source

#include "comparators.h"

extern "C" {

void shewchuk_comparator_geogram_initialize(void) { GEO::PCK::initialize(); }

double shewchuk_comparator_geogram_orient2d(const double* pa, const double* pb,
                                            const double* pc) {
  return static_cast<double>(GEO::PCK::orient_2d(pa, pb, pc));
}

// Geogram's orient_3d has the opposite sign convention.
double shewchuk_comparator_geogram_orient3d(const double* pa, const double* pb, const double* pc,
                                            const double* pd) {
  return -static_cast<double>(GEO::PCK::orient_3d(pa, pb, pc, pd));
}

double shewchuk_comparator_geogram_incircle(const double* pa, const double* pb, const double* pc,
                                            const double* pd) {
  return static_cast<double>(GEO::PCK::in_circle_2d_SOS(pa, pb, pc, pd));
}

// Geogram's in_sphere_3d_SOS takes a tetrahedron that is positive for its own
// orient_3d, hence the opposite sign.
double shewchuk_comparator_geogram_insphere(const double* pa, const double* pb, const double* pc,
                                            const double* pd, const double* pe) {
  return -static_cast<double>(GEO::PCK::in_sphere_3d_SOS(pa, pb, pc, pd, pe));
}

}  // extern "C"
