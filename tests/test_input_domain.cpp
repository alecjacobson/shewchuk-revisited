// The input domain of the library: where it is exact, and what it currently
// returns outside (docs/exactness.md, "Input domain").
//
// Nothing here is a promise about inputs outside the documented domain. The
// "beyond" checks record today's behaviour, on this sample, so that a change
// to it is noticed and made on purpose. The limits below are observations on
// this sample, not theorems; they move only if the generators or the
// arithmetic change.
//
// Method. Scaling every coordinate of a case by one power of two is exact (as
// long as no coordinate overflows or loses a bit) and cannot change the sign
// of the determinant. Each case of a slice of the corpus is rescaled so that
//   * its largest coordinate has binary exponent T (upper sweep), or
//   * the lowest set bit of any of its coordinates is 2^T (lower sweep),
// and the library's sign is compared with the exact oracle's.
//
//   test_input_domain          run the checks
//   test_input_domain --sweep  print the number of wrong results per T
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "check.h"
#include "generators.h"
#include "implementations.h"
#include "oracle.h"

using namespace shewchuk_test;

namespace {

// Cases drawn from each generator: every stage of every predicate is reached.
const long CASES_PER_GENERATOR = 25;

struct Case {
  Pred pred;
  double coords[MAX_COORDS];
  int max_exponent;  // ilogb of the largest magnitude
  int low_exponent;  // exponent of the lowest set bit of any coordinate
};

// Exponent of the lowest set bit of a nonzero, normal double.
int lowest_bit_exponent(double x) {
  const int exponent = std::ilogb(x);
  auto significand = static_cast<std::uint64_t>(std::scalbn(std::fabs(x), 52 - exponent));
  int trailing_zeros = 0;
  while ((significand & 1) == 0) {
    significand >>= 1;
    trailing_zeros++;
  }
  return exponent - 52 + trailing_zeros;
}

std::vector<Case> collect_cases() {
  std::vector<Case> cases;
  for_each_case(CASES_PER_GENERATOR, [&](const Generator& generator, const double* c) {
    Case one;
    one.pred = generator.pred;
    one.max_exponent = std::numeric_limits<int>::min();
    one.low_exponent = std::numeric_limits<int>::max();
    for (int i = 0; i < NUM_COORDS[one.pred]; i++) {
      one.coords[i] = c[i];
      if (c[i] == 0.0) continue;
      one.max_exponent = std::max(one.max_exponent, std::ilogb(c[i]));
      one.low_exponent = std::min(one.low_exponent, lowest_bit_exponent(c[i]));
    }
    if (one.max_exponent >= one.low_exponent) cases.push_back(one);  // not all zero
  });
  return cases;
}

// The domain the 1996 paper states: every coordinate is zero or has a binary
// exponent in this range.
const int PAPER_MIN_EXPONENT = -142, PAPER_MAX_EXPONENT = 201;

// The widest range in which every case of the sample is exact: the binary
// exponent of the largest coordinate, and the exponent of the lowest set bit
// of any coordinate. One step further the first failures appear. For a
// predicate of degree d these are about 1024 / d and exactly -floor(1074 / d).
struct Limits {
  int max_exponent;
  int low_exponent;
};
const Limits LIMITS[NUM_PREDS] = {
    {510, -537},  // orient2d, degree 2
    {339, -358},  // orient3d, degree 3
    {254, -268},  // incircle, degree 4
    {203, -214},  // insphere, degree 5
};

struct Outcome {
  long cases = 0;
  long wrong_sign = 0;  // finite result with the wrong sign
  long not_finite = 0;  // NaN or infinity
  long wrong() const { return wrong_sign + not_finite; }
};

// Rescales every case of `pred` by 2^shift(case) and compares with the oracle.
// Cases whose rescaled coordinates would not be exact doubles are left out.
template <class Shift>
Outcome run_scaled(const Implementation& library, const std::vector<Case>& cases, Pred pred,
                   Shift&& shift) {
  Outcome outcome;
  for (const Case& one : cases) {
    if (one.pred != pred) continue;
    const int k = shift(one);
    if (one.max_exponent + k > 1023 || one.low_exponent + k < -1074) continue;
    double scaled[MAX_COORDS];
    for (int i = 0; i < NUM_COORDS[pred]; i++) scaled[i] = std::scalbn(one.coords[i], k);
    const double value = library.call(pred, scaled);
    outcome.cases++;
    if (!std::isfinite(value)) {
      outcome.not_finite++;
    } else if (sign_of(value) != oracle_sign(pred, scaled)) {
      outcome.wrong_sign++;
    }
  }
  return outcome;
}

Outcome run_upper(const Implementation& library, const std::vector<Case>& cases, Pred pred,
                  int max_exponent) {
  return run_scaled(library, cases, pred,
                    [=](const Case& one) { return max_exponent - one.max_exponent; });
}

Outcome run_lower(const Implementation& library, const std::vector<Case>& cases, Pred pred,
                  int low_exponent) {
  return run_scaled(library, cases, pred,
                    [=](const Case& one) { return low_exponent - one.low_exponent; });
}

void report(Pred pred, const char* what, int t, const Outcome& o) {
  std::printf("%-8s %-28s %5d: %4ld cases, %4ld wrong sign, %4ld not finite\n", PRED_NAME[pred],
              what, t, o.cases, o.wrong_sign, o.not_finite);
}

void print_sweep(const Implementation& library, const std::vector<Case>& cases) {
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    std::printf("%s, largest coordinate exponent T: cases, wrong sign, not finite\n",
                PRED_NAME[p]);
    for (int t = 150; t <= 1023; t++) {
      const Outcome o = run_upper(library, cases, pred, t);
      if (o.wrong() || t % 50 == 0) {
        std::printf("  T=%5d %6ld %6ld %6ld\n", t, o.cases, o.wrong_sign, o.not_finite);
      }
    }
    std::printf("%s, lowest set bit exponent T: cases, wrong sign, not finite\n", PRED_NAME[p]);
    for (int t = -150; t >= -1074; t--) {
      const Outcome o = run_lower(library, cases, pred, t);
      if (o.wrong() || t % 50 == 0) {
        std::printf("  T=%5d %6ld %6ld %6ld\n", t, o.cases, o.wrong_sign, o.not_finite);
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  initialize_implementations();
  const Implementation& library = *find_implementation("library");
  const std::vector<Case> cases = collect_cases();

  if (argc == 2 && std::strcmp(argv[1], "--sweep") == 0) {
    print_sweep(library, cases);
    return 0;
  }
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    const Limits& limits = LIMITS[p];

    // Inside: the edge of the documented domain, and up to the observed limits.
    for (int t : {PAPER_MAX_EXPONENT, limits.max_exponent - 10, limits.max_exponent - 1,
                  limits.max_exponent}) {
      const Outcome o = run_upper(library, cases, pred, t);
      report(pred, "largest exponent", t, o);
      CHECK(o.cases > 200);
      CHECK(o.wrong() == 0);
    }
    for (int t : {PAPER_MIN_EXPONENT - 52, limits.low_exponent + 10, limits.low_exponent + 1,
                  limits.low_exponent}) {
      const Outcome o = run_lower(library, cases, pred, t);
      report(pred, "lowest set bit", t, o);
      CHECK(o.cases > 200);
      CHECK(o.wrong() == 0);
    }

    // Beyond the upper limit an intermediate product overflows. The result is
    // then NaN or an infinity; every result that is still finite has the
    // exact sign.
    for (int t : {limits.max_exponent + 2, limits.max_exponent + 20, 1000}) {
      const Outcome o = run_upper(library, cases, pred, t);
      report(pred, "largest exponent (beyond)", t, o);
      CHECK(o.not_finite > 0);
      CHECK(o.wrong_sign == 0);
    }

    // Beyond the lower limit a roundoff term underflows. The result is finite
    // and its sign is wrong for some inputs: nothing signals the failure.
    for (int t : {limits.low_exponent - 2, limits.low_exponent - 20, -1000}) {
      const Outcome o = run_lower(library, cases, pred, t);
      report(pred, "lowest set bit (beyond)", t, o);
      CHECK(o.wrong_sign > 0);
      CHECK(o.not_finite == 0);
    }

    // Non-finite input: a NaN coordinate gives NaN; an infinite coordinate
    // gives NaN or an infinity. Never a finite value, so never a zero.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    long finite_for_nan = 0, finite_for_infinity = 0, calls = 0;
    for (const Case& one : cases) {
      if (one.pred != pred) continue;
      for (int i = 0; i < NUM_COORDS[pred]; i++) {
        double c[MAX_COORDS];
        std::memcpy(c, one.coords, sizeof c);
        c[i] = nan;
        if (!std::isnan(library.call(pred, c))) finite_for_nan++;
        for (double special : {infinity, -infinity}) {
          c[i] = special;
          if (std::isfinite(library.call(pred, c))) finite_for_infinity++;
        }
        calls++;
      }
    }
    std::printf("%-8s one non-finite coordinate: %ld positions, %ld finite results for NaN, "
                "%ld for an infinity\n",
                PRED_NAME[p], calls, finite_for_nan, finite_for_infinity);
    CHECK(calls > 2000);
    CHECK(finite_for_nan == 0);
    CHECK(finite_for_infinity == 0);
  }
  return finish("input domain");
}
