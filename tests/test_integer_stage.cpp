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
//      whose width (tests/width.h, independent code) is at most 189, in the
//      tier that width selects, and must see enough cases in every tier;
//   4. worst-case magnitudes: inputs built to make every intermediate as
//      large as possible, at widths exactly at each tier's limit and one past
//      it, anywhere in the exponent range of a double (subnormals included);
//   5. negative control: a tier evaluated on inputs wider than its limit IS
//      caught returning wrong signs (and, in the checked build, IS caught by
//      the bound checks);
//   6. non-finite coordinates are refused, and the library then returns what
//      its build without the integer stage returns;
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
              "tests/width.h must state the library's widest tier");

// The tier that must decide a case of width `width`; 0 if the stage must
// refuse it.
int expected_tier(int width) {
  if (width < 0) return 0;
  for (int tier = 1; tier <= stage::num_tiers; tier++) {
    if (width <= stage::max_width(tier)) return tier;
  }
  return 0;
}

// The integer stage's answer for a case in the suite's flat layout.
int stage_sign(Pred pred, const double* c) {
  switch (pred) {
    case ORIENT2D: return stage::orient2d(c, c + 2, c + 4);
    case ORIENT3D: return stage::orient3d(c, c + 3, c + 6, c + 9);
    case INCIRCLE: return stage::incircle(c, c + 2, c + 4, c + 6);
    default: return stage::insphere(c, c + 3, c + 6, c + 9, c + 12);
  }
}

void print_case(Pred pred, const double* c) {
  std::printf("    %s", PRED_NAME[pred]);
  for (int i = 0; i < NUM_COORDS[pred]; i++) std::printf(" %a", c[i]);
  std::printf("\n");
}

// Runs the stage on one case and checks everything that can be checked about
// a single call: the recorded width equals the independently computed one,
// the deciding tier is the one that width selects (or the call is refused
// exactly when no tier holds it), and the sign is the oracle's. Returns the
// tier (0 if refused).
int check_case(Pred pred, const double* c) {
  const int width = width_of(c, NUM_COORDS[pred]);
  const int tier = expected_tier(width);
  const int sign = stage_sign(pred, c);
  bool ok = stage::last_width == width && stage::last_tier == tier;
  if (tier == 0) {
    ok = ok && sign == stage::refused;
  } else {
    ok = ok && sign == oracle_sign(pred, c);
  }
  if (!ok) {
    static int reports = 0;
    if (reports++ < 5) {
      std::printf("  integer stage: returned %d in tier %d at width %d; independent width %d, "
                  "expected tier %d, exact sign %d\n",
                  sign, stage::last_tier, stage::last_width, width, tier,
                  tier ? oracle_sign(pred, c) : 0);
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

  // Every operand size the three tiers use (D, 2D, ..., 5D limbs) ...
  test_add_sub<1>(r, 2000), test_add_sub<2>(r, 2000), test_add_sub<3>(r, 2000);
  test_add_sub<4>(r, 2000), test_add_sub<5>(r, 2000), test_add_sub<6>(r, 2000);
  test_add_sub<8>(r, 1000), test_add_sub<9>(r, 1000), test_add_sub<10>(r, 1000);
  test_add_sub<12>(r, 1000), test_add_sub<15>(r, 1000);
  // ... and every product: D x D, D x 2D, 2D x 2D, 2D x 3D.
  test_mul<1, 1>(r, 4000), test_mul<1, 2>(r, 4000), test_mul<2, 2>(r, 4000);
  test_mul<2, 3>(r, 4000);
  test_mul<2, 4>(r, 2000), test_mul<4, 4>(r, 2000), test_mul<4, 6>(r, 2000);
  test_mul<3, 3>(r, 2000), test_mul<3, 6>(r, 2000), test_mul<6, 6>(r, 1000);
  test_mul<6, 9>(r, 1000);

  // The most negative value times itself: the largest product there is.
  stage::Int<2> most_negative = {{0, stage::limb{1} << 63}};
  CHECK(value_of(stage::mul(most_negative, most_negative)) == pow2(254));
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
  long decided[NUM_PREDS][stage::num_tiers + 1] = {};
  long total[NUM_PREDS] = {};
  for_each_case(tier.cases_per_generator, [&](const Generator& generator, const double* c) {
    decided[generator.pred][check_case(generator.pred, c)]++;
    total[generator.pred]++;
  });
  std::printf("\nForce-stage on the corpus (tier %s): cases decided by each tier of the integer "
              "stage\n%-9s %9s %9s %9s %9s %9s\n",
              tier.name, "predicate", "cases", "tier 1", "tier 2", "tier 3", "refused");
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s %9ld %9ld %9ld %9ld %9ld\n", PRED_NAME[p], total[p], decided[p][1],
                decided[p][2], decided[p][3], decided[p][0]);
    // Every tier, and the refusal, must see a real share of each predicate's
    // cases: at least 2%.
    for (int t = 0; t <= stage::num_tiers; t++) CHECK(decided[p][t] >= total[p] / 50);
  }
}

