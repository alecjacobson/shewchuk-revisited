// The input domain of the library: every finite double.
//
//   test_input_domain          the quick tier
//   test_input_domain deep     several times as many cases
//   test_input_domain --sweep [NAME [STEP]]
//                              no checks: print, for the implementation NAME
//                              of the table (default: the library; a
//                              comparator, for example), the number of wrong
//                              results of the two sweeps below per exponent T,
//                              for every STEP-th T (some comparators take
//                              milliseconds per call out there)
//
// The library must return the exact sign for every finite input, with no
// exception for overflow or underflow (docs/exactness.md, "Input domain";
// docs/proofs/input-domain.md). The corpus of the other tests stays inside
// the exponent range the 1996 paper states, so this test goes everywhere
// else, and compares every sign with the exact oracle:
//
//   upper sweep   a slice of the corpus rescaled by a power of two so that
//                 its largest coordinate has binary exponent T, for every T
//                 up to 1023, the largest there is: intermediate products
//                 overflow;
//   lower sweep   the same with the lowest set bit of any coordinate at 2^T,
//                 for every T down to -1074, the smallest subnormal number:
//                 products underflow to subnormal numbers and to zero. The
//                 whole slice is run at the exponents just inside the lower
//                 edge of the floating-point domain, where the guard of the
//                 first filter stops holding while the floating-point stages
//                 are still valid;
//   axes          orient2d and orient3d: each axis rescaled by its own power
//                 of two (their determinants are homogeneous in each axis, so
//                 the sign is unchanged): products that underflow next to
//                 products that do not, and factors that amplify them;
//   spikes        small integers times powers of two from a short list of
//                 extreme exponents: exact cancellation between terms whose
//                 products overflow, underflow, or vanish;
//   subnormal     every coordinate a subnormal number or zero;
//   full range    every coordinate with its own exponent from the whole
//                 range of the format, with repeated coordinates;
//   the reproducer of the wrong nonzero sign that was reported for insphere.
//
// Scaling by a power of two is exact as long as no coordinate overflows or
// loses a bit, and cannot change the sign; all other inputs are simply
// handed to the oracle, which accepts any finite double.
//
// Negative control: the pristine 1996 code, and the library built without
// its guard and integer stage, are run on the same inputs and must be CAUGHT
// returning wrong results in every family. Without that, a pass of the
// library would say nothing.
//
// Routing: the library's test-only build reports which stage decided each
// call and how the call got there. On every case the test checks that the
// route is one the proof allows (a floating-point stage behind the first
// filter only inside the floating-point domain; the integer tier that the
// width selects), that the test-only build returns the shipped library's
// value, and, over the whole run, that every new route was taken often: the
// guard rejecting a certified filter, the recheck accepting one, the fast
// integer tiers outside the floating-point domain, the wide tier.
//
// No underflow or overflow inside the floating-point domain: at the edges of
// that domain, a call that a floating-point stage decides must not raise the
// underflow or overflow exception flag. That is evidence for section 5 of
// the proof, from the processor.
//
// Non-finite input: every predicate returns NaN if a coordinate is an
// infinity or a NaN.
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include "shewchuk/predicates.h"

#include "check.h"
#include "generators.h"
#include "implementations.h"
#include "judge.h"
#include "oracle.h"
#include "stages.h"
#include "test_hooks.h"
#include "width.h"

using namespace shewchuk_test;

