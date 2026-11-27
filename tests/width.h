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
// calls that reach it whose width is at most INTEGER_STAGE_MAX_WIDTH, and
// every call that reaches it from outside the floating-point domain (below).
// This header shares no code with it: it uses frexp and ldexp where the
// library reads the bits of the double.
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

// The widest width the library's integer stage accepts in its fast tiers
// (checked against the library's own constant in test_integer_stage.cpp).
inline constexpr int INTEGER_STAGE_MAX_WIDTH = 317;

// Where the set bits of `count` coordinates are, as binary exponents.
struct Extent {
  bool finite;    // no coordinate is an infinity or a NaN
  bool all_zero;  // (finite and) every coordinate is zero
  int lowest;     // exponent of the lowest set bit of any coordinate
  int highest;    // one more than the exponent of the highest set bit
};
inline Extent extent_of(const double* coords, int count) {
  Extent extent = {true, true, INT_MAX, INT_MIN};
  for (int i = 0; i < count; i++) {
    if (!std::isfinite(coords[i])) return {false, false, 0, 0};
    if (coords[i] == 0.0) continue;
    // |x| = fraction * 2^e with fraction in [0.5, 1): the highest set bit of
    // x has exponent e - 1. fraction * 2^53 is an integer (exact calls, for
    // subnormal numbers too).
    int e;
    const double fraction = std::frexp(std::fabs(coords[i]), &e);
    std::uint64_t mantissa = static_cast<std::uint64_t>(std::ldexp(fraction, 53));
    int low = e - 53;
    while ((mantissa & 1) == 0) {
      mantissa >>= 1;
      low++;
    }
    if (low < extent.lowest) extent.lowest = low;
    if (e > extent.highest) extent.highest = e;
    extent.all_zero = false;
  }
  return extent;
}

// The width of `count` coordinates, or -1 if one of them is not finite.
inline int width_of(const double* coords, int count) {
  const Extent extent = extent_of(coords, count);
  if (!extent.finite) return -1;
  return extent.all_zero ? 0 : extent.highest - extent.lowest;
}

// The floating-point domain (docs/proofs/input-domain.md, section 5), stated
// again here independently of the library: the largest coordinate has a
// binary exponent of at most 201, and the degree of the determinant times
// the exponent of the lowest set bit of any coordinate is at least -914.
// Inside it the library may use the adaptive floating-point stages of 1996;
// outside it, its integer stage decides whatever the first filter and its
// guard do not.
inline constexpr int DEGREE[NUM_PREDS] = {2, 3, 4, 5};
inline constexpr int FLOATING_POINT_MAX_EXPONENT = 201;
inline constexpr int FLOATING_POINT_MIN_GRID = -914;
inline bool in_floating_point_domain(Pred pred, const double* coords) {
  const Extent extent = extent_of(coords, NUM_COORDS[pred]);
  if (!extent.finite) return false;
  if (extent.all_zero) return true;
  return extent.highest - 1 <= FLOATING_POINT_MAX_EXPONENT &&
         DEGREE[pred] * extent.lowest >= FLOATING_POINT_MIN_GRID;
}

// The integer stage's wide tier, which holds every finite input (checked
// against the library's own constant in test_integer_stage.cpp).
inline constexpr int INTEGER_STAGE_WIDE_TIER = 33;

// The tier of the integer stage that must decide a case once the stage is
// asked to: 1 to 5 by width; for a wider case the wide tier if the case is
// outside the floating-point domain, and 0 (the stage leaves the case to the
// floating-point stages) if it is inside. 0 also for a case that is not
// finite.
inline int expected_integer_tier(Pred pred, const double* coords) {
  const int width = width_of(coords, NUM_COORDS[pred]);
  if (width < 0) return 0;
  if (width <= 61) return 1;
  if (width <= 125) return 2;
  if (width <= 189) return 3;
  if (width <= 253) return 4;
  if (width <= INTEGER_STAGE_MAX_WIDTH) return 5;
  return in_floating_point_domain(pred, coords) ? 0 : INTEGER_STAGE_WIDE_TIER;
}

}  // namespace shewchuk_test
