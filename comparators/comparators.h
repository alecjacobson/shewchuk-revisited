// Third-party exact predicates behind the harness's calling convention.
//
// Each comparator is one translation unit (comparators/<name>.cpp) compiled
// with the floating-point flags its upstream prescribes. It exports the four
// functions declared here: the argument order and the sign convention of the
// 1996 predicates.c, and the sign of the comparator's answer as -1.0, 0.0 or
// +1.0. The wrappers do nothing else, so a call through the harness's function
// pointer costs what a call of the comparator's own entry point costs.
//
// Whether the argument order and the signs were adapted correctly is not
// argued here: the differential test checks every wrapper against the exact
// oracle (tests/CMakeLists.txt, comparator.*).
#pragma once

// clang-format off
#define SHEWCHUK_COMPARATOR_DECLARE(name)                                              \
  double shewchuk_comparator_##name##_orient2d(const double* pa, const double* pb,     \
                                               const double* pc);                      \
  double shewchuk_comparator_##name##_orient3d(const double* pa, const double* pb,     \
                                               const double* pc, const double* pd);    \
  double shewchuk_comparator_##name##_incircle(const double* pa, const double* pb,     \
                                               const double* pc, const double* pd);    \
  double shewchuk_comparator_##name##_insphere(const double* pa, const double* pb,     \
                                               const double* pc, const double* pd,     \
                                               const double* pe);                      \
  void shewchuk_comparator_##name##_initialize(void);
// clang-format on

#ifdef __cplusplus
extern "C" {
#endif
SHEWCHUK_COMPARATOR_DECLARE(geogram)
SHEWCHUK_COMPARATOR_DECLARE(cgal)
SHEWCHUK_COMPARATOR_DECLARE(indirect)
SHEWCHUK_COMPARATOR_DECLARE(geometric_predicates)
#ifdef __cplusplus
}
#endif