namespace {

// Cases drawn from each generator for the slice: every stage of every
// predicate is reached.
const long CASES_PER_GENERATOR = 25;

// The domain the 1996 paper states: every coordinate is zero or has a binary
// exponent of at most this (and at least -142).
const int PAPER_MAX_EXPONENT = 201;

// How far the 1996 code is exact on the slice (docs/exactness.md, "Input
// domain"): the binary exponent of the largest coordinate, and the exponent
// of the lowest set bit of any coordinate. For a predicate of degree d these
// are about 1024 / d and exactly -floor(1074 / d). Observations on this
// slice; beyond them the 1996 code fails.
struct Limits {
  int max_exponent;
  int low_exponent;
};
const Limits REFERENCE_LIMITS[NUM_PREDS] = {
    {510, -537},  // orient2d, degree 2
    {339, -358},  // orient3d, degree 3
    {254, -268},  // incircle, degree 4
    {203, -214},  // insphere, degree 5
};

struct Case {
  Pred pred;
  double coords[MAX_COORDS];
  int max_exponent;  // binary exponent of the largest magnitude
  int low_exponent;  // exponent of the lowest set bit of any coordinate
};

std::vector<Case> collect_cases() {
  std::vector<Case> cases;
  for_each_case(CASES_PER_GENERATOR, [&](const Generator& generator, const double* c) {
    Case one;
    one.pred = generator.pred;
    std::memcpy(one.coords, c, sizeof(double) * NUM_COORDS[one.pred]);
    const Extent extent = extent_of(c, NUM_COORDS[one.pred]);
    if (extent.all_zero) return;
    one.max_exponent = extent.highest - 1;
    one.low_exponent = extent.lowest;
    cases.push_back(one);
  });
  return cases;
}

// ---------------------------------------------------------------------------
// The subjects and what is counted
// ---------------------------------------------------------------------------

struct Tally {
  long cases = 0;
  long wrong_sign = 0;  // finite result with the wrong sign
  long not_finite = 0;  // NaN or infinity for a finite input
  long wrong() const { return wrong_sign + not_finite; }
};

enum Family { UPPER, LOWER, AXES, SPIKES, SUBNORMAL, FULL_RANGE, NUM_FAMILIES };
const char* const FAMILY_NAME[NUM_FAMILIES] = {"upper sweep", "lower sweep", "axes",
                                               "spikes",      "subnormal",   "full range"};

// Columns of the route table: how a call of the library was decided.
enum Route {
  ROUTE_LEAF,          // the first test, its guard held
  ROUTE_ZERO,          // orient2d: two zero products with a zero factor each
  ROUTE_RECHECKED,     // filter certified, guard failed, inside the domain
  ROUTE_FLOATING,      // an adaptive floating-point stage (B, Bx, C, D)
  ROUTE_TIER_IN,       // a fast integer tier, input inside the domain
  ROUTE_TIER_OUT,      // a fast integer tier, input outside the domain
  ROUTE_WIDE,          // the wide integer tier
  NUM_ROUTES
};
const char* const ROUTE_NAME[NUM_ROUTES] = {"leaf", "zero", "recheck", "B-D", "tier/in",
                                            "tier/out", "wide"};

struct Totals {
  Tally library[NUM_PREDS][NUM_FAMILIES];
  Tally reference[NUM_PREDS][NUM_FAMILIES];
  Tally unguarded[NUM_PREDS][NUM_FAMILIES];
  long routes[NUM_PREDS][NUM_ROUTES] = {};
  // Calls the library's first test did not return although the 1996 code's
  // did; and how many of them the recheck then returned after all.
  long guard_rejections[NUM_PREDS] = {};
  long guard_rejections_rechecked[NUM_PREDS] = {};
  long route_violations = 0, hook_value_differences = 0;
  long sign_form_differences = 0;
  // The 1996 code inside and beyond REFERENCE_LIMITS, in the two sweeps.
  Tally reference_inside[NUM_PREDS], reference_beyond[NUM_PREDS];
  // Exception flags at the edge of the floating-point domain.
  long flag_checks[NUM_PREDS][NUM_LIBRARY_STAGES] = {};
  long flags_raised = 0;
};

struct Subjects {
  const Implementation* library;
  const Implementation* reference;
  const Implementation* unguarded;
};

// The sign-returning form of the shipped library's predicates.
int library_sign(Pred pred, const double* c) {
  switch (pred) {
    case ORIENT2D: return shewchuk_orient2d_sign(c, c + 2, c + 4);
    case ORIENT3D: return shewchuk_orient3d_sign(c, c + 3, c + 6, c + 9);
    case INCIRCLE: return shewchuk_incircle_sign(c, c + 2, c + 4, c + 6);
    default: return shewchuk_insphere_sign(c, c + 3, c + 6, c + 9, c + 12);
  }
}

void print_case(Pred pred, const double* c) {
  std::printf("    %s", PRED_NAME[pred]);
  for (int i = 0; i < NUM_COORDS[pred]; i++) std::printf(" %a", c[i]);
  std::printf("\n");
}

void count(Tally& tally, double value, int exact) {
  tally.cases++;
  if (!std::isfinite(value)) {
    tally.not_finite++;
  } else if (sign_of(value) != exact) {
    tally.wrong_sign++;
  }
}

// Runs one finite case through every subject and checks the route the
// library took. `reference_tally`, if given, also counts the 1996 code's
// result there.
void run_case(const Subjects& subjects, Totals& totals, Family family, Pred pred, const double* c,
              Tally* reference_tally = nullptr) {
  const int exact = oracle_sign(pred, c);
  const double value = subjects.library->call(pred, c);
  Tally& tally = totals.library[pred][family];
  const long wrong_before = tally.wrong();
  count(tally, value, exact);
  if (tally.wrong() != wrong_before && tally.wrong() <= 3) {
    std::printf("WRONG in %s: the library returns %a, exact sign %d\n", FAMILY_NAME[family], value,
                exact);
    print_case(pred, c);
  }
  // The sign-returning form: the exact sign, as an int.
  if (library_sign(pred, c) != exact) totals.sign_form_differences++;
  const double reference_value = subjects.reference->call(pred, c);
  count(totals.reference[pred][family], reference_value, exact);
  if (reference_tally) count(*reference_tally, reference_value, exact);
  count(totals.unguarded[pred][family], subjects.unguarded->call(pred, c), exact);

  // The route, from the test-only build.
  shewchuk_hooked_last_integer_tier = -1;
  shewchuk_hooked_last_path = -1;
  int stage = -1;
  const double hooked = staged_library_call(pred, c, &stage);
  const int path = shewchuk_hooked_last_path;
  const int tier = shewchuk_hooked_last_integer_tier;
  if (std::memcmp(&hooked, &value, sizeof value) != 0) totals.hook_value_differences++;

  const bool in_domain = in_floating_point_domain(pred, c);
  const bool floating_stage =
      stage == STAGE_B || stage == STAGE_BX || stage == STAGE_C || stage == STAGE_D;
  bool allowed = true;
  Route route = ROUTE_LEAF;
  if (stage == STAGE_I) {
    // The tier is the one the width and the domain select (tests/width.h):
    // a fast tier by width, the wide tier only outside the domain. The
    // integer stage is asked behind the first filter; for orient2d, also
    // behind the window of the first test.
    allowed = tier != 0 && tier == expected_integer_tier(pred, c) &&
              (path == SHEWCHUK_PATH_AFTER || (pred == ORIENT2D && path == SHEWCHUK_PATH_LEAF));
    route = tier == INTEGER_STAGE_WIDE_TIER ? ROUTE_WIDE : in_domain ? ROUTE_TIER_IN : ROUTE_TIER_OUT;
  } else if (path == SHEWCHUK_PATH_RECHECKED) {
    allowed = in_domain && (stage == STAGE_A || stage == STAGE_A0);
    route = ROUTE_RECHECKED;
  } else if (path == SHEWCHUK_PATH_ZERO_PRODUCTS) {
    allowed = pred == ORIENT2D && stage == STAGE_A0 && exact == 0;
    route = ROUTE_ZERO;
  } else if (floating_stage) {
    // Behind the first filter, floating-point arithmetic only inside the
    // domain. orient2d's stages B, Bx and C may also run under the window of
    // its first test (path LEAF); its stage D may not.
    allowed = in_domain || (pred == ORIENT2D && stage != STAGE_D && path == SHEWCHUK_PATH_LEAF);
    route = ROUTE_FLOATING;
  } else {
    allowed = path == SHEWCHUK_PATH_LEAF && (stage == STAGE_A || stage == STAGE_A0);
    route = ROUTE_LEAF;
  }
  if (!allowed) {
    if (totals.route_violations++ < 5) {
      std::printf("ROUTE NOT ALLOWED in %s: stage %d, path %d, tier %d, in the floating-point "
                  "domain: %d\n",
                  FAMILY_NAME[family], stage, path, tier, in_domain);
      print_case(pred, c);
    }
  }
  totals.routes[pred][route]++;

  // The guard at work: the 1996 code returned from its first test (stage A0
  // or A), the library did not return from its own.
  int reference_stage = -1;
  staged_reference_call(pred, c, &reference_stage);
  if ((reference_stage == STAGE_A0 || reference_stage == STAGE_A) && route != ROUTE_LEAF &&
      route != ROUTE_ZERO) {
    totals.guard_rejections[pred]++;
    if (route == ROUTE_RECHECKED) totals.guard_rejections_rechecked[pred]++;
  }
}

// ---------------------------------------------------------------------------
// The families
// ---------------------------------------------------------------------------

// Rescales a case by 2^k if that is exact; false if it is not.
bool rescale(const Case& one, int k, double* scaled) {
  if (one.max_exponent + k > 1023 || one.low_exponent + k < -1074) return false;
  for (int i = 0; i < NUM_COORDS[one.pred]; i++) scaled[i] = std::ldexp(one.coords[i], k);
  return true;
}

// Upper and lower sweep: every target exponent T, on one case in `stride`
// (rotating, so that over the sweep every case is used at many exponents).
void sweeps(const Subjects& subjects, Totals& totals, const std::vector<Case>& cases, int stride) {
  double scaled[MAX_COORDS];
  for (int t = PAPER_MAX_EXPONENT; t <= 1023; t++) {
    for (std::size_t i = static_cast<std::size_t>(t) % stride; i < cases.size(); i += stride) {
      if (rescale(cases[i], t - cases[i].max_exponent, scaled)) {
        const Pred pred = cases[i].pred;
        run_case(subjects, totals, UPPER, pred, scaled,
                 t <= REFERENCE_LIMITS[pred].max_exponent ? &totals.reference_inside[pred]
                                                          : &totals.reference_beyond[pred]);
      }
    }
  }
  for (int t = -150; t >= -1074; t--) {
    for (std::size_t i = static_cast<std::size_t>(-t) % stride; i < cases.size(); i += stride) {
      if (rescale(cases[i], t - cases[i].low_exponent, scaled)) {
        const Pred pred = cases[i].pred;
        run_case(subjects, totals, LOWER, pred, scaled,
                 t >= REFERENCE_LIMITS[pred].low_exponent ? &totals.reference_inside[pred]
                                                          : &totals.reference_beyond[pred]);
      }
    }
  }
  // Just inside the lower edge of the floating-point domain: every case,
  // with its lowest set bit at the lowest admitted exponent and a few above.
  for (const Case& one : cases) {
    const int lowest_grid = -((-FLOATING_POINT_MIN_GRID) / DEGREE[one.pred]);
    for (int t = lowest_grid; t < lowest_grid + 4; t++) {
      if (rescale(one, t - one.low_exponent, scaled)) {
        run_case(subjects, totals, LOWER, one.pred, scaled);
      }
    }
  }
}

// orient2d and orient3d are homogeneous in each axis separately: multiplying
// all x coordinates by 2^kx and all y coordinates by 2^ky multiplies the
// determinant by 2^(kx + ky). The exponents are drawn so that the products
// of two differences underflow, or overflow, while the determinant itself
// could be an ordinary number.
void axes(const Subjects& subjects, Totals& totals, const std::vector<Case>& cases, Rng& r,
          long per_case) {
  for (const Case& one : cases) {
    if (one.pred != ORIENT2D && one.pred != ORIENT3D) continue;
    const int dimension = DIMENSION[one.pred], points = NUM_POINTS[one.pred];
    // The exponents each axis can be shifted by, exactly.
    int lowest_shift[3], highest_shift[3];
    bool usable = true;
    for (int axis = 0; axis < dimension; axis++) {
      double column[5];
      for (int point = 0; point < points; point++) column[point] = one.coords[point * dimension + axis];
      const Extent extent = extent_of(column, points);
      if (extent.all_zero) {
        lowest_shift[axis] = highest_shift[axis] = 0;
        continue;
      }
      lowest_shift[axis] = -1074 - extent.lowest;
      highest_shift[axis] = 1024 - extent.highest;
      usable = usable && lowest_shift[axis] <= highest_shift[axis];
    }
    if (!usable) continue;
    for (long n = 0; n < per_case; n++) {
      double scaled[MAX_COORDS];
      for (int axis = 0; axis < dimension; axis++) {
        // Mostly at or near one end of what the axis allows.
        int k;
        switch (r.below(5)) {
          case 0: k = lowest_shift[axis]; break;
          case 1: k = highest_shift[axis]; break;
          case 2: k = lowest_shift[axis] + r.range(0, 600); break;
          case 3: k = highest_shift[axis] - r.range(0, 600); break;
          default: k = r.range(lowest_shift[axis], highest_shift[axis]); break;
        }
        k = std::max(lowest_shift[axis], std::min(highest_shift[axis], k));
        for (int point = 0; point < points; point++) {
          const int i = point * dimension + axis;
          scaled[i] = std::ldexp(one.coords[i], k);
        }
      }
      run_case(subjects, totals, AXES, one.pred, scaled);
    }
  }
}

// Small integers times powers of two from a short list of exponents. With so
// few distinct magnitudes the terms of the determinant cancel exactly all
// the time, and the list is chosen around the exponents at which a product
// of two, three, four or five coordinates crosses the subnormal threshold or
// the overflow threshold.
void spikes(const Subjects& subjects, Totals& totals, Rng& r, long per_predicate) {
  const int exponents[] = {-1074, -1073, -1022, -900, -720, -540, -537, -360, -358, -270,
                           -268,  -215,  -214,  -100, 0,    1,    100,  203,  204,  254,
                           255,   340,   341,   511,  512,  700,  1000, 1021};
  const int num_exponents = sizeof exponents / sizeof exponents[0];
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    for (long n = 0; n < per_predicate; n++) {
      // A few exponents per case, so that coordinates share magnitudes.
      int chosen[3];
      for (int& e : chosen) e = exponents[r.below(num_exponents)];
      const int used = r.range(1, 3);
      double c[MAX_COORDS];
      for (int i = 0; i < NUM_COORDS[p]; i++) {
        const int m = r.range(-3, 3);
        c[i] = std::ldexp(static_cast<double>(m), chosen[r.below(used)]);
        // Zeros of both signs.
        if (m == 0 && r.coin()) c[i] = -0.0;
      }
      run_case(subjects, totals, SPIKES, pred, c);
    }
  }
}

