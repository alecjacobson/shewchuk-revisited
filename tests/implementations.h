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

}  // namespace shewchuk_test
