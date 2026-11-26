// Tests of the library's exact integer stage on its own.
//
//   test_integer_stage [tier]
//
// src/integer_stage.h is compiled into this file, so the stage is called
// directly, without the floating-point filter in front of it. The build
// system compiles the file four times: with the stage's default limb
// arithmetic and bit counting and with the portable ones forced
// (SHEWCHUK_INTEGER_STAGE_PORTABLE), each with and without the run-time
// verification of the proof's bounds (SHEWCHUK_INTEGER_STAGE_CHECKED: every
// addition and subtraction is checked for signed overflow, and every
// intermediate against the bit width docs/proofs/integer-stage.md proves for
// it).
//
// What is tested:
//   1. the limb arithmetic against the oracle's big integers;
//   2. the scaling (doubles -> integers): zeros, negative zero, subnormals,
//      the largest doubles, the whole exponent range, infinities and NaNs;
//   3. force-stage: the stage as the sole decider on every corpus case it
//      accepts must return the oracle's sign, must accept exactly the cases
//      whose width (tests/width.h, independent code) is at most 317, in the
//      tier that width selects, and must see enough cases in every tier;
//   4. worst-case magnitudes: inputs built to make every intermediate as
//      large as possible, at widths exactly at each tier's limit and one past
//      it, anywhere in the exponent range of a double (subnormals included).
//      A case one past the widest fast tier is refused if it is inside the
//      floating-point domain (tests/width.h) and decided by the wide tier
//      if it is not; the wide tier is also run at the largest width there
//      is, and with every number of limbs it can work with, and every case is
//      run under every request (Request) of the stage;
//   5. negative control: a tier evaluated on inputs wider than its limit IS
//      caught returning wrong signs (and, in the checked build, IS caught by
//      the bound checks);
//   6. non-finite coordinates are reported as such, and the library then
//      returns NaN;
//   7. checked build only: no bound of the proof was violated anywhere above,
//      and the worst-case inputs came within 5 bits of every bound.
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "integer_stage.h"

#include "bigint.h"
#include "check.h"
#include "corpus.h"
#include "generators.h"
#include "implementations.h"
#include "oracle.h"
#include "width.h"

using namespace shewchuk_test;
namespace stage = shewchuk::detail::integer_stage;

