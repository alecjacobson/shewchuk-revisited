// The exact integer stage: the sign of each predicate's determinant in
// fixed-width integer arithmetic.
//
// Every finite double is an integer times a power of two. The stage scales
// all coordinates of a call by one common power of two so that they become
// integers, measures how many bits the largest of them needs (the width W),
// and, if W is small enough for one of its tiers, evaluates the determinant
// exactly in two's-complement integers made of 64-bit limbs and returns its
// sign. A positive common scale factor does not change the sign of a
// homogeneous polynomial, so that sign is the predicate's.
//
// There is no rounding anywhere, hence no error bound, no dependence on the
// floating-point flags, the rounding mode or flush-to-zero, and no exponent
// domain: the answer is exact for all finite inputs the stage accepts,
// subnormals included. The only ways to be wrong are an intermediate that
// does not fit its limbs, a wrong W, or a shift out of range. The proof that
// none of them happens is docs/proofs/integer-stage.md; the static_asserts
// below tie the limb counts to that derivation, and the checked build
// (SHEWCHUK_INTEGER_STAGE_CHECKED, test only) verifies every bound of the
// proof at run time on every operation.
//
// The stage answers for EVERY finite input for which the caller may not rely
// on floating-point arithmetic. Widths up to 317 go to one of five fast
// tiers. A wider input is either inside the domain in which the
// floating-point expansion stages of 1996 are valid
// (in_floating_point_domain() below, docs/proofs/input-domain.md): then the
// stage returns `refused` and the caller runs those stages, as before this
// stage existed; or it is not, and the wide tier, whose limbs hold any finite
// double, decides it. A coordinate that is not finite gives `not_finite`.
//
// All limb arithmetic is unsigned (no signed overflow is possible), and the
// only floating-point operation in this file is copying the bits of a double.
// Nothing here depends on how the translation unit is compiled, which is why
// this code may be a header while the floating-point arithmetic of
// predicates.cpp must stay in that file.
//
// Everything here has internal linkage: the header may be compiled into
// several translation units with different configuration macros (the test
// suite does that) without their definitions colliding. Nothing here throws,
// and every function that is not forced inline is noexcept, so that no
// exception-handling tables (and, with them, no extra symbols) end up in an
// unoptimized library.
//
// Configuration macros (the shipped library defines none of them):
//   SHEWCHUK_INTEGER_STAGE_PORTABLE    use no compiler extension (unsigned
//                                      __int128, builtins, intrinsics): force
//                                      the portable 64x64->128 multiplication
//                                      and bit counting, which compilers
//                                      without those extensions get
//   SHEWCHUK_INTEGER_STAGE_CHECKED     verify the proof's bounds at run time
//                                      and count violations (test only)
//   SHEWCHUK_INTEGER_STAGE_TEST_HOOKS  record the width and tier of each call
//
// Private header: not installed, not part of the public interface.
#pragma once

#include <climits>
#include <cstdint>
#include <cstring>

#if !defined(SHEWCHUK_INTEGER_STAGE_PORTABLE) && defined(_MSC_VER) && !defined(__clang__) && \
    (defined(_M_X64) || defined(_M_ARM64))
#include <intrin.h>
#define SHEWCHUK_INTEGER_STAGE_MSVC_INTRINSICS 1
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define SHEWCHUK_INTEGER_STAGE_INLINE __forceinline
#define SHEWCHUK_INTEGER_STAGE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define SHEWCHUK_INTEGER_STAGE_INLINE inline __attribute__((always_inline))
#define SHEWCHUK_INTEGER_STAGE_NOINLINE __attribute__((noinline))
#else
#define SHEWCHUK_INTEGER_STAGE_INLINE inline
#define SHEWCHUK_INTEGER_STAGE_NOINLINE
#endif