// Every coordinate is a subnormal number or zero: all products of two
// differences are zero in floating point.
void subnormals(const Subjects& subjects, Totals& totals, Rng& r, long per_predicate) {
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    for (long n = 0; n < per_predicate; n++) {
      const int bits = r.range(1, 52);
      double c[MAX_COORDS];
      for (int i = 0; i < NUM_COORDS[p]; i++) {
        const std::uint64_t magnitude = r.u64() >> (64 - bits);
        const double x = std::ldexp(static_cast<double>(magnitude), -1074);
        c[i] = r.below(8) == 0 ? 0.0 : r.coin() ? x : -x;
      }
      // Half the time make the configuration degenerate: one point repeats
      // another one, or lies between two others.
      if (r.coin()) {
        const int dimension = DIMENSION[p];
        const int from = r.below(NUM_POINTS[p] - 1), to = NUM_POINTS[p] - 1;
        for (int axis = 0; axis < dimension; axis++) c[to * dimension + axis] = c[from * dimension + axis];
        if (r.coin()) c[to * dimension] = c[((from + 1) % to) * dimension];
      }
      run_case(subjects, totals, SUBNORMAL, pred, c);
    }
  }
}

// Every coordinate has its own exponent from the whole format (huge next to
// tiny), short or full significands, and coordinates repeated between points
// so that differences vanish and terms cancel.
void full_range(const Subjects& subjects, Totals& totals, Rng& r, long per_predicate) {
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    for (long n = 0; n < per_predicate; n++) {
      double c[MAX_COORDS];
      const int style = static_cast<int>(r.below(4));
      for (int i = 0; i < NUM_COORDS[p]; i++) {
        int e;
        switch (style) {
          case 0: e = r.range(-1022, 1023); break;                     // anywhere
          case 1: e = r.coin() ? r.range(900, 1023) : r.range(-1022, -900); break;  // both ends
          case 2: e = r.range(-1022, -400); break;                     // tiny, far apart
          default: e = r.range(300, 1023); break;                      // huge, far apart
        }
        c[i] = r.coin() ? r.with_exponent(e) : r.with_exponent_short(e, r.range(1, 8));
        if (r.below(10) == 0) c[i] = 0.0;
      }
      const int repeats = r.range(0, NUM_COORDS[p] / 2);
      for (int k = 0; k < repeats; k++) c[r.below(NUM_COORDS[p])] = c[r.below(NUM_COORDS[p])];
      run_case(subjects, totals, FULL_RANGE, pred, c);
    }
  }
}

