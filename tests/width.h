// The width of a test case, computed independently of the library.
//
// Every finite double is an integer times a power of two. Scale all
// coordinates of a case by the one power of two that makes them integers
// with no common factor of two; the width W is the bit length of the largest
// magnitude, i.e. the smallest W with every scaled |coordinate| < 2^W.
// Equivalently: one past the highest set bit of any coordinate, minus the
// lowest set bit of any coordinate, as binary exponents. W is 0 if every
// coordinate is zero.
//
// The library's exact integer stage (src/integer_stage.h) decides exactly the
// calls that reach it whose width is at most INTEGER_STAGE_MAX_WIDTH. This
// header shares no code with it: it uses frexp and ldexp where the library
// reads the bits of the double.
#pragma once

#include <climits>
#include <cmath>
#include <cstdint>

#include "predicate_ids.h"
#include "stages.h"

namespace shewchuk_test {

// Where the library tries its integer stage: whether it comes before the
// stage `stage` of the 1996 code. For orient3d, incircle and insphere it sits
// directly behind the first filter, before B, Bx, C and D. For orient2d,
// whose stages B, Bx and C are cheaper than the integer stage, only before D.
// So a call the reference decides in `stage` is decided by the integer stage
// in the library exactly if this is true and its width is small enough.
inline bool integer_stage_precedes(Pred pred, int stage) {
  if (pred == ORIENT2D) return stage == STAGE_D;
  return stage == STAGE_B || stage == STAGE_BX || stage == STAGE_C || stage == STAGE_D;
}

// The widest width the library's integer stage accepts (checked against the
// library's own constant in test_integer_stage.cpp).
inline constexpr int INTEGER_STAGE_MAX_WIDTH = 189;

// The width of `count` coordinates, or -1 if one of them is not finite.
inline int width_of(const double* coords, int count) {
  int lowest = INT_MAX, highest = INT_MIN;
  for (int i = 0; i < count; i++) {
    if (!std::isfinite(coords[i])) return -1;
    if (coords[i] == 0.0) continue;
    // |x| = fraction * 2^e with fraction in [0.5, 1): the highest set bit of
    // x has exponent e - 1. fraction * 2^53 is an integer (exact calls).
    int e;
    const double fraction = std::frexp(std::fabs(coords[i]), &e);
    std::uint64_t mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
    int low = e - 53;
    while ((mantissa & 1) == 0) {
      mantissa >>= 1;
      low++;
    }
    if (low < lowest) lowest = low;
    if (e > highest) highest = e;
  }
  return highest == INT_MIN ? 0 : highest - lowest;
}

}  // namespace shewchuk_test