namespace shewchuk::detail {
namespace {
namespace integer_stage {

static_assert(CHAR_BIT == 8 && sizeof(double) == 8, "binary64 doubles in 8 bytes are assumed");

using limb = std::uint64_t;

// What a predicate of this stage returns when it does not decide; otherwise
// it returns the sign, -1, 0 or +1.
//   refused     the coordinates are finite and inside the domain of the
//               floating-point stages: the caller runs those
//   not_finite  a coordinate is an infinity or a NaN
inline constexpr int refused = 2;
inline constexpr int not_finite = 3;

// What the caller asks for (see SHEWCHUK_INTEGER_STAGE_DECIDE).
enum class Request {
  // Decide every input the fast tiers hold. Refuse a wider one if it is in
  // the floating-point domain; decide it in the wide tier if it is not.
  exact_or_floating_point,
  // Refuse every input in the floating-point domain, whatever its width;
  // decide every other one.
  prefer_floating_point,
  // Never refuse a finite input.
  always_exact,
};

// The tiers. Tier D stores a coordinate difference in D limbs and accepts
// widths up to max_width(D) = 64 D - 3. The fast tiers are D = 1 ... 5; the
// wide tier is D = wide_tier and holds every finite input.
inline constexpr int num_tiers = 5;
constexpr int max_width(int tier) noexcept { return 64 * tier - 3; }
inline constexpr int wide_tier = 33;
// The largest width there is: the highest set bit of a finite double has
// exponent at most 1023 and the lowest at least -1074 (see scale()).
inline constexpr int max_finite_width = 1023 + 1074 + 1;
static_assert(max_width(wide_tier) >= max_finite_width,
              "the wide tier must hold every finite input");
static_assert(max_width(wide_tier - 1) < max_finite_width,
              "the wide tier is the narrowest tier that holds every finite input");

// ---------------------------------------------------------------------------
// The domain of the floating-point stages (docs/proofs/input-domain.md).
// With L the binary exponent of the lowest set bit of any coordinate and H
// the binary exponent of the largest magnitude, no operation of the stages A
// to D of a predicate of degree d overflows or underflows if
//   H <= floating_point_max_exponent   and   d * L >= floating_point_min_grid.
// The floating-point stages B, C and D are used only on such inputs (for
// orient2d: stage D; its stages B and C have their own guard). Every other
// input is decided by the integer stage.
// ---------------------------------------------------------------------------
inline constexpr int floating_point_max_exponent = 201;
// -1074 is the exponent of the smallest subnormal number; 160 covers the
// lowest set bit of every error-bound constant (checked in predicates.cpp).
inline constexpr int error_constant_grid = 160;
inline constexpr int floating_point_min_grid = -1074 + error_constant_grid;
// `low` and `high` as scale() computes them, biased by 1075: the lowest set
// bit of any coordinate is 2^(low - 1075), every magnitude is below
// 2^(high - 1075). Not meaningful if all coordinates are zero.
constexpr bool in_floating_point_domain(int degree, int low, int high) noexcept {
  return high - 1076 <= floating_point_max_exponent &&
         degree * (low - 1075) >= floating_point_min_grid;
}

// ---------------------------------------------------------------------------
// The derivation, as code (docs/proofs/integer-stage.md, section 3).
//
// For scaled coordinates of width W, i.e. |x| <= 2^W - 1, every value the
// stage computes has magnitude below 2^k with k given here per kind of
// intermediate. A two's-complement integer of k + 1 bits holds it.
// ---------------------------------------------------------------------------
enum Kind {
  COORDINATE,     // a scaled coordinate
  DIFFERENCE,     // coordinate - coordinate
  PRODUCT2,       // difference * difference
  MINOR2,         // PRODUCT2 - PRODUCT2 (the orient2d determinant is one)
  LIFT2,          // PRODUCT2 + PRODUCT2 (incircle)
  LIFT3,          // PRODUCT2 + PRODUCT2 + PRODUCT2 (insphere) and its partial sum
  PRODUCT3,       // difference * MINOR2
  MINOR3,         // sum of three PRODUCT3 (the orient3d determinant is one)
  INCIRCLE_TERM,  // LIFT2 * MINOR2
  INCIRCLE_DET,   // sum of three INCIRCLE_TERM and its partial sum
  INSPHERE_TERM,  // LIFT3 * MINOR3
  INSPHERE_DET,   // sum of four INSPHERE_TERM and its partial sums
  NUM_KINDS
};

// k such that |value| < 2^k for every value of that kind at width W.
constexpr int magnitude_bits(Kind kind, int W) noexcept {
  switch (kind) {
    case COORDINATE: return W;
    case DIFFERENCE: return W + 1;
    case PRODUCT2: return 2 * W + 2;
    case MINOR2: return 2 * W + 3;
    case LIFT2: return 2 * W + 3;
    case LIFT3: return 2 * W + 4;
    case PRODUCT3: return 3 * W + 4;
    case MINOR3: return 3 * W + 6;
    case INCIRCLE_TERM: return 4 * W + 6;
    case INCIRCLE_DET: return 4 * W + 8;
    case INSPHERE_TERM: return 5 * W + 10;
    case INSPHERE_DET: return 5 * W + 12;
    default: return INT_MAX / 2;
  }
}

// Limbs tier D gives to a value of that kind: D per factor of the polynomial.
constexpr int limbs_of(Kind kind, int D) noexcept {
  switch (kind) {
    case COORDINATE:
    case DIFFERENCE: return D;
    case PRODUCT2:
    case MINOR2:
    case LIFT2:
    case LIFT3: return 2 * D;
    case PRODUCT3:
    case MINOR3: return 3 * D;
    case INCIRCLE_TERM:
    case INCIRCLE_DET: return 4 * D;
    case INSPHERE_TERM:
    case INSPHERE_DET: return 5 * D;
    default: return 0;
  }
}

// Every kind of intermediate fits its limbs, sign bit included, at the
// widest width its tier accepts ...
constexpr bool tier_holds_every_intermediate(int D, int W) noexcept {
  for (int kind = 0; kind < NUM_KINDS; kind++) {
    const Kind k = static_cast<Kind>(kind);
    if (magnitude_bits(k, W) + 1 > 64 * limbs_of(k, D)) return false;
  }
  return true;
}
static_assert(tier_holds_every_intermediate(1, max_width(1)));
static_assert(tier_holds_every_intermediate(2, max_width(2)));
static_assert(tier_holds_every_intermediate(3, max_width(3)));
static_assert(tier_holds_every_intermediate(4, max_width(4)));
static_assert(tier_holds_every_intermediate(5, max_width(5)));
// ... and the tier limits cannot be raised with these bounds: one bit more
// and the bound of the sum of three squares, among others, needs more than
// its 2 D limbs. (The bounds are upper bounds, so this says where the proof
// stops, not that width 64 D - 2 would overflow.)
static_assert(!tier_holds_every_intermediate(1, max_width(1) + 1));
static_assert(!tier_holds_every_intermediate(2, max_width(2) + 1));
static_assert(!tier_holds_every_intermediate(3, max_width(3) + 1));
static_assert(!tier_holds_every_intermediate(4, max_width(4) + 1));
static_assert(!tier_holds_every_intermediate(5, max_width(5) + 1));
static_assert(max_width(1) == 61 && max_width(2) == 125 && max_width(3) == 189 &&
              max_width(4) == 253 && max_width(5) == 317);
// The derivation is the same for every D (docs/proofs/integer-stage.md,
// section 3); the wide tier is one more instance of it.
static_assert(tier_holds_every_intermediate(wide_tier, max_width(wide_tier)));
static_assert(!tier_holds_every_intermediate(wide_tier, max_width(wide_tier) + 1));

// ---------------------------------------------------------------------------
// Test-only instrumentation
// ---------------------------------------------------------------------------
#ifdef SHEWCHUK_INTEGER_STAGE_TEST_HOOKS
// Width of the most recent call (-1 if a coordinate was not finite) and the
// tier that decided it (0 if the stage did not decide, wide_tier for the wide
// tier). Not thread-safe.
inline int last_width = -1;
inline int last_tier = 0;
#define SHEWCHUK_INTEGER_STAGE_RECORD(width, tier) (last_width = (width), last_tier = (tier))
#else
#define SHEWCHUK_INTEGER_STAGE_RECORD(width, tier) ((void)0)
#endif

#ifdef SHEWCHUK_INTEGER_STAGE_CHECKED
// Number of violated checks since the start of the process. Not thread-safe.
inline long check_violations = 0;
// Largest bit length of |value| seen so far, per kind and tier; the wide tier
// is recorded at index num_tiers + 1.
inline int observed_bits[NUM_KINDS][num_tiers + 2] = {};
constexpr int tier_index(int D) noexcept { return D <= num_tiers ? D : num_tiers + 1; }
#define SHEWCHUK_INTEGER_STAGE_CHECK(condition) ((condition) ? (void)0 : (void)++check_violations)
#else
#define SHEWCHUK_INTEGER_STAGE_CHECK(condition) ((void)0)
#endif

// ---------------------------------------------------------------------------
// Bit counting. (Not <bit>: in unoptimized builds its function templates
// would become external symbols of the library.)
// ---------------------------------------------------------------------------

// The number of bits of x: the smallest k with x < 2^k (0 for x = 0).
SHEWCHUK_INTEGER_STAGE_INLINE int bit_length(limb x) noexcept {
#if !defined(SHEWCHUK_INTEGER_STAGE_PORTABLE) && defined(__GNUC__)
  return x != 0 ? 64 - __builtin_clzll(x) : 0;
#elif defined(SHEWCHUK_INTEGER_STAGE_MSVC_INTRINSICS)
  unsigned long index;
  return _BitScanReverse64(&index, x) ? static_cast<int>(index) + 1 : 0;
#else
  // Binary search for the highest set bit.
  int length = 0;
  for (int step = 32; step > 0; step >>= 1) {
    if ((x >> step) != 0) {
      x >>= step;
      length += step;
    }
  }
  return length + static_cast<int>(x);  // x is now 0 or 1
#endif
}

// The number of trailing zero bits of x; 64 for x = 0.
SHEWCHUK_INTEGER_STAGE_INLINE int trailing_zeros(limb x) noexcept {
#if !defined(SHEWCHUK_INTEGER_STAGE_PORTABLE) && defined(__GNUC__)
  return x != 0 ? __builtin_ctzll(x) : 64;
#elif defined(SHEWCHUK_INTEGER_STAGE_MSVC_INTRINSICS)
  unsigned long index;
  return _BitScanForward64(&index, x) ? static_cast<int>(index) : 64;
#else
  // x & -x keeps only the lowest set bit of x.
  return x != 0 ? bit_length(x & (limb{0} - x)) - 1 : 64;
#endif
}

// ---------------------------------------------------------------------------
// Limb primitives. Three interchangeable implementations of the same exact
// operations; all arithmetic is unsigned.
// ---------------------------------------------------------------------------
#if !defined(SHEWCHUK_INTEGER_STAGE_PORTABLE) && defined(__SIZEOF_INT128__)

inline constexpr const char* limb_arithmetic = "unsigned __int128";
using wide = unsigned __int128;

// a + b + carry; carry in and out are 0 or 1.
SHEWCHUK_INTEGER_STAGE_INLINE limb add_carry(limb a, limb b, limb& carry) noexcept {
  const wide sum = static_cast<wide>(a) + b + carry;
  carry = static_cast<limb>(sum >> 64);
  return static_cast<limb>(sum);
}
// a - b - borrow; borrow in and out are 0 or 1.
SHEWCHUK_INTEGER_STAGE_INLINE limb sub_borrow(limb a, limb b, limb& borrow) noexcept {
  const wide difference = static_cast<wide>(a) - b - borrow;
  borrow = static_cast<limb>(difference >> 64) & 1;
  return static_cast<limb>(difference);
}
// Low limb of a * b + c + d; the high limb goes to `high`. The value is at
// most (2^64 - 1)^2 + 2 (2^64 - 1) = 2^128 - 1: it always fits.
SHEWCHUK_INTEGER_STAGE_INLINE limb multiply_add(limb a, limb b, limb c, limb d,
                                                limb& high) noexcept {
  const wide value = static_cast<wide>(a) * b + c + d;
  high = static_cast<limb>(value >> 64);
  return static_cast<limb>(value);
}

#else

SHEWCHUK_INTEGER_STAGE_INLINE limb add_carry(limb a, limb b, limb& carry) noexcept {
  const limb partial = a + b;
  const limb carry1 = partial < a;
  const limb sum = partial + carry;
  const limb carry2 = sum < partial;
  carry = carry1 | carry2;  // at most one of the two additions wraps
  return sum;
}
SHEWCHUK_INTEGER_STAGE_INLINE limb sub_borrow(limb a, limb b, limb& borrow) noexcept {
  const limb partial = a - b;
  const limb borrow1 = a < b;
  const limb difference = partial - borrow;
  const limb borrow2 = partial < borrow;
  borrow = borrow1 | borrow2;  // at most one of the two subtractions wraps
  return difference;
}

#if defined(SHEWCHUK_INTEGER_STAGE_MSVC_INTRINSICS) && defined(_M_X64)
inline constexpr const char* limb_arithmetic = "_umul128";
SHEWCHUK_INTEGER_STAGE_INLINE limb multiply(limb a, limb b, limb& high) noexcept {
  return _umul128(a, b, &high);
}
#else
inline constexpr const char* limb_arithmetic = "portable 32-bit halves";
// 64 x 64 -> 128 from four 32 x 32 -> 64 products.
SHEWCHUK_INTEGER_STAGE_INLINE limb multiply(limb a, limb b, limb& high) noexcept {
  const limb mask = 0xffffffffu;
  const limb a0 = a & mask, a1 = a >> 32, b0 = b & mask, b1 = b >> 32;
  const limb p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  // Three terms below 2^32 each: no overflow.
  const limb middle = (p00 >> 32) + (p01 & mask) + (p10 & mask);
  // The true high limb is below 2^64, so this sum does not wrap.
  high = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
  return (middle << 32) | (p00 & mask);
}
#endif

SHEWCHUK_INTEGER_STAGE_INLINE limb multiply_add(limb a, limb b, limb c, limb d,
                                                limb& high) noexcept {
  limb top;
  limb low = multiply(a, b, top);
  low += c;
  top += low < c;  // cannot wrap: the whole value is below 2^128
  low += d;
  top += low < d;
  high = top;
  return low;
}

#endif

// ---------------------------------------------------------------------------
// Fixed-width signed integers: N limbs, least significant first, two's
// complement. Nothing here checks for overflow in the shipped build; that no
// operation overflows is what the proof shows and the checked build verifies.
// ---------------------------------------------------------------------------
template <int N>
struct Int {
  limb w[N];
};

template <int N>
SHEWCHUK_INTEGER_STAGE_INLINE bool is_negative(const Int<N>& a) noexcept {
  return (a.w[N - 1] >> 63) != 0;
}

// a + b modulo 2^(64 N).
template <int N>
SHEWCHUK_INTEGER_STAGE_INLINE Int<N> add(const Int<N>& a, const Int<N>& b) noexcept {
  Int<N> r;
  limb carry = 0;
  for (int i = 0; i < N; i++) r.w[i] = add_carry(a.w[i], b.w[i], carry);
  // Signed overflow: the operands have the same sign and the result another.
  SHEWCHUK_INTEGER_STAGE_CHECK(is_negative(a) != is_negative(b) ||
                               is_negative(r) == is_negative(a));
  return r;
}

// a - b modulo 2^(64 N).
template <int N>
SHEWCHUK_INTEGER_STAGE_INLINE Int<N> sub(const Int<N>& a, const Int<N>& b) noexcept {
  Int<N> r;
  limb borrow = 0;
  for (int i = 0; i < N; i++) r.w[i] = sub_borrow(a.w[i], b.w[i], borrow);
  // Signed overflow: the operands have different signs and the result has
  // the subtrahend's.
  SHEWCHUK_INTEGER_STAGE_CHECK(is_negative(a) == is_negative(b) ||
                               is_negative(r) == is_negative(a));
  return r;
}

// The exact product: an N-limb by an M-limb signed integer always fits N + M
// limbs (|a| <= 2^(64N-1) and |b| <= 2^(64M-1) give |ab| <= 2^(64(N+M)-2)).
//
// Computed as the unsigned product of the bit patterns, corrected: with
// A = a + 2^(64N) [a < 0] and B = b + 2^(64M) [b < 0] the bit patterns,
//   a b = A B - 2^(64N) [a < 0] B - 2^(64M) [b < 0] A   (mod 2^(64(N+M))).
template <int N, int M>
SHEWCHUK_INTEGER_STAGE_INLINE Int<N + M> mul(const Int<N>& a, const Int<M>& b) noexcept {
  Int<N + M> r;
  for (int j = 0; j < M; j++) r.w[j] = 0;
  for (int i = 0; i < N; i++) {
    limb carry = 0;
    for (int j = 0; j < M; j++) {
      r.w[i + j] = multiply_add(a.w[i], b.w[j], r.w[i + j], carry, carry);
    }
    r.w[i + M] = carry;
  }
  const limb a_negative = limb{0} - (a.w[N - 1] >> 63);  // all ones or zero
  const limb b_negative = limb{0} - (b.w[M - 1] >> 63);
  limb borrow = 0;
  for (int j = 0; j < M; j++) r.w[N + j] = sub_borrow(r.w[N + j], b.w[j] & a_negative, borrow);
  borrow = 0;
  for (int i = 0; i < N; i++) r.w[M + i] = sub_borrow(r.w[M + i], a.w[i] & b_negative, borrow);
  return r;
}

// The same product as a function call: one copy of the code per pair of
// sizes instead of one per use.
template <int N, int M>
SHEWCHUK_INTEGER_STAGE_NOINLINE Int<N + M> mul_out_of_line(const Int<N>& a,
                                                           const Int<M>& b) noexcept {
  return mul(a, b);
}

// -1, 0 or +1.
template <int N>
SHEWCHUK_INTEGER_STAGE_INLINE int sign(const Int<N>& a) noexcept {
  limb any = 0;
  for (int i = 0; i < N; i++) any |= a.w[i];
  return is_negative(a) ? -1 : (any != 0 ? 1 : 0);
}

#ifdef SHEWCHUK_INTEGER_STAGE_CHECKED
// Number of bits of |a|: the smallest k with |a| < 2^k.
template <int N>
int bit_length_of_magnitude(const Int<N>& a) noexcept {
  Int<N> magnitude = a;
  if (is_negative(a)) {
    limb carry = 1;
    for (int i = 0; i < N; i++) {
      const limb inverted = ~a.w[i];
      magnitude.w[i] = inverted + carry;
      carry = magnitude.w[i] < inverted;
    }
    // -2^(64N-1) is its own negation: its magnitude needs 64N bits.
    if (is_negative(magnitude)) return 64 * N;
  }
  for (int i = N - 1; i >= 0; i--) {
    if (magnitude.w[i] != 0) return 64 * i + bit_length(magnitude.w[i]);
  }
  return 0;
}
#endif

// Declares that `value` is an intermediate of the given kind: in the checked
// build, verifies the bound the proof states for that kind (and with it that
// the value fits its limbs with the sign bit to spare) and records how close
// the value came. In the shipped build this is the identity.
template <int N>
SHEWCHUK_INTEGER_STAGE_INLINE Int<N> bounded([[maybe_unused]] Kind kind, [[maybe_unused]] int D,
                                             [[maybe_unused]] int W, const Int<N>& value) noexcept {
#ifdef SHEWCHUK_INTEGER_STAGE_CHECKED
  const int bits = bit_length_of_magnitude(value);
  SHEWCHUK_INTEGER_STAGE_CHECK(N == limbs_of(kind, D));
  SHEWCHUK_INTEGER_STAGE_CHECK(bits <= magnitude_bits(kind, W));
  SHEWCHUK_INTEGER_STAGE_CHECK(bits <= 64 * N - 1);
  if (bits > observed_bits[kind][tier_index(D)]) observed_bits[kind][tier_index(D)] = bits;
#endif
  return value;
}

// ---------------------------------------------------------------------------
// Scaling: doubles -> integers with a common power-of-two factor
// ---------------------------------------------------------------------------

// The coordinates of one call after scaling. With `low` the same for all
// coordinates (it is not stored: the sign of the determinant does not depend
// on it), a nonzero coordinate i equals
//   (negative[i] ? -1 : +1) * magnitude[i] * 2^shift[i] * 2^low
// with magnitude[i] odd and 0 <= shift[i] < width. A zero coordinate has
// magnitude[i] == 0; its shift[i] is then a nonnegative number without
// meaning (0 if all coordinates are zero, more than 2^20 otherwise), and
// every use of a shift is a use on the magnitude, which is zero.
// `width` is W: the bit length of the largest scaled magnitude
// magnitude[i] * 2^shift[i], i.e. the smallest W with all of them < 2^W.
// It is 0 exactly when every coordinate is zero.
template <int K>
struct Scaled {
  limb magnitude[K];
  limb negative[K];  // all ones if the sign bit of the coordinate is set, else zero
  int shift[K];
  int width;
};

// Where the set bits of the coordinates of one call are. Biased by 1075: the
// lowest set bit of any coordinate is 2^(low - 1075) and every magnitude is
// below 2^(high - 1075). If all coordinates are zero, low is beyond 2^20 and
// high is 1. (Not a part of Scaled: the tiers do not need it, and Scaled
// keeps the layout the tiers were measured with.)
struct Extent {
  int low, high;
};

// Decomposes the K doubles whose bit patterns are in `bits`. Returns false,
// leaving `out` unspecified, if one of them is an infinity or a NaN.
//
// A double with biased exponent field b and fraction field f is
//   b = 0        (-1)^s * f * 2^-1074             (zero or subnormal)
//   0 < b < 2047 (-1)^s * (2^52 + f) * 2^(b-1075) (normal)
//   b = 2047     infinity or NaN
// so both finite cases are (-1)^s * m * 2^(E - 1075) with the integer
// m = f + 2^52 [b != 0] < 2^53 and E = max(b, 1) in [1, 2046].
//
// Exponents stay biased by 1075 in this function (only differences of them
// are used). Per coordinate, `lowest` is the exponent of its lowest set bit
// and `highest` one more than that of its highest set bit:
//   nonzero finite   1 <= lowest < highest <= 2046 + 53 = 2099
//   zero             lowest > 2^20, highest = 1
//   infinity, NaN    highest = 2047 + 53 = 2100
// A zero thus never is the minimum of `lowest` nor the maximum of `highest`
// when a nonzero coordinate exists, and a non-finite coordinate is the
// maximum of `highest`: no per-coordinate test is needed for either.
template <int K>
SHEWCHUK_INTEGER_STAGE_INLINE bool scale(const limb* bits, Scaled<K>& out,
                                         Extent& extent) noexcept {
  int lowest[K];
  int low = INT_MAX, high = 0;
  for (int i = 0; i < K; i++) {
    const limb fraction = bits[i] & ((limb{1} << 52) - 1);
    const int biased = static_cast<int>((bits[i] >> 52) & 0x7ff);
    const limb m = fraction | (static_cast<limb>(biased != 0) << 52);
    const int exponent = biased + (biased == 0);
    // Strip the trailing zero bits: m = odd * 2^zeros. trailing_zeros(0) is
    // 64; the mask keeps the shift count in range and leaves m = 0 as it is.
    const int zeros = trailing_zeros(m);
    out.magnitude[i] = m >> (zeros & 63);
    out.negative[i] = limb{0} - (bits[i] >> 63);
    // zeros >> 6 is 1 for m = 0 and 0 otherwise (zeros <= 52 then).
    lowest[i] = exponent + zeros + ((zeros >> 6) << 20);
    const int highest = exponent + bit_length(m);
    low = lowest[i] < low ? lowest[i] : low;
    high = highest > high ? highest : high;
  }
  if (high >= 2047 + 53) return false;
  // All coordinates zero: high is 1 and low is beyond 2^20.
  out.width = high > low ? high - low : 0;
  extent.low = low;
  extent.high = high;
  for (int i = 0; i < K; i++) out.shift[i] = lowest[i] - low;
  return true;
}
template <int K>
SHEWCHUK_INTEGER_STAGE_INLINE bool scale(const limb* bits, Scaled<K>& out) noexcept {
  Extent extent;
  return scale(bits, out, extent);
}

// The scaled coordinate i as a D-limb integer. Requires out.width <= 64 D - 1
// (every tier's limit is below that), which makes the shifted magnitude fit
// with the sign bit to spare.
template <int D, int K>
SHEWCHUK_INTEGER_STAGE_INLINE Int<D> coordinate(const Scaled<K>& s, int i) noexcept {
  const limb m = s.magnitude[i];
  const int shift = s.shift[i];
  // For a nonzero coordinate, 0 <= shift < width <= 64 D - 1. For a zero one
  // the shift is meaningless, and m = 0 makes every part below zero.
  SHEWCHUK_INTEGER_STAGE_CHECK(shift >= 0);
  SHEWCHUK_INTEGER_STAGE_CHECK(m == 0 || shift + bit_length(m) <= s.width);
  const int bit = shift & 63;
  const limb low_part = m << bit;
  Int<D> magnitude;
  if constexpr (D == 1) {
    // shift <= 63 for a nonzero coordinate: bit is the whole shift, and the
    // shifted magnitude is below 2^width: no bit leaves the limb.
    SHEWCHUK_INTEGER_STAGE_CHECK(m == 0 || (shift == bit && (low_part >> bit) == m));
    magnitude.w[0] = low_part;
  } else {
    const int word = shift >> 6;
    // m >> (64 - bit) without a shift by 64 when bit is 0.
    const limb high_part = (m >> 1) >> (63 - bit);
    // If the high part has no limb to go to, it is zero (nothing is dropped).
    SHEWCHUK_INTEGER_STAGE_CHECK(m == 0 || word < D);
    SHEWCHUK_INTEGER_STAGE_CHECK(word + 1 < D || high_part == 0);
    for (int k = 0; k < D; k++) {
      magnitude.w[k] = (k == word ? low_part : 0) | (k == word + 1 ? high_part : 0);
    }
  }
  // Negate if the coordinate is negative: (x XOR mask) - mask, mask = -1 or 0.
  const limb mask = s.negative[i];
  Int<D> r;
  limb borrow = 0;
  for (int k = 0; k < D; k++) r.w[k] = sub_borrow(magnitude.w[k] ^ mask, mask, borrow);
  return bounded(COORDINATE, D, s.width, r);
}

// ---------------------------------------------------------------------------
// The four determinants in tier D. Same expressions and sign conventions as
// the floating-point stage A of each predicate (and as predicates.c).
// ---------------------------------------------------------------------------
template <int D>
struct Tier {
  static constexpr int limit = max_width(D);