// ---------------------------------------------------------------------------
// No underflow or overflow inside the floating-point domain
// ---------------------------------------------------------------------------

// Calls the test-only build and reports whether the underflow or the
// overflow exception flag was raised during the call.
bool raises_flags(Pred pred, const double* c, int* stage) {
  std::feclearexcept(FE_UNDERFLOW | FE_OVERFLOW);
  staged_library_call(pred, c, stage);
  return std::fetestexcept(FE_UNDERFLOW | FE_OVERFLOW) != 0;
}

// The slice rescaled to the four corners of the floating-point domain: the
// largest coordinate at the largest exponent the domain admits, or the
// lowest set bit at the lowest grid it admits. Every call that a
// floating-point stage decides must leave both flags clear. (The integer
// stage performs no floating-point operation; calls it decides say nothing
// here.)
void flags_at_the_edge(Totals& totals, const std::vector<Case>& cases) {
  for (const Case& one : cases) {
    // ceil(FLOATING_POINT_MIN_GRID / degree): the lowest admitted exponent of
    // the lowest set bit.
    const int lowest_grid = -((-FLOATING_POINT_MIN_GRID) / DEGREE[one.pred]);
    for (const int k : {FLOATING_POINT_MAX_EXPONENT - one.max_exponent,
                        lowest_grid - one.low_exponent}) {
      double scaled[MAX_COORDS];
      if (!rescale(one, k, scaled) || !in_floating_point_domain(one.pred, scaled)) continue;
      int stage = -1;
      const bool raised = raises_flags(one.pred, scaled, &stage);
      if (stage < 0 || stage >= NUM_LIBRARY_STAGES || stage == STAGE_I) continue;
      totals.flag_checks[one.pred][stage]++;
      if (raised) {
        if (totals.flags_raised++ < 5) {
          std::printf("EXCEPTION FLAG raised inside the floating-point domain, stage %s\n",
                      STAGE_NAME[stage]);
          print_case(one.pred, scaled);
        }
      }
    }
  }
}

