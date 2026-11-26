// The guard of the first filter: when may its result be trusted on inputs
// outside the exponent range the 1996 analysis assumes?
//
// The error analysis of the 1996 predicates assumes that no operation
// overflows or underflows. Call a computed value CLEAN if it is bit for bit
// what the same operations give with an unbounded exponent range (and it is
// finite). On clean values the 1996 analysis holds as it is. A value stops
// being clean only where a product is rounded in the subnormal range; what
// follows shows that a value that is not clean is SMALL, with an explicit
// bound, and the guard is: the error bound of the filter exceeds that bound.
// Then the determinant estimate, the permanent and the error bound the filter
// compares are all clean, and the comparison is the one the 1996 analysis is
// about. The derivation is docs/proofs/input-domain.md; this header is that
// derivation as code, so that the constants cannot drift from it.
//
// Nothing here performs floating-point arithmetic at run time: the guard
// reads bit patterns and compares integers. That is on purpose. The first
// filter is limited by the processor's floating-point units, and one more
// floating-point comparison costs it 2% (measured); a limited number of
// integer operations run beside it for free (docs/benchmarking.md, "What
// limits the first filters"). For the same reason nothing here depends on
// the floating-point flags of the translation unit.
//
// Private header: not installed, not part of the public interface.
#pragma once

#include <climits>
#include <cstdint>
#include <cstring>

#if defined(_MSC_VER) && !defined(__clang__)
#define SHEWCHUK_GUARD_INLINE __forceinline
#elif defined(__GNUC__)
#define SHEWCHUK_GUARD_INLINE inline __attribute__((always_inline))
#else
#define SHEWCHUK_GUARD_INLINE inline
#endif

// Code generation only. Makes the compiler forget where a pointer came from,
// so that it loads a coordinate's bits from memory into an integer register
// instead of moving them over from the floating-point register that already
// holds the coordinate; such a move occupies a floating-point unit, the load
// does not. The pointer's value is unchanged.
#if defined(__GNUC__) || defined(__clang__)
#define SHEWCHUK_GUARD_FORGET_ORIGIN(pointer) __asm__("" : "+r"(pointer))
#else
#define SHEWCHUK_GUARD_FORGET_ORIGIN(pointer) ((void)0)
#endif
// Code generation only, for the same reason: keeps a 64-bit value in an
// integer register, so that the compiler does not compare several of them at
// once in vector registers, which are the floating-point units again.
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__aarch64__) || defined(__x86_64__))
#define SHEWCHUK_GUARD_KEEP_IN_INTEGER_REGISTER(value) __asm__("" : "+r"(value))
#else
#define SHEWCHUK_GUARD_KEEP_IN_INTEGER_REGISTER(value) ((void)0)
#endif
// Code generation only, for the same reason: makes a double go through memory,
// so that its bits reach an integer register by a store and a load instead of
// a move between the register files.
#if defined(__GNUC__) || defined(__clang__)
#define SHEWCHUK_GUARD_THROUGH_MEMORY(value) __asm__("" : "+m"(value))
#else
#define SHEWCHUK_GUARD_THROUGH_MEMORY(value) ((void)0)
#endif