  template <int N>
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<N> is(Kind kind, int W, const Int<N>& value) noexcept {
    return bounded(kind, D, W, value);
  }
  // The exact product a * b. Tiers 1 and 2, which take the inputs met in
  // practice, get every multiplication expanded in place; the other tiers
  // call one shared copy per operand size, which costs tier 3 nothing
  // measurable and keeps the library's code smaller.
  template <int N, int M>
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<N + M> times(const Int<N>& a, const Int<M>& b) noexcept {
    if constexpr (D <= 2) {
      return mul(a, b);
    } else {
      return mul_out_of_line(a, b);
    }
  }

  // a - b for scaled coordinates a and b.
  template <int K>
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<D> difference(const Scaled<K>& s, int a,
                                                         int b) noexcept {
    return is(DIFFERENCE, s.width, sub(coordinate<D>(s, a), coordinate<D>(s, b)));
  }
  // p * q for differences p and q.
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<2 * D> product2(int W, const Int<D>& p,
                                                           const Int<D>& q) noexcept {
    return is(PRODUCT2, W, times(p, q));
  }
  // p q - r t for differences p, q, r, t.
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<2 * D> minor2(int W, const Int<D>& p, const Int<D>& q,
                                                         const Int<D>& r,
                                                         const Int<D>& t) noexcept {
    return is(MINOR2, W, sub(product2(W, p, q), product2(W, r, t)));
  }
  // d * m for a difference d and a 2 x 2 minor m.
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<3 * D> product3(int W, const Int<D>& d,
                                                           const Int<2 * D>& m) noexcept {
    return is(PRODUCT3, W, times(d, m));
  }