// The same check must be able to fail: far outside the domain the unguarded
// floating-point stages do underflow and overflow, and the flags say so.
long flags_outside(const Subjects& subjects, const std::vector<Case>& cases) {
  long raised = 0;
  for (const Case& one : cases) {
    for (const int k : {1000 - one.max_exponent, -1000 - one.low_exponent}) {
      double scaled[MAX_COORDS];
      if (!rescale(one, k, scaled)) continue;
      std::feclearexcept(FE_UNDERFLOW | FE_OVERFLOW);
      subjects.unguarded->call(one.pred, scaled);
      if (std::fetestexcept(FE_UNDERFLOW | FE_OVERFLOW) != 0) raised++;
    }
  }
  return raised;
}

// ---------------------------------------------------------------------------
// The reported wrong sign, and non-finite input
// ---------------------------------------------------------------------------

double from_bits(std::uint64_t bits) {
  double x;
  std::memcpy(&x, &bits, sizeof x);
  return x;
}

// An insphere call for which the 1996 code returns a positive value while
// the determinant is negative: its first filter certifies a determinant
// computed from products that underflowed.
void reproducer(const Subjects& subjects) {
  const std::uint64_t bits[15] = {
      0xb0cb68a751ddc700, 0x32efffffffffffff, 0xa730000000000000, 0xb0cb68a751ddc701,
      0xa730000000000000, 0x32efffffffffffff, 0xa730000000000000, 0xb2efffffffffffff,
      0x30cb68a751ddc700, 0xb0cb68a751ddc700, 0x2730000000000000, 0xb2efffffffffffff,
      0x2730000000000000, 0x32efffffffffffff, 0xb0cb68a751ddc700};
  double c[15];
  for (int i = 0; i < 15; i++) c[i] = from_bits(bits[i]);
  const int exact = oracle_sign(INSPHERE, c);
  const double reference = subjects.reference->call(INSPHERE, c);
  const double unguarded = subjects.unguarded->call(INSPHERE, c);
  const double library = subjects.library->call(INSPHERE, c);
  std::printf("reported insphere case: exact sign %d, 1996 code %a, library without guard %a, "
              "library %a\n",
              exact, reference, unguarded, library);
  CHECK(exact == -1);
  CHECK(reference > 0.0);  // the opposite sign, silently
  CHECK(unguarded > 0.0);
  CHECK(library < 0.0);
}

