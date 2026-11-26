// CGAL's Exact_predicates_inexact_constructions_kernel (EPICK): static
// filters, then interval arithmetic, then exact rational arithmetic.
//
// CGAL is header-only and is compiled into this translation unit. Each call
// builds the kernel's point objects from the coordinates, which is how a
// program that keeps its points as plain doubles has to call CGAL; a program
// that stores CGAL points does not pay for that (it is a copy of two or three
// doubles per point).
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

#include "comparators.h"

namespace {

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;

Kernel::Point_2 point2(const double* p) { return Kernel::Point_2(p[0], p[1]); }
Kernel::Point_3 point3(const double* p) { return Kernel::Point_3(p[0], p[1], p[2]); }

}  // namespace

extern "C" {

void shewchuk_comparator_cgal_initialize(void) {}

double shewchuk_comparator_cgal_orient2d(const double* pa, const double* pb, const double* pc) {
  return static_cast<double>(CGAL::orientation(point2(pa), point2(pb), point2(pc)));
}

// CGAL's 3D orientation and oriented-sphere test have the opposite sign
// convention.
double shewchuk_comparator_cgal_orient3d(const double* pa, const double* pb, const double* pc,
                                         const double* pd) {
  return -static_cast<double>(CGAL::orientation(point3(pa), point3(pb), point3(pc), point3(pd)));
}

double shewchuk_comparator_cgal_incircle(const double* pa, const double* pb, const double* pc,
                                         const double* pd) {
  return static_cast<double>(
      CGAL::side_of_oriented_circle(point2(pa), point2(pb), point2(pc), point2(pd)));
}

double shewchuk_comparator_cgal_insphere(const double* pa, const double* pb, const double* pc,
                                         const double* pd, const double* pe) {
  return -static_cast<double>(CGAL::side_of_oriented_sphere(point3(pa), point3(pb), point3(pc),
                                                            point3(pd), point3(pe)));
}

}  // extern "C"