  static int orient2d(const Scaled<6>& s) noexcept {
    const int W = s.width;
    const Int<D> acx = difference(s, 0, 4), acy = difference(s, 1, 5);
    const Int<D> bcx = difference(s, 2, 4), bcy = difference(s, 3, 5);
    return sign(minor2(W, acx, bcy, acy, bcx));
  }

  static int orient3d(const Scaled<12>& s) noexcept {
    const int W = s.width;
    const Int<D> adx = difference(s, 0, 9), ady = difference(s, 1, 10), adz = difference(s, 2, 11);
    const Int<D> bdx = difference(s, 3, 9), bdy = difference(s, 4, 10), bdz = difference(s, 5, 11);
    const Int<D> cdx = difference(s, 6, 9), cdy = difference(s, 7, 10), cdz = difference(s, 8, 11);
    const Int<2 * D> bc = minor2(W, bdx, cdy, cdx, bdy);
    const Int<2 * D> ca = minor2(W, cdx, ady, adx, cdy);
    const Int<2 * D> ab = minor2(W, adx, bdy, bdx, ady);
    // A partial sum of the three terms is bounded like the whole sum.
    const Int<3 * D> partial = is(MINOR3, W, add(product3(W, adz, bc), product3(W, bdz, ca)));
    return sign(is(MINOR3, W, add(partial, product3(W, cdz, ab))));
  }