namespace {

static_assert(stage::max_width(stage::num_tiers) == INTEGER_STAGE_MAX_WIDTH,
              "tests/width.h must state the library's widest fast tier");
static_assert(stage::max_width(1) == 61 && stage::max_width(2) == 125 &&
                  stage::max_width(3) == 189 && stage::max_width(4) == 253 &&
                  stage::num_tiers == 5,
              "tests/width.h must state the library's tier limits");
static_assert(stage::wide_tier == INTEGER_STAGE_WIDE_TIER,
              "tests/width.h must state the library's wide tier");
static_assert(stage::floating_point_max_exponent == FLOATING_POINT_MAX_EXPONENT &&
                  stage::floating_point_min_grid == FLOATING_POINT_MIN_GRID,
              "tests/width.h must state the library's floating-point domain");

// Index of a tier in a table with one column per tier: 0 for "not decided",
// 1 to 5 for the fast tiers, 6 for the wide tier.
int tier_column(int tier) { return tier == stage::wide_tier ? stage::num_tiers + 1 : tier; }

// The integer stage's answer for a case in the suite's flat layout.
int stage_sign(Pred pred, const double* c,
               stage::Request request = stage::Request::exact_or_floating_point) {
  switch (pred) {
    case ORIENT2D: return stage::orient2d(c, c + 2, c + 4, request);
    case ORIENT3D: return stage::orient3d(c, c + 3, c + 6, c + 9, request);
    case INCIRCLE: return stage::incircle(c, c + 2, c + 4, c + 6, request);
    default: return stage::insphere(c, c + 3, c + 6, c + 9, c + 12, request);
  }
}

void print_case(Pred pred, const double* c) {
  std::printf("    %s", PRED_NAME[pred]);
  for (int i = 0; i < NUM_COORDS[pred]; i++) std::printf(" %a", c[i]);
  std::printf("\n");
}

// Runs the stage on one finite case and checks everything that can be checked
// about a single call: the recorded width equals the independently computed
// one, the deciding tier is the one the width and the floating-point domain
// select (tests/width.h; the call is refused exactly when it is wider than
// the fast tiers and inside that domain), and the sign is the oracle's.
// Then the two other requests: `always_exact` never refuses (a refused case
// goes to the wide tier), and `prefer_floating_point` refuses exactly the
// cases inside the domain. Returns the tier of the first call (0 if refused).
//
// The wide tier is slow (it multiplies integers of thousands of bits), and
// the corpus has many refused cases: with `sample_wide`, only one refused
// case in 16 is also sent to the wide tier.
int check_case(Pred pred, const double* c, bool sample_wide = false) {
  const int width = width_of(c, NUM_COORDS[pred]);
  const int tier = expected_integer_tier(pred, c);
  const int exact = oracle_sign(pred, c);
  const int sign = stage_sign(pred, c);
  bool ok = stage::last_width == width && stage::last_tier == tier;
  ok = ok && sign == (tier == 0 ? stage::refused : exact);
  static unsigned long refused_cases = 0;
  if (tier != 0 || !sample_wide || refused_cases++ % 16 == 0) {
    const int forced = stage_sign(pred, c, stage::Request::always_exact);
    ok = ok && forced == exact && stage::last_tier == (tier == 0 ? stage::wide_tier : tier);
  }
  {
    const bool in_domain = in_floating_point_domain(pred, c) && width != 0;
    const int preferred = stage_sign(pred, c, stage::Request::prefer_floating_point);
    ok = ok && (in_domain ? preferred == stage::refused && stage::last_tier == 0
                          : preferred == exact && stage::last_tier != 0);
  }
  if (!ok) {
    static int reports = 0;
    if (reports++ < 5) {
      std::printf("  integer stage: returned %d in tier %d at width %d; independent width %d, "
                  "expected tier %d, exact sign %d\n",
                  sign, stage::last_tier, stage::last_width, width, tier, exact);
      print_case(pred, c);
    }
  }
  CHECK(ok);
  return tier;
}

// ---------------------------------------------------------------------------
// 1. Limb arithmetic against the oracle's big integers
// ---------------------------------------------------------------------------

BigInt pow2(unsigned k) { return BigInt(1).shifted_left(k); }

// An unsigned 64-bit value as a big integer.
BigInt from_limb(stage::limb x) {
  return BigInt(static_cast<std::int64_t>(x >> 32)).shifted_left(32) +
         BigInt(static_cast<std::int64_t>(x & 0xffffffffu));
}

// The value of a two's-complement integer.
template <int N>
BigInt value_of(const stage::Int<N>& a) {
  BigInt value;
  for (int i = 0; i < N; i++) value = value + from_limb(a.w[i]).shifted_left(64u * i);
  if (a.w[N - 1] >> 63) value = value - pow2(64u * N);
  return value;
}

// A random N-limb integer whose top `headroom` bits below the sign bit are
// copies of the sign bit (so that sums of two of them cannot overflow when
// headroom >= 1). Limbs are sometimes all zeros or all ones: carry chains.
template <int N>
stage::Int<N> random_int(Rng& r, int headroom) {
  stage::Int<N> a;
  for (int i = 0; i < N; i++) {
    const int kind = static_cast<int>(r.below(6));
    a.w[i] = kind == 0 ? 0 : kind == 1 ? ~stage::limb{0} : r.u64();
  }
  // Arithmetic shift of the top limb, written with unsigned operations.
  const stage::limb top = a.w[N - 1];
  const stage::limb sign_fill = (top >> 63) ? ~stage::limb{0} << (63 - headroom) : 0;
  a.w[N - 1] = (top >> headroom) | sign_fill;
  return a;
}

template <int N>
void test_add_sub(Rng& r, int count) {
  for (int i = 0; i < count; i++) {
    const int headroom = r.range(1, 63);
    const stage::Int<N> a = random_int<N>(r, headroom), b = random_int<N>(r, headroom);
    CHECK(value_of(stage::add(a, b)) == value_of(a) + value_of(b));
    CHECK(value_of(stage::sub(a, b)) == value_of(a) - value_of(b));
    const int expected_sign = value_of(a).sign();
    CHECK(stage::sign(a) == expected_sign);
  }
}

template <int N, int M>
void test_mul(Rng& r, int count) {
  for (int i = 0; i < count; i++) {
    const stage::Int<N> a = random_int<N>(r, r.range(0, 63));
    const stage::Int<M> b = random_int<M>(r, r.range(0, 63));
    CHECK(value_of(stage::mul(a, b)) == value_of(a) * value_of(b));
    CHECK(value_of(stage::mul(b, a)) == value_of(a) * value_of(b));
  }
}

// The wide tier's integers: the same operations on a number of limbs chosen
// at run time.
template <int K>
BigInt value_of(const stage::Var<K>& a) {
  BigInt value;
  for (int i = 0; i < a.n; i++) value = value + from_limb(a.w[i]).shifted_left(64u * i);
  if (a.w[a.n - 1] >> 63) value = value - pow2(64u * a.n);
  return value;
}

template <int K>
stage::Var<K> random_var(Rng& r, int n, int headroom) {
  stage::Var<K> a;
  a.n = n;
  for (int i = 0; i < n; i++) {
    const int kind = static_cast<int>(r.below(6));
    a.w[i] = kind == 0 ? 0 : kind == 1 ? ~stage::limb{0} : r.u64();
  }
  const stage::limb top = a.w[n - 1];
  const stage::limb sign_fill = (top >> 63) ? ~stage::limb{0} << (63 - headroom) : 0;
  a.w[n - 1] = (top >> headroom) | sign_fill;
  return a;
}

// Every number of limbs from 1 to the storage of a Var<2> (sums) and every
// pair of sizes of a Var<2> and a Var<3> a tier of D limbs multiplies
// (2D x 2D and 2D x 3D; D x D and D x 2D are among the sizes of the first).
void test_run_time_limbs(Rng& r) {
  for (int n = 1; n <= 2 * stage::wide_tier; n++) {
    for (int i = 0; i < 12; i++) {
      const int headroom = r.range(1, 63);
      const stage::Var<2> a = random_var<2>(r, n, headroom), b = random_var<2>(r, n, headroom);
      const stage::Var<2> sum = stage::add(a, b), difference = stage::sub(a, b);
      CHECK(sum.n == n && difference.n == n);
      CHECK(value_of(sum) == value_of(a) + value_of(b));
      CHECK(value_of(difference) == value_of(a) - value_of(b));
      CHECK(stage::sign(a) == value_of(a).sign());
    }
  }
  for (int D = 1; D <= stage::wide_tier; D++) {
    for (int i = 0; i < 3; i++) {
      const stage::Var<2> a = random_var<2>(r, 2 * D, r.range(0, 63));
      const stage::Var<2> b = random_var<2>(r, i == 0 ? D : 2 * D, r.range(0, 63));
      const stage::Var<3> c = random_var<3>(r, i == 1 ? D : 3 * D, r.range(0, 63));
      const stage::Var<4> ab = stage::mul(a, b);
      const stage::Var<5> ac = stage::mul(a, c);
      const stage::Var<5> ca = stage::mul(c, a);
      CHECK(ab.n == a.n + b.n && ac.n == a.n + c.n && ca.n == ac.n);
      CHECK(value_of(ab) == value_of(a) * value_of(b));
      CHECK(value_of(ac) == value_of(a) * value_of(c));
      CHECK(value_of(ca) == value_of(a) * value_of(c));
    }
  }
  // The most negative value times itself: the largest product there is.
  stage::Var<1> most_negative;
  most_negative.n = 2;
  most_negative.w[0] = 0, most_negative.w[1] = stage::limb{1} << 63;
  CHECK(value_of(stage::mul(most_negative, most_negative)) == pow2(254));
}

void test_limb_arithmetic() {
  std::printf("limb arithmetic: %s\n", stage::limb_arithmetic);
  Rng r(0x1e57);

  // Bit counting against the definitions, on every single bit, every run of
  // low ones, zero, and random values.
  auto check_bit_counting = [](stage::limb x) {
    int length = 0, zeros = 0;
    while (length < 64 && (x >> length) != 0) length++;
    while (zeros < 64 && ((x >> zeros) & 1) == 0) zeros++;
    CHECK(stage::bit_length(x) == length);
    CHECK(stage::trailing_zeros(x) == zeros);
  };
  check_bit_counting(0);
  check_bit_counting(~stage::limb{0});
  for (int bit = 0; bit < 64; bit++) {
    check_bit_counting(stage::limb{1} << bit);
    check_bit_counting((stage::limb{1} << bit) - 1);
    check_bit_counting(r.u64() << bit);
    check_bit_counting(r.u64() >> bit);
    check_bit_counting((r.u64() | 1) << bit);
  }

  // The primitives on edge values and random ones.
  const stage::limb edges[] = {0,          1,          2,          0xffffffffu,
                               0x100000000u, 0x100000001u, 0x7fffffffffffffffu,
                               0x8000000000000000u, 0xfffffffffffffffeu, 0xffffffffffffffffu};
  const int num_edges = static_cast<int>(sizeof edges / sizeof edges[0]);
  auto pick = [&] { return r.below(3) == 0 ? r.u64() : edges[r.below(num_edges)]; };
  for (int i = 0; i < 20000; i++) {
    const stage::limb a = pick(), b = pick(), c = pick(), d = pick();
    stage::limb high;
    const stage::limb low = stage::multiply_add(a, b, c, d, high);
    CHECK(from_limb(high).shifted_left(64) + from_limb(low) ==
          from_limb(a) * from_limb(b) + from_limb(c) + from_limb(d));
    for (stage::limb carry_in = 0; carry_in < 2; carry_in++) {
      stage::limb carry = carry_in;
      const stage::limb sum = stage::add_carry(a, b, carry);
      CHECK(carry <= 1);
      CHECK(from_limb(carry).shifted_left(64) + from_limb(sum) ==
            from_limb(a) + from_limb(b) + from_limb(carry_in));
      stage::limb borrow = carry_in;
      const stage::limb difference = stage::sub_borrow(a, b, borrow);
      CHECK(borrow <= 1);
      CHECK(from_limb(difference) - from_limb(borrow).shifted_left(64) ==
            from_limb(a) - from_limb(b) - from_limb(carry_in));
    }
  }

  // Every operand size the five fast tiers use (D, 2D, ..., 5D limbs) ...
  test_add_sub<1>(r, 2000), test_add_sub<2>(r, 2000), test_add_sub<3>(r, 2000);
  test_add_sub<4>(r, 2000), test_add_sub<5>(r, 2000), test_add_sub<6>(r, 2000);
  test_add_sub<8>(r, 1000), test_add_sub<9>(r, 1000), test_add_sub<10>(r, 1000);
  test_add_sub<12>(r, 1000), test_add_sub<15>(r, 1000), test_add_sub<16>(r, 1000);
  test_add_sub<20>(r, 1000), test_add_sub<25>(r, 1000);
  // ... and every product: D x D, D x 2D, 2D x 2D, 2D x 3D.
  test_mul<1, 1>(r, 4000), test_mul<1, 2>(r, 4000), test_mul<2, 2>(r, 4000);
  test_mul<2, 3>(r, 4000);
  test_mul<2, 4>(r, 2000), test_mul<4, 4>(r, 2000), test_mul<4, 6>(r, 2000);
  test_mul<3, 3>(r, 2000), test_mul<3, 6>(r, 2000), test_mul<6, 6>(r, 1000);
  test_mul<6, 9>(r, 1000);
  test_mul<4, 8>(r, 1000), test_mul<8, 8>(r, 1000), test_mul<8, 12>(r, 1000);
  test_mul<5, 5>(r, 1000), test_mul<5, 10>(r, 1000), test_mul<10, 10>(r, 1000);
  test_mul<10, 15>(r, 1000);

  // The most negative value times itself: the largest product there is.
  stage::Int<2> most_negative = {{0, stage::limb{1} << 63}};
  CHECK(value_of(stage::mul(most_negative, most_negative)) == pow2(254));

  test_run_time_limbs(r);
}

// ---------------------------------------------------------------------------
// 2. Scaling
// ---------------------------------------------------------------------------

// The exponent of the lowest set bit of any coordinate (independent code).
int lowest_exponent(const double* x, int count) {
  int lowest = INT_MAX;
  for (int i = 0; i < count; i++) {
    if (x[i] == 0.0) continue;
    int e;
    double fraction = std::frexp(std::fabs(x[i]), &e);
    // Shift the fraction left until it is an integer; count the steps.
    int low = e;
    while (fraction != std::floor(fraction)) {
      fraction *= 2.0;
      low--;
    }
    if (low < lowest) lowest = low;
  }
  return lowest;
}

// Checks scale() on K finite doubles: every coordinate is reconstructed
// exactly from its magnitude, shift and sign, and the width is the
// independently computed one.
template <int K>
void check_scaling(const double* x) {
  stage::limb bits[K];
  stage::load_bits(x, K, bits);
  stage::Scaled<K> s;
  const bool finite = stage::scale<K>(bits, s);
  CHECK(finite);
  if (!finite) return;
  bool ok = s.width == width_of(x, K);
  const int low = lowest_exponent(x, K);
  for (int i = 0; i < K; i++) {
    ok = ok && s.negative[i] == (std::signbit(x[i]) ? ~stage::limb{0} : 0);
    if (x[i] == 0.0) {
      // The shift of a zero has no meaning; it only must not be negative.
      ok = ok && s.magnitude[i] == 0 && s.shift[i] >= 0;
      continue;
    }
    ok = ok && (s.magnitude[i] & 1) == 1 && s.magnitude[i] < (stage::limb{1} << 53);
    ok = ok && s.shift[i] >= 0 && s.shift[i] < s.width;
    // A magnitude below 2^53 converts exactly, and ldexp is exact when its
    // result is representable, as the original coordinate is.
    const double rebuilt = std::ldexp(static_cast<double>(s.magnitude[i]), s.shift[i] + low);
    ok = ok && rebuilt == std::fabs(x[i]);
  }
  if (!ok) {
    static int reports = 0;
    if (reports++ < 5) {
      std::printf("  scale() is wrong (width %d, independent width %d) on:", s.width,
                  width_of(x, K));
      for (int i = 0; i < K; i++) std::printf(" %a", x[i]);
      std::printf("\n");
    }
  }
  CHECK(ok);
}

void check_scaling_all_sizes(const double* x15) {
  check_scaling<6>(x15), check_scaling<8>(x15), check_scaling<12>(x15), check_scaling<15>(x15);
}

// A random finite double from anywhere in the format: zeros, subnormals,
// short significands, any exponent.
double any_finite_double(Rng& r) {
  const int kind = static_cast<int>(r.below(10));
  if (kind == 0) return r.coin() ? 0.0 : -0.0;
  std::uint64_t fraction = r.u64() & ((std::uint64_t{1} << 52) - 1);
  if (kind == 1) fraction &= ~std::uint64_t{0} << r.range(1, 52);  // trailing zeros
  if (kind == 2) fraction = std::uint64_t{1} << r.range(0, 51);    // one bit
  std::uint64_t exponent = r.below(2047);                          // 2047 is inf / NaN
  if (kind == 3) exponent = 0;                                     // subnormal
  if (kind == 4) exponent = r.coin() ? 1 : 2046;                   // smallest, largest binade
  const std::uint64_t bits = (static_cast<std::uint64_t>(r.coin()) << 63) | (exponent << 52) | fraction;
  double x;
  std::memcpy(&x, &bits, sizeof x);
  return x;
}

void test_scaling() {
  const double tiny = std::numeric_limits<double>::denorm_min();  // 2^-1074
  const double huge = std::numeric_limits<double>::max();
  double x[15];

  // All zeros, of either sign: width 0.
  for (int i = 0; i < 15; i++) x[i] = (i % 2) ? -0.0 : 0.0;
  check_scaling_all_sizes(x);
  CHECK(width_of(x, 15) == 0);

  // One nonzero coordinate at each extreme of the format: its odd part is the
  // whole scaled value.
  for (const double value : {tiny, -tiny, huge, -huge, DBL_MIN, 1.0, -0.75, 0x1.fffffffffffffp-1022,
                             0x0.fffffffffffffp-1022, 0x0.0000000000003p-1022}) {
    for (int position = 0; position < 6; position++) {
      for (int i = 0; i < 15; i++) x[i] = 0.0;
      x[position] = value;
      check_scaling_all_sizes(x);
    }
  }
  // The widest case there is: the smallest and the largest double together,
  // 2^-1074 up to just below 2^1024.
  for (int i = 0; i < 15; i++) x[i] = (i % 3 == 0) ? tiny : (i % 3 == 1) ? -huge : 0.0;
  check_scaling_all_sizes(x);
  CHECK(width_of(x, 6) == 2098);
  // Subnormals only, and subnormals next to the smallest normal numbers.
  for (int i = 0; i < 15; i++) x[i] = (i + 1) * tiny * ((i % 2) ? -1.0 : 1.0);
  check_scaling_all_sizes(x);
  for (int i = 0; i < 15; i++) x[i] = DBL_MIN + (i % 4) * tiny;
  check_scaling_all_sizes(x);

  // Random doubles from the whole format.
  Rng r(0x5ca1e);
  for (int n = 0; n < 50000; n++) {
    for (int i = 0; i < 15; i++) x[i] = any_finite_double(r);
    check_scaling_all_sizes(x);
  }
  // Random doubles within a few binades of each other, anywhere in the
  // format (the cases the tiers accept).
  for (int n = 0; n < 50000; n++) {
    const int base = r.range(-1070, 1000);
    for (int i = 0; i < 15; i++) {
      x[i] = r.below(8) == 0 ? 0.0 : std::ldexp(r.with_exponent(0), base + r.range(0, 20));
    }
    check_scaling_all_sizes(x);
  }

  // Infinities and NaNs of every flavor, at every position: scale() says no.
  const double inf = std::numeric_limits<double>::infinity();
  const double quiet = std::numeric_limits<double>::quiet_NaN();
  const double signaling = std::numeric_limits<double>::signaling_NaN();
  for (const double bad : {inf, -inf, quiet, -quiet, signaling}) {
    for (int position = 0; position < 15; position++) {
      for (int i = 0; i < 15; i++) x[i] = r.symmetric();
      x[position] = bad;
      stage::limb bits[15];
      stage::load_bits(x, 15, bits);
      stage::Scaled<15> s15;
      CHECK(!stage::scale<15>(bits, s15));
      if (position < 6) {
        stage::Scaled<6> s6;
        CHECK(!stage::scale<6>(bits, s6));
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 3. Force-stage on the corpus
// ---------------------------------------------------------------------------

void test_corpus(const Tier& tier) {
  long decided[NUM_PREDS][stage::num_tiers + 2] = {};
  long total[NUM_PREDS] = {};
  for_each_case(tier.cases_per_generator, [&](const Generator& generator, const double* c) {
    decided[generator.pred][tier_column(check_case(generator.pred, c, true))]++;
    total[generator.pred]++;
  });
  std::printf("\nForce-stage on the corpus (tier %s): cases decided by each tier of the integer "
              "stage\n%-9s %9s %9s %9s %9s %9s %9s %9s\n",
              tier.name, "predicate", "cases", "tier 1", "tier 2", "tier 3", "tier 4", "tier 5",
              "refused");
  static_assert(stage::num_tiers == 5, "one column per fast tier");
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s %9ld %9ld %9ld %9ld %9ld %9ld %9ld\n", PRED_NAME[p], total[p], decided[p][1],
                decided[p][2], decided[p][3], decided[p][4], decided[p][5], decided[p][0]);
    // Every fast tier, and the refusal, must see a real share of each
    // predicate's cases: at least 2%. The corpus is inside the floating-point
    // domain, so the wide tier decides none of it on the first request
    // (check_case runs it on one refused case in 16). Tiers 4 and 5 have the
    // "span_tier" families of the corpus for this.
    for (int t = 0; t <= stage::num_tiers; t++) CHECK(decided[p][t] >= total[p] / 50);
    CHECK(decided[p][stage::num_tiers + 1] == 0);
  }
}

// ---------------------------------------------------------------------------
// 4. Worst-case magnitudes at the tier limits
// ---------------------------------------------------------------------------

// A scaled coordinate as significand * 2^exponent, the significand an
// integer of at most 53 bits. (Kept apart because 2^exponent alone can be
// beyond the range of a double: widths go up to 2098.)
struct Magnitude {
  double significand;
  int exponent;
};

// The largest magnitude a scaled coordinate of width W can have: 2^W - 1 if
// that has at most 53 bits, else (2^53 - 1) 2^(W-53).
Magnitude largest_magnitude(int W) {
  if (W <= 53) return {std::ldexp(1.0, W) - 1.0, 0};
  return {0x1.fffffffffffffp52, W - 53};
}

// Writes a case of width exactly W whose scaled coordinates are extreme.
//
// Every coordinate is +-P, with P the largest magnitude of that width, except
// one coordinate, the anchor, which is +-1: it pins the lowest bit, so that
// the width is W and not 53. All differences of two such coordinates are 0,
// +-(P +- 1) or +-2P: the products, minors and lifts built from them are the
// largest the expressions can produce. `pattern` chooses the signs (bit i:
// coordinate i is negative) and, with `mix`, coordinates are sometimes
// replaced by other values (0, the smallest value of full width 2^(W-1), a
// random value) to reach the sign and cancellation patterns that pure
// corners do not.
//
// `low` is the binary exponent of the scaled unit: the doubles written are
// the scaled integers times 2^low.
void extreme_case(Rng& r, Pred pred, int W, int low, std::uint64_t pattern, int anchor, bool mix,
                  double* c) {
  const int count = NUM_COORDS[pred];
  const Magnitude P = largest_magnitude(W);
  // The double for a scaled magnitude, with the sign `pattern` gives
  // coordinate i.
  auto write = [&](int i, Magnitude magnitude) {
    const double x = std::ldexp(magnitude.significand, magnitude.exponent + low);
    c[i] = ((pattern >> i) & 1) ? -x : x;
  };
  for (int i = 0; i < count; i++) {
    Magnitude magnitude = P;
    if (mix) {
      switch (r.below(8)) {
        case 0: magnitude = {0.0, 0}; break;
        case 1: magnitude = {1.0, W - 1}; break;
        case 2: magnitude = {1.0, 0}; break;
        case 3: {  // a random value below 2^W with at most 53 significant bits
          const int bits = r.range(1, W < 53 ? W : 53);
          magnitude.significand = std::floor(std::fabs(r.with_exponent(bits - 1)));
          magnitude.exponent = r.range(0, W - bits);
          break;
        }
        default: break;
      }
    }
    write(i, magnitude);
  }
  // One coordinate of the largest magnitude and one odd one are needed for
  // the width to be exactly W. (For W <= 53, P itself is odd.)
  write((anchor + 1) % count, P);
  if (W > 53) write(anchor, {1.0, 0});
}

// Runs extreme cases of the given width through the stage. With
// 2^count * count sign-and-anchor patterns at most `budget`, all of them;
// otherwise `budget` random ones. The same number of mixed cases follows.
// `tiers_seen` counts the cases per deciding tier (tier_column()).
void test_extremes_at(Rng& r, Pred pred, int W, long budget, long* tiers_seen) {
  const int count = NUM_COORDS[pred];
  const std::uint64_t patterns = std::uint64_t{1} << count;
  const bool exhaustive = patterns * count <= static_cast<std::uint64_t>(budget);
  // Any position in the format that holds W bits: scaled unit 2^low with
  // low >= -1074 and low + W <= 1024. Mostly near the extremes, and, where
  // the width allows it, inside the floating-point domain of every predicate
  // (largest exponent low + W - 1 <= 201, and 5 * low >= -914), so that the
  // widths past the fast tiers see refusals as well as the wide tier.
  auto random_low = [&] {
    switch (r.below(4)) {
      case 0: return -1074;
      case 1: return 1024 - W;
      case 2: return r.range(-1074, 1024 - W);
      default:
        if (W > 200 && 202 - W >= -182) return r.range(-182, 202 - W);
        return 1024 - W >= 60 ? r.range(-60, 60) : r.range(-1074, 1024 - W);
    }
  };
  double c[MAX_COORDS];
  long cases = 0;
  auto run = [&](std::uint64_t pattern, int anchor, bool mix) {
    extreme_case(r, pred, W, random_low(), pattern, anchor, mix, c);
    CHECK(width_of(c, count) == W);
    // The tier: the one of the width; past the fast tiers, the wide tier or a
    // refusal, depending on where in the format the case is (check_case).
    const int tier = check_case(pred, c);
    CHECK(W > stage::max_width(stage::num_tiers) ? tier == 0 || tier == stage::wide_tier
                                                 : W <= stage::max_width(tier));
    tiers_seen[tier_column(tier)]++;
    cases++;
  };
  if (exhaustive) {
    for (int anchor = 0; anchor < count; anchor++) {
      for (std::uint64_t pattern = 0; pattern < patterns; pattern++) run(pattern, anchor, false);
    }
  } else {
    for (long i = 0; i < budget; i++) run(r.u64(), static_cast<int>(r.below(count)), false);
  }
  const long mixed = cases;
  for (long i = 0; i < mixed; i++) run(r.u64(), static_cast<int>(r.below(count)), true);
}

void test_extremes(long budget) {
  Rng r(0xb0a7);
  // Each fast tier's limit and one past it, the widths around 53 bits where
  // the shape of the largest magnitude changes, the limb boundaries, and the
  // narrowest cases.
  const int widths[] = {1,   2,   3,   52,  53,  54,  60,  61,  62,  63,  64,  65,
                        66,  124, 125, 126, 127, 128, 129, 188, 189, 190, 191, 192,
                        193, 252, 253, 254, 255, 256, 257, 316, 317};
  static_assert(stage::max_width(1) == 61 && stage::max_width(2) == 125 &&
                stage::max_width(3) == 189 && stage::max_width(4) == 253 &&
                stage::max_width(5) == 317 && stage::num_tiers == 5,
                "list the limits and limit + 1 above");
  // The wide tier: widths no fast tier holds, up to the largest width there
  // is (a coordinate with its lowest bit at 2^-1074 and one of magnitude
  // 2^1023). These cases are slow (the oracle too), so there are fewer.
  const int wide_widths[] = {318, 319, 320, 381, 509, 1100, 2047, 2048, 2097, 2098};
  static_assert(stage::max_finite_width == 2098, "list the largest width above");
  for (int p = 0; p < NUM_PREDS; p++) {
    long tiers_seen[stage::num_tiers + 2] = {};
    for (const int W : widths) test_extremes_at(r, static_cast<Pred>(p), W, budget, tiers_seen);
    // (At most 256 patterns per wide width, in the deep tier too: the checked
    // build of the wide tier is very slow.)
    const long wide_budget = budget / 32 < 256 ? budget / 32 : 256;
    for (const int W : wide_widths) {
      test_extremes_at(r, static_cast<Pred>(p), W, wide_budget, tiers_seen);
    }
    std::printf("worst-case magnitudes, %s: %ld cases in tier 1, %ld in tier 2, %ld in tier 3, "
                "%ld in tier 4, %ld in tier 5, %ld in the wide tier, %ld refused\n",
                PRED_NAME[p], tiers_seen[1], tiers_seen[2], tiers_seen[3], tiers_seen[4],
                tiers_seen[5], tiers_seen[stage::num_tiers + 1], tiers_seen[0]);
    // (Refusals need a case that is wide and inside the floating-point
    // domain, which only a part of the wide cases are.)
    for (int t = 0; t <= stage::num_tiers + 1; t++) CHECK(tiers_seen[t] > (t == 0 ? 40 : 100));
  }
}

// ---------------------------------------------------------------------------
// 5. Negative control: a tier on inputs wider than its limit
// ---------------------------------------------------------------------------

// Tier D's answer for a case, whatever its width (never done by the stage).
template <int D>
int forced_tier_sign(Pred pred, const double* c) {
  stage::limb bits[MAX_COORDS];
  stage::load_bits(c, NUM_COORDS[pred], bits);
  switch (pred) {
    case ORIENT2D: {
      stage::Scaled<6> s;
      return stage::scale<6>(bits, s) ? stage::Tier<D>::orient2d(s) : stage::refused;
    }
    case ORIENT3D: {
      stage::Scaled<12> s;
      return stage::scale<12>(bits, s) ? stage::Tier<D>::orient3d(s) : stage::refused;
    }
    case INCIRCLE: {
      stage::Scaled<8> s;
      return stage::scale<8>(bits, s) ? stage::Tier<D>::incircle(s) : stage::refused;
    }
    default: {
      stage::Scaled<15> s;
      return stage::scale<15>(bits, s) ? stage::Tier<D>::insphere(s) : stage::refused;
    }
  }
}

// The wide tier works with as many limbs as the width needs: every number of
// limbs from 1 to 33 is run here at the widest width it is used for and at
// the narrowest, on worst-case inputs, whatever the stage would do with such
// a width. (In the checked build every bound of the proof is verified on
// each of these calls, for that number of limbs.)
void test_wide_tier_at_every_size(Rng& r) {
  long cases = 0;
  for (int D = 1; D <= stage::wide_tier; D++) {
    const int narrowest = D == 1 ? 1 : stage::max_width(D - 1) + 1;
    const int widest = D < stage::wide_tier ? stage::max_width(D) : stage::max_finite_width;
    CHECK(stage::limbs_for_width(narrowest) == D && stage::limbs_for_width(widest) == D);
    for (const int W : {narrowest, widest}) {
      for (int p = 0; p < NUM_PREDS; p++) {
        const Pred pred = static_cast<Pred>(p);
        double c[MAX_COORDS];
        for (int i = 0; i < 4; i++) {
          extreme_case(r, pred, W, r.range(-1074, 1024 - W), r.u64(),
                       static_cast<int>(r.below(NUM_COORDS[p])), i % 2 == 1, c);
          CHECK(width_of(c, NUM_COORDS[p]) == W);
          CHECK(forced_tier_sign<stage::wide_tier>(pred, c) == oracle_sign(pred, c));
          cases++;
        }
      }
    }
  }
  std::printf("the wide tier with 1 to %d limbs per difference: %ld worst-case inputs\n",
              stage::wide_tier, cases);
}

// The worst-case inputs must have teeth: evaluated in a tier a few bits too
// narrow for them, they must produce wrong signs, and the checked build must
// see bounds violated. Otherwise the tests above could not notice a limb
// count or a tier limit that is too small.
template <int D>
void test_too_narrow_tier(Rng& r) {
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    // Five bits past the limit: not even the coordinates fit their limbs.
    const int W = stage::max_width(D) + 5;
    long wrong = 0, cases = 0;
#ifdef SHEWCHUK_INTEGER_STAGE_CHECKED
    const long violations_before = stage::check_violations;
#endif
    double c[MAX_COORDS];
    for (int i = 0; i < 4000; i++) {
      extreme_case(r, pred, W, r.range(-60, 60), r.u64(), static_cast<int>(r.below(NUM_COORDS[p])),
                   i % 2 == 1, c);
      if (forced_tier_sign<D>(pred, c) != oracle_sign(pred, c)) wrong++;
      cases++;
    }
    std::printf("negative control: tier %d forced on %s at width %d: %ld of %ld signs wrong", D,
                PRED_NAME[p], W, wrong, cases);
    CHECK(wrong > cases / 100);
#ifdef SHEWCHUK_INTEGER_STAGE_CHECKED
    const long violations = stage::check_violations - violations_before;
    std::printf(", %ld bound checks failed", violations);
    CHECK(violations >= cases);
    // Those failures were provoked on purpose.
    stage::check_violations = violations_before;
#endif
    std::printf("\n");
  }
}

// ---------------------------------------------------------------------------
// 6. Non-finite coordinates
// ---------------------------------------------------------------------------

void test_non_finite() {
  const Implementation& library = *find_implementation("library");
  const double inf = std::numeric_limits<double>::infinity();
  const double quiet = std::numeric_limits<double>::quiet_NaN();
  Rng r(0x9a9);
  long differences = 0;
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    for (int i = 0; i < 2000; i++) {
      double c[MAX_COORDS];
      // Half the time a degenerate case (all points equal but one), which the
      // filter cannot decide and the integer stage would otherwise take.
      const double shared = r.symmetric();
      for (int k = 0; k < NUM_COORDS[p]; k++) c[k] = (i % 2) ? shared : r.symmetric();
      const double bads[] = {inf, -inf, quiet, -quiet};
      c[r.below(NUM_COORDS[p])] = bads[r.below(4)];
      if (r.coin()) c[r.below(NUM_COORDS[p])] = bads[r.below(4)];
      // The stage itself says so, under every request ...
      for (const stage::Request request :
           {stage::Request::exact_or_floating_point, stage::Request::prefer_floating_point,
            stage::Request::always_exact}) {
        CHECK(stage_sign(pred, c, request) == stage::not_finite);
        CHECK(stage::last_width == -1 && stage::last_tier == 0);
      }
      // ... and the library returns NaN.
      const double value = library.call(pred, c);
      if (!std::isnan(value)) {
        if (differences < 5) {
          std::printf("  non-finite input: the library returns %a\n", value);
          print_case(pred, c);
        }
        differences++;
      }
    }
  }
  if (differences) std::printf("%ld non-finite inputs do not give NaN\n", differences);
  CHECK(differences == 0);
}

// Zero handling: all coordinates zero, either sign of zero, is sign 0 in
// tier 1; and replacing +0.0 by -0.0 never changes a result.
void test_zeros() {
  Rng r(0x2e70);
  for (int p = 0; p < NUM_PREDS; p++) {
    const Pred pred = static_cast<Pred>(p);
    double c[MAX_COORDS], negated[MAX_COORDS];
    for (int k = 0; k < NUM_COORDS[p]; k++) c[k] = (k % 2) ? -0.0 : 0.0;
    CHECK(stage_sign(pred, c) == 0);
    CHECK(stage::last_width == 0 && stage::last_tier == 1);
    for (int i = 0; i < 20000; i++) {
      for (int k = 0; k < NUM_COORDS[p]; k++) {
        c[k] = r.coin() ? 0.0 : static_cast<double>(r.range(-3, 3));
        negated[k] = c[k] == 0.0 ? -0.0 : c[k];
      }
      check_case(pred, c);
      CHECK(stage_sign(pred, c) == stage_sign(pred, negated));
    }
  }
}

// ---------------------------------------------------------------------------
// 7. Checked build: the proof's bounds
// ---------------------------------------------------------------------------

#ifdef SHEWCHUK_INTEGER_STAGE_CHECKED
const char* const KIND_NAME[stage::NUM_KINDS] = {
    "coordinate",    "difference",     "product of 2",  "2x2 minor",
    "lift (2D)",     "lift (3D)",      "product of 3",  "3x3 minor",
    "incircle term", "incircle det",   "insphere term", "insphere det"};

void report_bounds() {
  std::printf("\nLargest intermediates seen, in bits of magnitude, against the proof's bound "
              "at each tier's widest width\n%-14s", "intermediate");
  for (int t = 1; t <= stage::num_tiers; t++) std::printf("   tier %d: seen bound limbs", t);
  std::printf("\n");
  for (int kind = 0; kind < stage::NUM_KINDS; kind++) {
    std::printf("%-14s", KIND_NAME[kind]);
    for (int t = 1; t <= stage::num_tiers; t++) {
      const int bound = stage::magnitude_bits(static_cast<stage::Kind>(kind), stage::max_width(t));
      const int seen = stage::observed_bits[kind][t];
      std::printf("           %5d %5d %5d", seen, bound,
                  stage::limbs_of(static_cast<stage::Kind>(kind), t));
      // The bound holds (also checked on every operation) ...
      CHECK(seen <= bound);
      // ... and the worst-case inputs came within 5 bits of it (the bounds
      // are triangle inequalities, not attained maxima): they do exercise
      // the top limb of every intermediate.
      CHECK(seen >= bound - 5);
    }
    std::printf("\n");
  }
  // The wide tier: its limit (2109) is beyond the largest width a finite
  // input can have (2098), so its bounds are not approached as closely; the
  // widest inputs there are must still come within 5 bits of the bound for
  // THEIR width.
  std::printf("wide tier (%d limbs per difference), at the largest width %d: seen / bound\n",
              stage::wide_tier, stage::max_finite_width);
  for (int kind = 0; kind < stage::NUM_KINDS; kind++) {
    const int bound =
        stage::magnitude_bits(static_cast<stage::Kind>(kind), stage::max_finite_width);
    const int seen = stage::observed_bits[kind][stage::num_tiers + 1];
    std::printf("%-14s %6d / %6d\n", KIND_NAME[kind], seen, bound);
    CHECK(seen <= bound);
    CHECK(seen >= bound - 5);
    CHECK(bound + 1 <= 64 * stage::limbs_of(static_cast<stage::Kind>(kind), stage::wide_tier));
  }
}
#endif

}  // namespace

int main(int argc, char** argv) {
  const Tier* tier = find_tier(argc > 1 ? argv[1] : "quick");
  if (!tier) {
    std::printf("unknown tier '%s'\n", argv[1]);
    return 2;
  }
  const bool deep = std::strcmp(tier->name, "deep") == 0;
  initialize_implementations();

  test_limb_arithmetic();
  test_scaling();
  test_zeros();
  test_non_finite();
  test_extremes(deep ? 1L << 16 : 1L << 11);
  {
    Rng r(0x51de);
    test_wide_tier_at_every_size(r);
  }
  test_corpus(*tier);
#ifdef SHEWCHUK_INTEGER_STAGE_CHECKED
  std::printf("\nrun-time verification of the proof's bounds: %ld violations\n",
              stage::check_violations);
  CHECK(stage::check_violations == 0);
  report_bounds();
#endif
  Rng r(0x7ee7);
  test_too_narrow_tier<1>(r);
  test_too_narrow_tier<2>(r);
  test_too_narrow_tier<4>(r);
  test_too_narrow_tier<5>(r);
  CHECK(failure_count() == 0);
  return finish("integer stage");
}
