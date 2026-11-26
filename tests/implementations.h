// The table of predicate implementations the test drivers can run.
//
// A new implementation plugs into every driver by adding one entry to the
// table in implementations.cpp.
#pragma once

#include <vector>

#include "predicate_ids.h"

namespace shewchuk_test {

struct Implementation {
  const char* name;
  const char* description;
  double (*orient2d)(const double* pa, const double* pb, const double* pc);
  double (*orient3d)(const double* pa, const double* pb, const double* pc, const double* pd);
  double (*incircle)(const double* pa, const double* pb, const double* pc, const double* pd);
  double (*insphere)(const double* pa, const double* pb, const double* pc, const double* pd,
                     const double* pe);
  // Null if the implementation can run here; otherwise the reason it cannot
  // (not built for this compiler, or the CPU lacks a required feature).
  const char* (*unavailable_reason)();
  // True for third-party code that is measured as a comparator
  // (docs/comparators.md). The drivers report a comparator's wrong signs
  // without treating them as a failure of this project.
  bool comparator = false;
  // Per predicate: true if the implementation, by design, never reports a
  // degenerate input: it breaks the tie with a symbolic perturbation and
  // returns a nonzero sign where the exact sign is zero.
  bool perturbs_zero[NUM_PREDS] = {false, false, false, false};
  // Per predicate (incircle, insphere): true if the implementation answers
  // "is the last point inside the circle or sphere through the others",
  // whatever the orientation of the others. That is the 1996 sign multiplied
  // by the sign of orient2d or orient3d of the other points, and undefined
  // when those are collinear or coplanar.
  bool unoriented[NUM_PREDS] = {false, false, false, false};
  // Per predicate (incircle, insphere): nonzero if the implementation has a
  // precondition on the other points: their orient2d or orient3d, in the 1996
  // convention, must have this sign. Its answers on other inputs mean nothing
  // and are not checked.
  int required_base_orientation[NUM_PREDS] = {0, 0, 0, 0};
  // True if the benchmark's sign checksum can be compared with the
  // reference's for this predicate.
  bool same_signs_as_reference(Pred pred) const {
    return !perturbs_zero[pred] && !unoriented[pred] && required_base_orientation[pred] == 0;
  }

  // Calls the predicate `pred` on a flat coordinate array.
  double call(Pred pred, const double* c) const {
    switch (pred) {
      case ORIENT2D: return orient2d(c, c + 2, c + 4);
      case ORIENT3D: return orient3d(c, c + 3, c + 6, c + 9);
      case INCIRCLE: return incircle(c, c + 2, c + 4, c + 6);
      default: return insphere(c, c + 3, c + 6, c + 9, c + 12);
    }
  }
};

// Runs the one-time initialization of every available implementation
// (exactinit() for the 1996 code). Safe to call repeatedly.
void initialize_implementations();

// All implementations. "reference" is always first.
const std::vector<Implementation>& all_implementations();

// The implementation called `name`, or null.
const Implementation* find_implementation(const char* name);

// The pristine reference built with locked floating-point flags.
const Implementation& reference_implementation();

// Calls the stage-instrumented reference and reports which stage (stages.h)
// decided the call. Returns the predicate value.
double staged_reference_call(Pred pred, const double* c, int* stage);

// The same for the library: calls its test-only build (src/test_hooks.h).
double staged_library_call(Pred pred, const double* c, int* stage);

}  // namespace shewchuk_test