  static int incircle(const Scaled<8>& s) noexcept {
    const int W = s.width;
    const Int<D> adx = difference(s, 0, 6), ady = difference(s, 1, 7);
    const Int<D> bdx = difference(s, 2, 6), bdy = difference(s, 3, 7);
    const Int<D> cdx = difference(s, 4, 6), cdy = difference(s, 5, 7);
    const Int<2 * D> bc = minor2(W, bdx, cdy, cdx, bdy);
    const Int<2 * D> ca = minor2(W, cdx, ady, adx, cdy);
    const Int<2 * D> ab = minor2(W, adx, bdy, bdx, ady);
    const Int<2 * D> alift = is(LIFT2, W, add(product2(W, adx, adx), product2(W, ady, ady)));
    const Int<2 * D> blift = is(LIFT2, W, add(product2(W, bdx, bdx), product2(W, bdy, bdy)));
    const Int<2 * D> clift = is(LIFT2, W, add(product2(W, cdx, cdx), product2(W, cdy, cdy)));
    const Int<4 * D> a = is(INCIRCLE_TERM, W, times(alift, bc));
    const Int<4 * D> b = is(INCIRCLE_TERM, W, times(blift, ca));
    const Int<4 * D> c = is(INCIRCLE_TERM, W, times(clift, ab));
    const Int<4 * D> partial = is(INCIRCLE_DET, W, add(a, b));
    return sign(is(INCIRCLE_DET, W, add(partial, c)));
  }