void non_finite(const Subjects& subjects, const std::vector<Case>& cases) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  long calls[NUM_PREDS] = {}, not_nan = 0, wrong_path = 0, not_flagged = 0;
  auto check = [&](Pred pred, const double* c) {
    calls[pred]++;
    if (!std::isnan(subjects.library->call(pred, c))) {
      if (not_nan++ < 5) {
        std::printf("NOT NaN for a non-finite coordinate: %a\n", subjects.library->call(pred, c));
        print_case(pred, c);
      }
    }
    int stage = -1;
    shewchuk_hooked_last_path = -1;
    if (!std::isnan(staged_library_call(pred, c, &stage))) not_nan++;
    if (shewchuk_hooked_last_path != SHEWCHUK_PATH_NOT_FINITE) wrong_path++;
    // The sign-returning form: never a sign, in particular never 0.
    if (library_sign(pred, c) != SHEWCHUK_SIGN_NOT_FINITE) not_flagged++;
  };
  Rng r(0x1f);
  for (const Case& one : cases) {
    // One coordinate replaced, at every position; the case at its own scale
    // and at the two ends of the format.
    for (int i = 0; i < NUM_COORDS[one.pred]; i++) {
      for (const int k : {0, 1000 - one.max_exponent, -1000 - one.low_exponent}) {
        double c[MAX_COORDS];
        if (!rescale(one, k, c)) continue;
        for (const double special : {nan, -nan, infinity, -infinity}) {
          const double saved = c[i];
          c[i] = special;
          check(one.pred, c);
          c[i] = saved;
        }
        if (k != 0) continue;
        // Several coordinates replaced, including all of them.
        for (int j = 0; j < NUM_COORDS[one.pred]; j++) {
          if (r.below(3) == 0) c[j] = r.coin() ? nan : r.coin() ? infinity : -infinity;
        }
        c[i] = r.coin() ? nan : infinity;
        check(one.pred, c);
      }
    }
  }
  for (int p = 0; p < NUM_PREDS; p++) {
    double c[MAX_COORDS];
    for (const double special : {nan, infinity, -infinity}) {
      for (int i = 0; i < NUM_COORDS[p]; i++) c[i] = special;
      check(static_cast<Pred>(p), c);
    }
    std::printf("%-8s %ld calls with a coordinate that is not finite\n", PRED_NAME[p], calls[p]);
    CHECK(calls[p] > 5000);
  }
  if (not_nan) std::printf("%ld calls with a non-finite coordinate did not return NaN\n", not_nan);
  CHECK(not_nan == 0);
  CHECK(wrong_path == 0);
  if (not_flagged) {
    std::printf("%ld calls with a non-finite coordinate: the sign-returning form did not return "
                "SHEWCHUK_SIGN_NOT_FINITE\n",
                not_flagged);
  }
  CHECK(not_flagged == 0);
}

// ---------------------------------------------------------------------------
// --sweep: the two sweeps for any implementation of the table, printed
// ---------------------------------------------------------------------------

struct Outcome {
  long cases = 0;
  long wrong_sign = 0;  // finite result with the wrong sign
  long not_finite = 0;  // NaN or infinity
  long wrong() const { return wrong_sign + not_finite; }
  // The first case with a wrong result, as called, and what was returned.
  double first_wrong[MAX_COORDS] = {};
  double first_wrong_value = 0;
};

