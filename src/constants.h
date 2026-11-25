// The constants of the 1996 predicates, as compile-time values.
//
// predicates.c computes these at run time in exactinit() and keeps them in
// mutable globals; forgetting to call exactinit() leaves every error bound at
// zero. Here they are constants, so there is nothing to initialize and
// nothing shared between threads.
//
// Each constant is written as the hexadecimal literal of the value that
// exactinit() computes, and checked at compile time against the expression
// exactinit() uses (predicates.c lines 643-682). tests/test_constants.cpp
// also compares them with the globals of the pristine build at run time.
//
// Private header: not installed, not part of the public interface.
#pragma once

#include <limits>

namespace shewchuk::detail {

static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<double>::digits == 53,
              "the predicates are only validated for IEEE 754 binary64 doubles");

// 2^-53: half the distance from 1.0 to the next double. Bounds the relative
// error of one correctly rounded operation.
inline constexpr double epsilon = 0x1p-53;

// 2^27 + 1: splits a 53-bit significand into two halves (Dekker / Veltkamp).
inline constexpr double splitter = 134217729.0;

inline constexpr double resulterrbound = 0x1.8000000000002p-52;
inline constexpr double ccwerrboundA = 0x1.8000000000004p-52;
inline constexpr double ccwerrboundB = 0x1.0000000000003p-52;
inline constexpr double ccwerrboundC = 0x1.2000000000004p-103;
inline constexpr double o3derrboundA = 0x1.c000000000007p-51;
inline constexpr double o3derrboundB = 0x1.8000000000007p-52;
inline constexpr double o3derrboundC = 0x1.a000000000009p-102;
inline constexpr double iccerrboundA = 0x1.4000000000006p-50;
inline constexpr double iccerrboundB = 0x1.0000000000006p-51;
inline constexpr double iccerrboundC = 0x1.6000000000009p-101;
inline constexpr double isperrboundA = 0x1.0000000000007p-49;
inline constexpr double isperrboundB = 0x1.4000000000009p-51;
inline constexpr double isperrboundC = 0x1.1c0000000000bp-100;

// The loop of exactinit() (predicates.c lines 649-666), evaluated by the
// compiler: halve epsilon until 1 + epsilon rounds to 1, doubling the
// splitter every other step.
struct ExactinitResult {
  double epsilon;
  double splitter;
};
constexpr ExactinitResult run_exactinit_loop() {
  double half = 0.5, check = 1.0, lastcheck = 0.0;
  double eps = 1.0, split = 1.0;
  bool every_other = true;
  do {
    lastcheck = check;
    eps *= half;
    if (every_other) split *= 2.0;
    every_other = !every_other;
    check = 1.0 + eps;
  } while ((check != 1.0) && (check != lastcheck));
  split += 1.0;
  return {eps, split};
}

static_assert(run_exactinit_loop().epsilon == epsilon);
static_assert(run_exactinit_loop().splitter == splitter);
static_assert(splitter == 134217729.0);
static_assert(splitter == 0x1.0000002p+27);
static_assert(epsilon == std::numeric_limits<double>::epsilon() / 2);

// The closed formulas of exactinit() (predicates.c lines 669-681).
static_assert(resulterrbound == (3.0 + 8.0 * epsilon) * epsilon);
static_assert(ccwerrboundA == (3.0 + 16.0 * epsilon) * epsilon);
static_assert(ccwerrboundB == (2.0 + 12.0 * epsilon) * epsilon);
static_assert(ccwerrboundC == (9.0 + 64.0 * epsilon) * epsilon * epsilon);
static_assert(o3derrboundA == (7.0 + 56.0 * epsilon) * epsilon);
static_assert(o3derrboundB == (3.0 + 28.0 * epsilon) * epsilon);
static_assert(o3derrboundC == (26.0 + 288.0 * epsilon) * epsilon * epsilon);
static_assert(iccerrboundA == (10.0 + 96.0 * epsilon) * epsilon);
static_assert(iccerrboundB == (4.0 + 48.0 * epsilon) * epsilon);
static_assert(iccerrboundC == (44.0 + 576.0 * epsilon) * epsilon * epsilon);
static_assert(isperrboundA == (16.0 + 224.0 * epsilon) * epsilon);
static_assert(isperrboundB == (5.0 + 72.0 * epsilon) * epsilon);
static_assert(isperrboundC == (71.0 + 1408.0 * epsilon) * epsilon * epsilon);

}  // namespace shewchuk::detail