  // x^2 + y^2 + z^2 for differences x, y, z.
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<2 * D> lift3(int W, const Int<D>& x, const Int<D>& y,
                                                        const Int<D>& z) noexcept {
    const Int<2 * D> partial = is(LIFT3, W, add(product2(W, x, x), product2(W, y, y)));
    return is(LIFT3, W, add(partial, product2(W, z, z)));
  }
  // The combination p +- q + r of three PRODUCT3 values.
  static SHEWCHUK_INTEGER_STAGE_INLINE Int<3 * D> minor3(int W, const Int<3 * D>& p,
                                                         bool subtract_q, const Int<3 * D>& q,
                                                         const Int<3 * D>& r) noexcept {
    const Int<3 * D> partial = is(MINOR3, W, subtract_q ? sub(p, q) : add(p, q));
    return is(MINOR3, W, add(partial, r));
  }

  static int insphere(const Scaled<15>& s) noexcept {
    const int W = s.width;
    const Int<D> aex = difference(s, 0, 12), aey = difference(s, 1, 13), aez = difference(s, 2, 14);
    const Int<D> bex = difference(s, 3, 12), bey = difference(s, 4, 13), bez = difference(s, 5, 14);
    const Int<D> cex = difference(s, 6, 12), cey = difference(s, 7, 13), cez = difference(s, 8, 14);
    const Int<D> dex = difference(s, 9, 12), dey = difference(s, 10, 13), dez = difference(s, 11, 14);
    const Int<2 * D> ab = minor2(W, aex, bey, bex, aey);
    const Int<2 * D> bc = minor2(W, bex, cey, cex, bey);
    const Int<2 * D> cd = minor2(W, cex, dey, dex, cey);
    const Int<2 * D> da = minor2(W, dex, aey, aex, dey);
    const Int<2 * D> ac = minor2(W, aex, cey, cex, aey);
    const Int<2 * D> bd = minor2(W, bex, dey, dex, bey);
    // abc = aez bc - bez ac + cez ab, and so on.
    const Int<3 * D> abc =
        minor3(W, product3(W, aez, bc), true, product3(W, bez, ac), product3(W, cez, ab));
    const Int<3 * D> bcd =
        minor3(W, product3(W, bez, cd), true, product3(W, cez, bd), product3(W, dez, bc));
    const Int<3 * D> cda =
        minor3(W, product3(W, cez, da), false, product3(W, dez, ac), product3(W, aez, cd));
    const Int<3 * D> dab =
        minor3(W, product3(W, dez, ab), false, product3(W, aez, bd), product3(W, bez, da));
    const Int<2 * D> alift = lift3(W, aex, aey, aez);
    const Int<2 * D> blift = lift3(W, bex, bey, bez);
    const Int<2 * D> clift = lift3(W, cex, cey, cez);
    const Int<2 * D> dlift = lift3(W, dex, dey, dez);
    // det = (dlift abc - clift dab) + (blift cda - alift bcd)
    const Int<5 * D> left = is(INSPHERE_DET, W,
                               sub(is(INSPHERE_TERM, W, times(dlift, abc)),
                                   is(INSPHERE_TERM, W, times(clift, dab))));
    const Int<5 * D> right = is(INSPHERE_DET, W,
                                sub(is(INSPHERE_TERM, W, times(blift, cda)),
                                    is(INSPHERE_TERM, W, times(alift, bcd))));
    return sign(is(INSPHERE_DET, W, add(left, right)));
  }
};

// Tiers 4 and 5 as functions of their own: their integers are larger, and
// neither their code nor the stack they need is to become part of the
// function that holds tiers 1 to 3.
template <int D>
struct OutOfLine {
  [[maybe_unused]] static SHEWCHUK_INTEGER_STAGE_NOINLINE int orient2d(const Scaled<6>& s) noexcept {
    return Tier<D>::orient2d(s);
  }
  [[maybe_unused]] static SHEWCHUK_INTEGER_STAGE_NOINLINE int orient3d(const Scaled<12>& s) noexcept {
    return Tier<D>::orient3d(s);
  }
  [[maybe_unused]] static SHEWCHUK_INTEGER_STAGE_NOINLINE int incircle(const Scaled<8>& s) noexcept {
    return Tier<D>::incircle(s);
  }
  [[maybe_unused]] static SHEWCHUK_INTEGER_STAGE_NOINLINE int insphere(const Scaled<15>& s) noexcept {
    return Tier<D>::insphere(s);
  }
};

// The bit pattern of each of the `count` doubles at p.
SHEWCHUK_INTEGER_STAGE_INLINE void load_bits(const double* p, int count, limb* bits) noexcept {
  std::memcpy(bits, p, sizeof(double) * static_cast<std::size_t>(count));
}

// Scales the coordinates and evaluates the determinant in the narrowest fast
// tier that holds them. Tiers 1 to 3 are part of this code; coordinates wider
// than those go to beyond_tier_3_<predicate>(), which has tiers 4 and 5, the
// refusal and the wide tier. All coordinates zero (width 0) is decided by
// tier 1. This is the code of the request `exact_or_floating_point`, the one
// the predicates make on the path that matters for speed; its text up to the
// last line is what it was when the stage had three tiers, on purpose: the
// machine code of the tiers that take the inputs met in practice is then
// what it was (measured: with tiers 4 and 5 added here, tiers 1 to 3 of one
// predicate or another got 1.5% to 7% slower with each of three compilers).
#define SHEWCHUK_INTEGER_STAGE_DECIDE(predicate, count)                         \
  Scaled<count> s;                                                              \
  Extent extent;                                                                \
  if (!scale(bits, s, extent)) {                                                \
    SHEWCHUK_INTEGER_STAGE_RECORD(-1, 0);                                       \
    return not_finite;                                                          \
  }                                                                             \
  if (s.width <= Tier<1>::limit) {                                              \
    SHEWCHUK_INTEGER_STAGE_RECORD(s.width, 1);                                  \
    return Tier<1>::predicate(s);                                               \
  }                                                                             \
  if (s.width <= Tier<2>::limit) {                                              \
    SHEWCHUK_INTEGER_STAGE_RECORD(s.width, 2);                                  \
    return Tier<2>::predicate(s);                                               \
  }                                                                             \
  if (s.width <= Tier<3>::limit) {                                              \
    SHEWCHUK_INTEGER_STAGE_RECORD(s.width, 3);                                  \
    return Tier<3>::predicate(s);                                               \
  }                                                                             \
  return beyond_tier_3_##predicate(s, extent.low, extent.high)

// Coordinates wider than tier 3 holds: tier 4 or 5 by width; wider than
// those, refused inside the floating-point domain and decided by the wide
// tier outside.
#define SHEWCHUK_INTEGER_STAGE_BEYOND_TIER_3(predicate, degree)                 \
  if (s.width <= Tier<4>::limit) {                                              \
    SHEWCHUK_INTEGER_STAGE_RECORD(s.width, 4);                                  \
    return OutOfLine<4>::predicate(s);                                          \
  }                                                                             \
  if (s.width <= Tier<5>::limit) {                                              \
    SHEWCHUK_INTEGER_STAGE_RECORD(s.width, 5);                                  \
    return OutOfLine<5>::predicate(s);                                          \
  }                                                                             \
  if (in_floating_point_domain(degree, low, high)) {                            \
    SHEWCHUK_INTEGER_STAGE_RECORD(s.width, 0);                                  \
    return refused;                                                             \
  }                                                                             \
  SHEWCHUK_INTEGER_STAGE_RECORD(s.width, wide_tier);                            \
  return wide_##predicate(s)
static_assert(num_tiers == 5, "the two macros above list the fast tiers explicitly");

// The other two requests, in front of the code above: `prefer_floating_point`
// refuses inside the floating-point domain before any tier is tried, and
// `always_exact` sends coordinates wider than the fast tiers to the wide tier
// without asking where they are. Whatever is left is decided by the code
// above, which then cannot refuse: under `prefer_floating_point` the
// coordinates are outside the domain (or all zero), under `always_exact`
// they fit a fast tier. (The coordinates are scaled twice on these paths.
// They are rare.)
#define SHEWCHUK_INTEGER_STAGE_OTHER_REQUESTS(predicate, count, degree)        \
  if (request != Request::exact_or_floating_point) {                            \
    Scaled<count> s;                                                            \
    Extent extent;                                                              \
    if (!scale(bits, s, extent)) {                                              \
      SHEWCHUK_INTEGER_STAGE_RECORD(-1, 0);                                     \
      return not_finite;                                                        \
    }                                                                           \
    if (request == Request::prefer_floating_point) {                            \
      if (s.width != 0 && in_floating_point_domain(degree, extent.low, extent.high)) { \
        SHEWCHUK_INTEGER_STAGE_RECORD(s.width, 0);                              \
        return refused;                                                         \
      }                                                                         \
    } else if (s.width > max_width(num_tiers)) {                                \
      SHEWCHUK_INTEGER_STAGE_RECORD(s.width, wide_tier);                        \
      return wide_##predicate(s);                                               \
    }                                                                           \
  }

// orient2d: the wide tier's determinant, never inlined: its integers are large
// (docs/proofs/input-domain.md, section 7), and the stack they need must not
// become part of the frame of the fast tiers.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int wide_orient2d(const Scaled<6>& s) noexcept {
  return Tier<wide_tier>::orient2d(s);
}
// Coordinates wider than tier 3 holds; `low` and `high` are those of the
// Extent (as two integers, so that the caller hands them over in registers).
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int beyond_tier_3_orient2d(
    const Scaled<6>& s, int low, int high) noexcept {
  SHEWCHUK_INTEGER_STAGE_BEYOND_TIER_3(orient2d, 2);
}
// The exact sign (-1, 0, +1), with the sign convention of the public
// function, or `refused`, or `not_finite`: the request
// `exact_or_floating_point`.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int orient2d(
    const double* pa, const double* pb, const double* pc) noexcept {
  limb bits[6];
  load_bits(pa, 2, bits), load_bits(pb, 2, bits + 2), load_bits(pc, 2, bits + 4);
  SHEWCHUK_INTEGER_STAGE_DECIDE(orient2d, 6);
}
// The same under any request.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int orient2d(
    const double* pa, const double* pb, const double* pc, Request request) noexcept {
  limb bits[6];
  load_bits(pa, 2, bits), load_bits(pb, 2, bits + 2), load_bits(pc, 2, bits + 4);
  SHEWCHUK_INTEGER_STAGE_OTHER_REQUESTS(orient2d, 6, 2)
  return orient2d(pa, pb, pc);
}