// Rescales every case of `pred` so that its largest exponent (upper) or the
// exponent of its lowest set bit (lower) is t, and judges the results (what
// an implementation declares about its own conventions is honoured, judge.h).
Outcome run_sweep_step(const Implementation& implementation, const std::vector<Case>& cases,
                       Pred pred, bool upper, int t) {
  Outcome outcome;
  for (const Case& one : cases) {
    if (one.pred != pred) continue;
    double scaled[MAX_COORDS];
    if (!rescale(one, t - (upper ? one.max_exponent : one.low_exponent), scaled)) continue;
    const double value = implementation.call(pred, scaled);
    const Verdict verdict =
        judge(implementation, pred, scaled, value, oracle_sign(pred, scaled)).verdict;
    if (verdict == Verdict::UNDEFINED || verdict == Verdict::OUTSIDE_PRECONDITION) continue;
    outcome.cases++;
    if (verdict == Verdict::WRONG && outcome.wrong() == 0) {
      std::copy(scaled, scaled + NUM_COORDS[pred], outcome.first_wrong);
      outcome.first_wrong_value = value;
    }
    if (!std::isfinite(value)) {
      outcome.not_finite++;
    } else if (verdict == Verdict::WRONG) {
      outcome.wrong_sign++;
    }
  }
  return outcome;
}

// Prints, once per direction, the first wrong result of a sweep as a
// reproducer: the coordinates as hexadecimal floats.
void print_first_wrong(Pred pred, const Outcome& o, bool* printed) {
  if (*printed || o.wrong() == 0) return;
  *printed = true;
  std::printf("    first wrong result: returned %a, exact sign %d, for %s", o.first_wrong_value,
              oracle_sign(pred, o.first_wrong), PRED_NAME[pred]);
  for (int i = 0; i < NUM_COORDS[pred]; i++) std::printf(" %a", o.first_wrong[i]);
  std::printf("\n");
}

void print_sweep(const Implementation& implementation, const std::vector<Case>& cases, int step) {
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    bool printed_upper = false, printed_lower = false;
    std::printf("%s, largest coordinate exponent T: cases, wrong sign, not finite\n",
                PRED_NAME[p]);
    for (int t = 150; t <= 1023; t += step) {
      const Outcome o = run_sweep_step(implementation, cases, pred, true, t);
      if (o.wrong() || t % 50 == 0) {
        std::printf("  T=%5d %6ld %6ld %6ld\n", t, o.cases, o.wrong_sign, o.not_finite);
      }
      print_first_wrong(pred, o, &printed_upper);
    }
    std::printf("%s, lowest set bit exponent T: cases, wrong sign, not finite\n", PRED_NAME[p]);
    for (int t = -150; t >= -1074; t -= step) {
      const Outcome o = run_sweep_step(implementation, cases, pred, false, t);
      if (o.wrong() || t % 50 == 0) {
        std::printf("  T=%5d %6ld %6ld %6ld\n", t, o.cases, o.wrong_sign, o.not_finite);
      }
      print_first_wrong(pred, o, &printed_lower);
    }
  }
}