namespace shewchuk::detail {
namespace {
namespace guard {

static_assert(CHAR_BIT == 8 && sizeof(double) == 8, "binary64 doubles in 8 bytes are assumed");

using bits64 = std::uint64_t;

// The bit pattern of a double without its sign bit orders nonnegative doubles
// (and +infinity, and then the NaNs) like the unsigned integer it is.
inline constexpr bits64 magnitude_mask = 0x7fffffffffffffffull;
inline constexpr bits64 infinity_bits = 0x7ff0000000000000ull;
// The bit pattern of 2^exponent, a normal number.
constexpr bits64 power_of_two_bits(int exponent) noexcept {
  return static_cast<bits64>(exponent + 1023) << 52;
}
static_assert(power_of_two_bits(0) == 0x3ff0000000000000ull);
static_assert(power_of_two_bits(1024) == infinity_bits);

SHEWCHUK_GUARD_INLINE bits64 bits_of(double x) noexcept {
  bits64 bits;
  std::memcpy(&bits, &x, sizeof bits);
  return bits;
}
// The bits of a value of the filter, by way of memory.
SHEWCHUK_GUARD_INLINE bits64 bits_through_memory(double x) noexcept {
  double slot = x;
  SHEWCHUK_GUARD_THROUGH_MEMORY(slot);
  return bits_of(slot);
}
// The bits of p[index], read from memory.
SHEWCHUK_GUARD_INLINE bits64 bits_at(const double* p, int index) noexcept {
  bits64 bits;
  std::memcpy(&bits, p + index, sizeof bits);
  SHEWCHUK_GUARD_KEEP_IN_INTEGER_REGISTER(bits);
  return bits;
}

// ---------------------------------------------------------------------------
// The derivation, as code (docs/proofs/input-domain.md, sections 2 and 3).
//
// D is a power of two, D >= 1, that bounds the magnitude of certain
// coordinate differences (the "bounded" ones). For every value v the first
// filter computes, with v' the value the same operations give with an
// unbounded exponent range:
//   * v is clean (v == v'), or |v| and |v'| are both at most
//     2^control2 * D^controlD;
//   * |v| and |v'| are both at most 2^size2 * D^sizeD, unless size2 is
//     `no_bound`.
// The rules below are the lemmas of the proof.
// ---------------------------------------------------------------------------
struct Value {
  bool clean;
  int control2, controlD;
  int size2, sizeD;
};
// A product of two clean values is clean unless its magnitude is at most
// 2^tiny_product (lemma P).
inline constexpr int tiny_product = -1022;
// A clean value absorbs a value that is 2^-55 of its magnitude or less: the
// sum is the clean value, in both arithmetics. So a sum that is not clean has
// at most 2^55 + 1 <= 2^absorb times the magnitude of its unclean part
// (lemma S).
inline constexpr int absorb = 56;
inline constexpr int no_bound = 1 << 20;
inline constexpr int never = INT_MIN / 2;  // the control of a clean value: nothing to bound

constexpr int larger(int a, int b) noexcept { return a > b ? a : b; }

// A difference of two coordinates. It is always clean (lemma D).
constexpr Value bounded_difference() noexcept { return {true, never, 0, 0, 1}; }
constexpr Value free_difference() noexcept { return {true, never, 0, no_bound, 0}; }

// x + y or x - y (lemma S).
constexpr Value sum(Value x, Value y) noexcept {
  Value r{};
  r.clean = x.clean && y.clean;
  r.control2 = never;
  if (!x.clean) r.control2 = x.control2, r.controlD = x.controlD;
  if (!y.clean) r.control2 = larger(r.control2, y.control2), r.controlD = larger(r.controlD, y.controlD);
  if (!r.clean) r.control2 += absorb;
  const bool bounded = x.size2 < no_bound && y.size2 < no_bound;
  r.size2 = bounded ? larger(x.size2, y.size2) + 1 : no_bound;
  r.sizeD = bounded ? larger(x.sizeD, y.sizeD) : 0;
  return r;
}
// x * y (lemma P). Never called clean: a product of clean values can be
// rounded in the subnormal range.
constexpr Value product(Value x, Value y) noexcept {
  Value r{};
  r.clean = false;
  r.control2 = tiny_product, r.controlD = 0;
  // An unclean factor times a bound on the other factor.
  if (!x.clean) {
    r.control2 = y.size2 < no_bound ? larger(r.control2, x.control2 + y.size2) : no_bound;
    r.controlD = larger(r.controlD, x.controlD + y.sizeD);
  }
  if (!y.clean && r.control2 < no_bound) {
    r.control2 = x.size2 < no_bound ? larger(r.control2, y.control2 + x.size2) : no_bound;
    r.controlD = larger(r.controlD, y.controlD + x.sizeD);
  }
  const bool bounded = x.size2 < no_bound && y.size2 < no_bound;
  r.size2 = bounded ? x.size2 + y.size2 : no_bound;
  r.sizeD = bounded ? x.sizeD + y.sizeD : 0;
  return r;
}
// |x|: the same bounds.
constexpr Value absolute(Value x) noexcept { return x; }

// What the first filter of one predicate compares.
struct Filter {
  Value det, permanent;
};

// orient2d: detleft - detright, and |detleft| + |detright|. No difference
// needs a bound.
constexpr Filter orient2d_filter() noexcept {
  const Value d = free_difference();
  const Value detleft = product(d, d), detright = product(d, d);
  return {sum(detleft, detright), sum(absolute(detleft), absolute(detright))};
}

// orient3d: only the z differences multiply a value that may be unclean, so
// only they need a bound.
constexpr Filter orient3d_filter() noexcept {
  const Value xy = free_difference(), z = bounded_difference();
  const Value p = product(xy, xy);  // bdx * cdy and its five siblings
  const Value minor = sum(p, p), plus = sum(absolute(p), absolute(p));
  const Value term = product(z, minor), plus_term = product(plus, absolute(z));
  return {sum(sum(term, term), term), sum(sum(plus_term, plus_term), plus_term)};
}

// incircle: every difference is bounded.
constexpr Filter incircle_filter() noexcept {
  const Value d = bounded_difference();
  const Value p = product(d, d);
  const Value minor = sum(p, p), plus = sum(absolute(p), absolute(p));
  const Value lift = sum(product(d, d), product(d, d));
  const Value term = product(lift, minor), plus_term = product(plus, lift);
  return {sum(sum(term, term), term), sum(sum(plus_term, plus_term), plus_term)};
}

// insphere: every difference is bounded.
constexpr Filter insphere_filter() noexcept {
  const Value d = bounded_difference();
  const Value p = product(d, d);       // aex * bey and its eleven siblings
  const Value minor2 = sum(p, p);      // ab, bc, cd, da, ac, bd
  const Value zm = product(d, minor2); // aez * bc ...
  const Value minor3 = sum(sum(zm, zm), zm);  // abc, bcd, cda, dab
  const Value lift = sum(sum(product(d, d), product(d, d)), product(d, d));
  const Value term = product(lift, minor3);
  const Value det = sum(sum(term, term), sum(term, term));
  const Value plus2 = sum(absolute(p), absolute(p));
  const Value plus3_term = product(plus2, absolute(d));
  const Value plus3 = sum(sum(plus3_term, plus3_term), plus3_term);
  const Value plus_term = product(plus3, lift);
  const Value permanent = sum(sum(sum(plus_term, plus_term), plus_term), plus_term);
  return {det, permanent};
}

// The guard of one predicate: the filter's result is used only if
//   errbound >= 2^floor_exponent * D^power.
// That is sound (theorem 1 of the proof) if the bound of every unclean det
// and permanent, 2^control2 * D^controlD, is below it for every D >= 1, and
// if the product errbound = constant * permanent is itself above the
// subnormal range.
struct Guard {
  int floor_exponent;
  int power;  // 1, 2 or 3
};
constexpr bool sound(Guard g, Filter f) noexcept {
  return f.det.control2 < g.floor_exponent && f.permanent.control2 < g.floor_exponent &&
         f.det.controlD <= g.power && f.permanent.controlD <= g.power &&
         tiny_product < g.floor_exponent && g.power >= 1 && g.power <= 3;
}
// The largest floor the derivation needs, for the static_asserts that show
// where the proof stops.
constexpr int needed_floor(Filter f) noexcept {
  return larger(larger(f.det.control2, f.permanent.control2), tiny_product) + 1;
}

inline constexpr Guard orient3d_guard = {-853, 1};
inline constexpr Guard incircle_guard = {-852, 2};
inline constexpr Guard insphere_guard = {-683, 3};
static_assert(sound(orient3d_guard, orient3d_filter()));
static_assert(sound(incircle_guard, incircle_filter()));
static_assert(sound(insphere_guard, insphere_filter()));
// The floors are the smallest the derivation allows: one less and the bound
// of an unclean permanent is not below the guard's.
static_assert(needed_floor(orient3d_filter()) == orient3d_guard.floor_exponent);
static_assert(needed_floor(incircle_filter()) == incircle_guard.floor_exponent);
static_assert(needed_floor(insphere_filter()) == insphere_guard.floor_exponent);
// The power of D: 1 for orient3d, 2 for incircle, 3 for insphere.
static_assert(orient3d_filter().permanent.controlD == 1 && orient3d_filter().det.controlD == 1);
static_assert(incircle_filter().permanent.controlD == 2 && incircle_filter().det.controlD == 2);
static_assert(insphere_filter().permanent.controlD == 3 && insphere_filter().det.controlD == 3);

// orient2d has no bounded difference (D does not occur), so its guard is a
// constant window on the error bound:
//   2^orient2d_floor_exponent <= errbound < 2^orient2d_ceiling_exponent.
// The floor is far above what the first filter needs, because the same window
// also guards stages B and C of orient2d (proof, section 4), which need
//   ccwerrboundC * detsum > 2^(3 absorb + tiny_product), and detsum is at
// least 2^51 errbound, ccwerrboundC at least 2^-104.
inline constexpr int orient2d_floor_exponent = -600;
inline constexpr int orient2d_ceiling_exponent = 950;
static_assert(orient2d_filter().det.control2 < orient2d_floor_exponent &&
              orient2d_filter().permanent.control2 < orient2d_floor_exponent &&
              orient2d_filter().det.controlD == 0 && orient2d_filter().permanent.controlD == 0);
static_assert(orient2d_floor_exponent + 51 - 104 > 3 * absorb + tiny_product + absorb);
// Stage B of orient2d multiplies by 2^27 + 1 and adds a few products of the
// size of detsum < 2^(ceiling + 53): nothing reaches 2^1024.
static_assert(orient2d_ceiling_exponent + 53 + 3 < 1024);

// ---------------------------------------------------------------------------
// The guard at run time.
// ---------------------------------------------------------------------------

// floor(q / 3) without a division, for every q an exponent field can give
// (q < 2^12).
constexpr bits64 third(bits64 q) noexcept { return (q * 43691) >> 17; }
constexpr bool third_is_exact() noexcept {
  for (bits64 q = 0; q < (bits64{1} << 12); q++) {
    if (third(q) != q / 3) return false;
  }
  return true;
}
static_assert(third_is_exact());

// The 1996 test of the first filter of orient3d, incircle and insphere,
// |det| > errbound, on bit patterns, and true only if det is finite. If it is
// true, errbound is finite and not negative as well: with a the pattern of
// det without its sign and e the pattern of errbound, e < a < infinity_bits,
// and a negative errbound (there is none), a NaN and +infinity all have
// patterns of at least infinity_bits.
SHEWCHUK_GUARD_INLINE bool filter_certifies(double det, double errbound) noexcept {
  const bits64 a = bits_of(det) & magnitude_mask;
  return (a > bits_of(errbound)) & (a < infinity_bits);
}

// q for a finite errbound >= 2^floor_exponent: the exponent field of errbound
// minus that of 2^floor_exponent, so that errbound >= 2^(floor_exponent + q),
// then floor(q / power): the exponent j of the largest D = 2^j with
// 2^floor_exponent * D^power <= errbound that this test can show.
SHEWCHUK_GUARD_INLINE bits64 difference_bound_exponent(Guard g, bits64 e) noexcept {
  const bits64 q = (e - power_of_two_bits(g.floor_exponent)) >> 52;
  return g.power == 3 ? third(q) : g.power == 2 ? q >> 1 : q;
}

// The guard, given that filter_certifies() holds: true only if
//   errbound >= 2^floor_exponent * D^power
// for a power of two D >= 1 that bounds every rounded difference of two of
// the given coordinates.
//
// `coordinates` are the bit patterns of the coordinates that take part in a
// bounded difference. With j as above (j >= 0), the test is that every
// coordinate's exponent field is at most 1021 + j: then every coordinate is
// below 2^(j - 1) in magnitude, every rounded difference of two of them is at
// most D = 2^j >= 1, and 2^floor_exponent * D^power <= errbound. "Exponent
// field at most F" is "bit pattern without the sign below (F + 1) * 2^52";
// `limit` is that number, or less.
template <class... Coordinates>
SHEWCHUK_GUARD_INLINE bool holds(Guard g, double errbound, Coordinates... coordinates) noexcept {
  const bits64 e = bits_of(errbound);
  const bits64 floor_bits = power_of_two_bits(g.floor_exponent);
  bits64 limit;
  if (g.power == 3) {
    limit = (1022 + difference_bound_exponent(g, e)) << 52;
  } else {
    // (1021 + floor(q / power)) * 2^52 plus what the shift leaves of the
    // fraction field of errbound, which is less than 2^52: at most
    // (1022 + floor(q / power)) * 2^52, i.e. the same test or a slightly
    // stricter one, for one operation. Used only if e >= floor_bits (tested
    // below); then nothing wraps, because e < 2^63.
    limit = power_of_two_bits(-2) + ((e - floor_bits) >> (g.power - 1));
  }
  return (e >= floor_bits) & (... & ((coordinates & magnitude_mask) < limit));
}

// The same guard with D taken from sums of squares instead of coordinates.
// `squares` are values of the filter, each a (rounded) sum of rounded squares
// of differences, such that the rounded square of every bounded difference
// is a term of one of them (the lifted coordinates of insphere, or a sum of
// those). The test is that every one is below 2^(2j - 1), j as above. Then
// every bounded difference x is at most D = 2^j in magnitude: fl(x * x) is
// at most the sum it is a term of (the other terms are not negative and
// rounding is monotonic), so fl(x * x) < 2^(2j - 1); either x * x < 2^-1022,
// and |x| < 2^-511 <= D, or the product was rounded with a relative error of
// at most 2^-53, and x * x < 2^(2j - 1) / (1 - 2^-53) < 2^(2j).
//
// A sum that overflowed to +infinity (bit pattern infinity_bits) passes only
// if 2^(2j - 1) is beyond the largest double, i.e. j >= 513; then D = 2^j
// still bounds every x, because filter_certifies() has shown that det is
// finite, hence that every fl(x * x) is, hence |x| < 2^512. A NaN among the
// squares would make det a NaN.
template <class... Squares>
SHEWCHUK_GUARD_INLINE bool holds_for_squares(Guard g, double errbound,
                                             Squares... squares) noexcept {
  const bits64 e = bits_of(errbound);
  const bits64 floor_bits = power_of_two_bits(g.floor_exponent);
  // The bit pattern of 2^(2j - 1). If e >= floor_bits, j is at most 2046 and
  // nothing wraps; if not, the result is not used.
  const bits64 limit = (1022 + 2 * difference_bound_exponent(g, e)) << 52;
  return (e >= floor_bits) & (... & (bits_through_memory(squares) < limit));
}

}  // namespace guard
}  // namespace
}  // namespace shewchuk::detail