// orient3d: the wide tier's determinant, never inlined: its integers are large
// (docs/proofs/input-domain.md, section 7), and the stack they need must not
// become part of the frame of the fast tiers.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int wide_orient3d(const Scaled<12>& s) noexcept {
  return Tier<wide_tier>::orient3d(s);
}
// Coordinates wider than tier 3 holds; `low` and `high` are those of the
// Extent (as two integers, so that the caller hands them over in registers).
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int beyond_tier_3_orient3d(
    const Scaled<12>& s, int low, int high) noexcept {
  SHEWCHUK_INTEGER_STAGE_BEYOND_TIER_3(orient3d, 3);
}
// The exact sign (-1, 0, +1), with the sign convention of the public
// function, or `refused`, or `not_finite`: the request
// `exact_or_floating_point`.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int orient3d(
    const double* pa, const double* pb, const double* pc, const double* pd) noexcept {
  limb bits[12];
  load_bits(pa, 3, bits), load_bits(pb, 3, bits + 3), load_bits(pc, 3, bits + 6);
  load_bits(pd, 3, bits + 9);
  SHEWCHUK_INTEGER_STAGE_DECIDE(orient3d, 12);
}
// The same under any request.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int orient3d(
    const double* pa, const double* pb, const double* pc, const double* pd, Request request) noexcept {
  limb bits[12];
  load_bits(pa, 3, bits), load_bits(pb, 3, bits + 3), load_bits(pc, 3, bits + 6);
  load_bits(pd, 3, bits + 9);
  SHEWCHUK_INTEGER_STAGE_OTHER_REQUESTS(orient3d, 12, 3)
  return orient3d(pa, pb, pc, pd);
}