void print_tallies(const char* subject, const Tally (*tallies)[NUM_FAMILIES]) {
  std::printf("\n%s: wrong results / cases (wrong = wrong sign, or not finite)\n%-9s", subject,
              "predicate");
  for (int f = 0; f < NUM_FAMILIES; f++) std::printf(" %19s", FAMILY_NAME[f]);
  std::printf("\n");
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s", PRED_NAME[p]);
    for (int f = 0; f < NUM_FAMILIES; f++) {
      if (tallies[p][f].cases == 0) {
        std::printf(" %19s", "-");
      } else {
        std::printf(" %9ld / %7ld", tallies[p][f].wrong(), tallies[p][f].cases);
      }
    }
    std::printf("\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool deep = argc > 1 && std::strcmp(argv[1], "deep") == 0;
  const bool sweep = argc > 1 && std::strcmp(argv[1], "--sweep") == 0;
  if (argc > 1 && !deep && !sweep) {
    std::printf("usage: test_input_domain [deep | --sweep [NAME [STEP]]]\n");
    return 2;
  }
  initialize_implementations();
  if (sweep) {
    const Implementation* swept = find_implementation(argc >= 3 ? argv[2] : "library");
    if (!swept || swept->unavailable_reason()) {
      std::printf("implementation '%s' is unknown or unavailable\n", argv[2]);
      return 2;
    }
    const int step = argc >= 4 ? std::max(1, std::atoi(argv[3])) : 1;
    print_sweep(*swept, collect_cases(), step);
    return 0;
  }
  Subjects subjects;
  subjects.library = find_implementation("library");
  subjects.reference = &reference_implementation();
  // The library without its guard and its integer stage: the floating-point
  // stages of 1996 alone.
  subjects.unguarded = find_implementation("library_expansion_only");
  const std::vector<Case> cases = collect_cases();
  std::printf("%zu corpus cases in the slice\n", cases.size());
  CHECK(cases.size() > 1000);

  static Totals totals;
  Rng r(0xd0a1);
  sweeps(subjects, totals, cases, deep ? 8 : 48);
  axes(subjects, totals, cases, r, deep ? 30 : 5);
  spikes(subjects, totals, r, deep ? 60000 : 8000);
  subnormals(subjects, totals, r, deep ? 20000 : 3000);
  full_range(subjects, totals, r, deep ? 15000 : 2000);

  print_tallies("the library", totals.library);
  print_tallies("the 1996 code (negative control)", totals.reference);
  print_tallies("the library without guard and integer stage (negative control)",
                totals.unguarded);

  for (int p = 0; p < NUM_PREDS; p++) {
    for (int f = 0; f < NUM_FAMILIES; f++) {
      const Tally& library = totals.library[p][f];
      if (f == AXES && p != ORIENT2D && p != ORIENT3D) {
        CHECK(library.cases == 0);
        continue;
      }
      // The contract: the exact sign for every finite input.
      CHECK(library.cases >= 2000);
      CHECK(library.wrong_sign == 0);
      CHECK(library.not_finite == 0);
      // The negative control: the same inputs do make the 1996 code fail,
      // and the library's floating-point stages alone too.
      CHECK(totals.reference[p][f].wrong() > 20);
      CHECK(totals.unguarded[p][f].wrong() > 20);
    }
    // Underflow gives the 1996 code finite wrong signs, with nothing to
    // signal them; overflow gives it NaN or an infinity.
    CHECK(totals.reference[p][LOWER].wrong_sign > 0);
    CHECK(totals.reference[p][UPPER].not_finite > 0);
    // Where the 1996 code stops being exact on this slice.
    std::printf("%-9s the 1996 code in the sweeps: %ld wrong of %ld with the largest exponent up "
                "to %d and the lowest set bit from 2^%d; %ld wrong of %ld beyond\n",
                PRED_NAME[p], totals.reference_inside[p].wrong(), totals.reference_inside[p].cases,
                REFERENCE_LIMITS[p].max_exponent, REFERENCE_LIMITS[p].low_exponent,
                totals.reference_beyond[p].wrong(), totals.reference_beyond[p].cases);
    CHECK(totals.reference_inside[p].cases > 500);
    CHECK(totals.reference_inside[p].wrong() == 0);
    CHECK(totals.reference_beyond[p].wrong() > 1000);
  }

  std::printf("\nhow the library decided these calls\n%-9s", "predicate");
  for (int route = 0; route < NUM_ROUTES; route++) std::printf(" %9s", ROUTE_NAME[route]);
  std::printf("\n");
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s", PRED_NAME[p]);
    for (int route = 0; route < NUM_ROUTES; route++) std::printf(" %9ld", totals.routes[p][route]);
    std::printf("\n");
    // Every new route is taken, often.
    for (int route = 0; route < NUM_ROUTES; route++) {
      if (route == ROUTE_ZERO && p != ORIENT2D) {
        CHECK(totals.routes[p][route] == 0);
      } else {
        CHECK(totals.routes[p][route] >= 100);
      }
    }
  }
  CHECK(totals.route_violations == 0);
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s the 1996 code returns from its first test, the library's guard does not let "
                "it: %ld calls (%ld returned by the recheck)\n",
                PRED_NAME[p], totals.guard_rejections[p], totals.guard_rejections_rechecked[p]);
    CHECK(totals.guard_rejections[p] > 300);
  }
  if (totals.hook_value_differences) {
    std::printf("%ld values differ between the library and its test-only build\n",
                totals.hook_value_differences);
  }
  CHECK(totals.hook_value_differences == 0);
  if (totals.sign_form_differences) {
    std::printf("%ld results of the sign-returning form are not the exact sign\n",
                totals.sign_form_differences);
  }
  CHECK(totals.sign_form_differences == 0);

  flags_at_the_edge(totals, cases);
  std::printf("\ncalls at the edge of the floating-point domain decided by a floating-point "
              "stage, all without\nunderflow or overflow exception:\n%-9s", "predicate");
  for (int s = 0; s < NUM_STAGES; s++) std::printf(" %7s", STAGE_NAME[s]);
  std::printf("\n");
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s", PRED_NAME[p]);
    long behind_filter = 0;
    for (int s = 0; s < NUM_STAGES; s++) {
      std::printf(" %7ld", totals.flag_checks[p][s]);
      if (s >= STAGE_B) behind_filter += totals.flag_checks[p][s];
    }
    std::printf("\n");
    CHECK(totals.flag_checks[p][STAGE_A] > 100);
    CHECK(behind_filter > 20);
  }
  CHECK(totals.flags_raised == 0);
  const long outside = flags_outside(subjects, cases);
  std::printf("far outside it, the unguarded stages raised a flag in %ld calls\n", outside);
  CHECK(outside > 1000);

  reproducer(subjects);
  non_finite(subjects, cases);
  return finish("input domain");
}