// ---------------------------------------------------------------------------
// 4. Worst-case magnitudes at the tier limits
// ---------------------------------------------------------------------------

// The largest magnitude a scaled coordinate of width W can have, as a
// double: 2^W - 1 if that has at most 53 bits, else (2^53 - 1) 2^(W-53).
double largest_magnitude(int W) {
  if (W <= 53) return std::ldexp(1.0, W) - 1.0;
  return std::ldexp(0x1.fffffffffffffp52, W - 53);
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
  const double P = largest_magnitude(W);
  for (int i = 0; i < count; i++) {
    double magnitude = P;
    if (mix) {
      switch (r.below(8)) {
        case 0: magnitude = 0.0; break;
        case 1: magnitude = std::ldexp(1.0, W - 1); break;
        case 2: magnitude = 1.0; break;
        case 3: {  // a random value below 2^W with at most 53 significant bits
          const int bits = r.range(1, W < 53 ? W : 53);
          magnitude = std::floor(std::fabs(r.with_exponent(bits - 1)));
          magnitude = std::ldexp(magnitude, r.range(0, W - bits));
          break;
        }
        default: break;
      }
    }
    c[i] = ((pattern >> i) & 1) ? -magnitude : magnitude;
  }
  // One coordinate of the largest magnitude and one odd one are needed for
  // the width to be exactly W. (For W <= 53, P itself is odd.)
  c[(anchor + 1) % count] = ((pattern >> ((anchor + 1) % count)) & 1) ? -P : P;
  if (W > 53) c[anchor] = ((pattern >> anchor) & 1) ? -1.0 : 1.0;
  for (int i = 0; i < count; i++) c[i] = std::ldexp(c[i], low);
}

// Runs extreme cases of the given width through the stage. With
// 2^count * count sign-and-anchor patterns at most `budget`, all of them;
// otherwise `budget` random ones. The same number of mixed cases follows.
void test_extremes_at(Rng& r, Pred pred, int W, long budget) {
  const int count = NUM_COORDS[pred];
  const std::uint64_t patterns = std::uint64_t{1} << count;
  const bool exhaustive = patterns * count <= static_cast<std::uint64_t>(budget);
  const int tier = expected_tier(W);
  // Any position in the format that holds W bits: scaled unit 2^low with
  // low >= -1074 and low + W <= 1024. Mostly near the extremes.
  auto random_low = [&] {
    switch (r.below(4)) {
      case 0: return -1074;
      case 1: return 1024 - W;
      case 2: return r.range(-1074, 1024 - W);
      default: return r.range(-60, 60);
    }
  };
  double c[MAX_COORDS];
  long cases = 0;
  auto run = [&](std::uint64_t pattern, int anchor, bool mix) {
    extreme_case(r, pred, W, random_low(), pattern, anchor, mix, c);
    CHECK(width_of(c, count) == W);
    CHECK(check_case(pred, c) == tier);
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
  // Each tier's limit and one past it (the next tier, or the refusal), the
  // widths around 53 bits where the shape of the largest magnitude changes,
  // the limb boundaries, and the narrowest cases.
  const int widths[] = {1,  2,  3,   52,  53,  54,  60,  61,  62,  63, 64,
                        65, 66, 124, 125, 126, 127, 128, 129, 188, 189, 190, 191, 192, 253};
  static_assert(stage::max_width(1) == 61 && stage::max_width(2) == 125 &&
                stage::max_width(3) == 189, "list the limits and limit + 1 above");
  for (int p = 0; p < NUM_PREDS; p++) {
    for (const int W : widths) test_extremes_at(r, static_cast<Pred>(p), W, budget);
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
  const Implementation& expansion_only = *find_implementation("library_expansion_only");
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
      // The stage itself refuses ...
      CHECK(stage_sign(pred, c) == stage::refused);
      CHECK(stage::last_width == -1 && stage::last_tier == 0);
      // ... and the library returns what it returns without the stage: the
      // same value, or a NaN where that returns a NaN. (Which of several NaN
      // operands an instruction propagates, sign and payload, is up to the
      // compiler's operand order, so NaN bits may differ between two builds
      // of the same source.)
      const double with_stage = library.call(pred, c);
      const double without_stage = expansion_only.call(pred, c);
      const bool same = std::memcmp(&with_stage, &without_stage, sizeof with_stage) == 0 ||
                        (std::isnan(with_stage) && std::isnan(without_stage));
      if (!same) {
        if (differences < 5) {
          std::printf("  non-finite input: %a with the integer stage, %a without\n", with_stage,
                      without_stage);
          print_case(pred, c);
        }
        differences++;
      }
    }
  }
  if (differences) {
    std::printf("%ld non-finite inputs give another value with the integer stage\n", differences);
  }
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
  CHECK(failure_count() == 0);
  return finish("integer stage");
}
