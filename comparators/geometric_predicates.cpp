// GeometricPredicates: a header-only C++ template port of the 1996 adaptive
// predicates. Compiled into this translation unit.
#include <cmath>  // predicates.h uses std::abs and std::fma

#include "predicates.h"

#include "comparators.h"

namespace {
double sign(double x) { return static_cast<double>((x > 0.0) - (x < 0.0)); }
}  // namespace

extern "C" {

void shewchuk_comparator_geometric_predicates_initialize(void) {}

double shewchuk_comparator_geometric_predicates_orient2d(const double* pa, const double* pb,
                                                         const double* pc) {
  return sign(predicates::adaptive::orient2d<double>(pa, pb, pc));
}

double shewchuk_comparator_geometric_predicates_orient3d(const double* pa, const double* pb,
                                                         const double* pc, const double* pd) {
  return sign(predicates::adaptive::orient3d<double>(pa, pb, pc, pd));
}

double shewchuk_comparator_geometric_predicates_incircle(const double* pa, const double* pb,
                                                         const double* pc, const double* pd) {
  return sign(predicates::adaptive::incircle<double>(pa, pb, pc, pd));
}

double shewchuk_comparator_geometric_predicates_insphere(const double* pa, const double* pb,
                                                         const double* pc, const double* pd,
                                                         const double* pe) {
  return sign(predicates::adaptive::insphere<double>(pa, pb, pc, pd, pe));
}

}  // extern "C"