// incircle: the wide tier's determinant, never inlined: its integers are large
// (docs/proofs/input-domain.md, section 7), and the stack they need must not
// become part of the frame of the fast tiers.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int wide_incircle(const Scaled<8>& s) noexcept {
  return Tier<wide_tier>::incircle(s);
}
// Coordinates wider than tier 3 holds; `low` and `high` are those of the
// Extent (as two integers, so that the caller hands them over in registers).
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int beyond_tier_3_incircle(
    const Scaled<8>& s, int low, int high) noexcept {
  SHEWCHUK_INTEGER_STAGE_BEYOND_TIER_3(incircle, 4);
}
// The exact sign (-1, 0, +1), with the sign convention of the public
// function, or `refused`, or `not_finite`: the request
// `exact_or_floating_point`.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int incircle(
    const double* pa, const double* pb, const double* pc, const double* pd) noexcept {
  limb bits[8];
  load_bits(pa, 2, bits), load_bits(pb, 2, bits + 2), load_bits(pc, 2, bits + 4);
  load_bits(pd, 2, bits + 6);
  SHEWCHUK_INTEGER_STAGE_DECIDE(incircle, 8);
}
// The same under any request.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int incircle(
    const double* pa, const double* pb, const double* pc, const double* pd, Request request) noexcept {
  limb bits[8];
  load_bits(pa, 2, bits), load_bits(pb, 2, bits + 2), load_bits(pc, 2, bits + 4);
  load_bits(pd, 2, bits + 6);
  SHEWCHUK_INTEGER_STAGE_OTHER_REQUESTS(incircle, 8, 4)
  return incircle(pa, pb, pc, pd);
}

// insphere: the wide tier's determinant, never inlined: its integers are large
// (docs/proofs/input-domain.md, section 7), and the stack they need must not
// become part of the frame of the fast tiers.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int wide_insphere(const Scaled<15>& s) noexcept {
  return Tier<wide_tier>::insphere(s);
}
// Coordinates wider than tier 3 holds; `low` and `high` are those of the
// Extent (as two integers, so that the caller hands them over in registers).
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int beyond_tier_3_insphere(
    const Scaled<15>& s, int low, int high) noexcept {
  SHEWCHUK_INTEGER_STAGE_BEYOND_TIER_3(insphere, 5);
}
// The exact sign (-1, 0, +1), with the sign convention of the public
// function, or `refused`, or `not_finite`: the request
// `exact_or_floating_point`.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int insphere(
    const double* pa, const double* pb, const double* pc, const double* pd,
    const double* pe) noexcept {
  limb bits[15];
  load_bits(pa, 3, bits), load_bits(pb, 3, bits + 3), load_bits(pc, 3, bits + 6);
  load_bits(pd, 3, bits + 9), load_bits(pe, 3, bits + 12);
  SHEWCHUK_INTEGER_STAGE_DECIDE(insphere, 15);
}
// The same under any request.
[[maybe_unused]] SHEWCHUK_INTEGER_STAGE_NOINLINE int insphere(
    const double* pa, const double* pb, const double* pc, const double* pd,
    const double* pe, Request request) noexcept {
  limb bits[15];
  load_bits(pa, 3, bits), load_bits(pb, 3, bits + 3), load_bits(pc, 3, bits + 6);
  load_bits(pd, 3, bits + 9), load_bits(pe, 3, bits + 12);
  SHEWCHUK_INTEGER_STAGE_OTHER_REQUESTS(insphere, 15, 5)
  return insphere(pa, pb, pc, pd, pe);
}

#undef SHEWCHUK_INTEGER_STAGE_OTHER_REQUESTS
#undef SHEWCHUK_INTEGER_STAGE_BEYOND_TIER_3
#undef SHEWCHUK_INTEGER_STAGE_DECIDE

}  // namespace integer_stage
}  // namespace
}  // namespace shewchuk::detail
