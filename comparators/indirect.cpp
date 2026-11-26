// Indirect_Predicates (with its numeric kernel NFG): the direct predicates on
// double coordinates. A semi-static filter, then expansions or, for the
// widest inputs, a big-float number type.
//
// Both libraries are header-only and are compiled into this translation unit.
#include "implicit_point.h"

#include "comparators.h"

extern "C" {

void shewchuk_comparator_indirect_initialize(void) {}

double shewchuk_comparator_indirect_orient2d(const double* pa, const double* pb,
                                             const double* pc) {
  return static_cast<double>(::orient2d(pa[0], pa[1], pb[0], pb[1], pc[0], pc[1]));
}

// orient3d and inSphere have the opposite sign convention.
double shewchuk_comparator_indirect_orient3d(const double* pa, const double* pb, const double* pc,
                                             const double* pd) {
  return -static_cast<double>(::orient3d(pa[0], pa[1], pa[2], pb[0], pb[1], pb[2], pc[0], pc[1],
                                        pc[2], pd[0], pd[1], pd[2]));
}

double shewchuk_comparator_indirect_incircle(const double* pa, const double* pb, const double* pc,
                                             const double* pd) {
  return static_cast<double>(
      IPs::incircle(pa[0], pa[1], pb[0], pb[1], pc[0], pc[1], pd[0], pd[1]));
}

double shewchuk_comparator_indirect_insphere(const double* pa, const double* pb, const double* pc,
                                             const double* pd, const double* pe) {
  return -static_cast<double>(IPs::inSphere(pa[0], pa[1], pa[2], pb[0], pb[1], pb[2], pc[0], pc[1],
                                        pc[2], pd[0], pd[1], pd[2], pe[0], pe[1], pe[2]));
}

}  // extern "C"
