// The four adaptive predicates of Shewchuk's 1996 predicates.c (orient2d,
// orient3d, incircle, insphere) and the expansion arithmetic they call.
//
// This file is a port, not a rewrite. Except where the list below says
// otherwise, every function performs the same floating-point operations in
// the same order as the function of the same name in reference/predicates.c,
// with the same stages and the same buffer capacities. Every function returns
// bit-identical values (the test suite checks that on the whole corpus). Each
// function is marked with the line range it was ported from so that the two
// can be compared side by side. What changed:
//
//   * ANSI prototypes, const input pointers, `double` instead of the
//     REAL / INEXACT macros;
//   * the error bounds and the splitter are compile-time constants
//     (constants.h) instead of globals written by exactinit();
//   * only the code the four adaptive predicates reach is here;
//   * fast_expansion_sum_zeroelim() no longer reads one element past the end
//     of its inputs (see the comment there);
//   * six array lengths in incircleadapt() are zero-initialized to silence a
//     false compiler warning;
//   * every filter (stages A, B and C) takes absolute values with fabs and
//     compares fabs(det) with the error bound once, instead of the Absolute()
//     macro and two comparisons; docs/exactness.md ("The filters") shows
//     that no returned value can change;
//   * orient2d decides the sign test on its two products and the first
//     filter with one comparison (see shewchuk_orient2d);
//   * SHEWCHUK_STAGE_HIT() marks which stage decides a call. It expands to
//     nothing except in the test-only build (test_hooks.h);
//   * orient2dadapt(), orient3dadapt() and incircleadapt() are never inlined
//     into the predicates that call them (SHEWCHUK_NOINLINE, see its comment);
//   * orient2dadapt() is stages B, Bx and C only, written on pairs of doubles
//     where the compiler has them; stage D is orient2d_stage_d(), which
//     computes the expansion B and the difference tails again
//     (docs/exactness.md, "Differences from the 1996 code").
//
// Two things are new and not from 1996.
//
// The exact integer stage (integer_stage.h). For orient3d, incircle and
// insphere it is tried when the first filter (stage A) cannot certify the
// sign, before the adaptive stages B, C and D (the *_after_filter()
// functions); for orient2d, whose stages B and C are cheaper than it, it is
// tried before stage D (in orient2d_after_stage_c()). It returns only a sign,
// so a
// call it decides returns -1.0, 0.0 or +1.0.
//
// The input-domain guard (filter_guard.h, docs/proofs/input-domain.md). The
// 1996 analysis assumes that no operation overflows or underflows, and the
// 1996 code returns wrong signs, silently, where one does. Here a
// floating-point stage is used only where that cannot have happened:
//   * the first filter's result is returned only if its guard holds, a
//     comparison of bit patterns that proves that the determinant estimate
//     and the error bound are what they would be with an unbounded exponent
//     range;
//   * the stages behind it run only on inputs inside a proven exponent
//     domain (integer_stage.h, in_floating_point_domain()); for orient2d,
//     stages B and C run under a guard of their own;
//   * every other finite input is decided by the integer stage, whose wide
//     tier holds any finite double.
// So the sign is exact for every finite input. A coordinate that is not
// finite gives NaN. For inputs inside the range the 1996 paper states, the
// stages take the decisions they took before, with the same values.
//
// Compiled with SHEWCHUK_PREDICATES_NO_INTEGER_STAGE (test only) both are
// left out and every returned value is the 1996 code's, bit for bit, on every
// input: that build is the negative control of the input-domain tests.
//
// EXACTNESS DEPENDS ON HOW THIS FILE IS COMPILED. The algorithms need every
// operation to be one correctly rounded IEEE 754 binary64 operation, exactly
// as written: no fused multiply-add, no reassociation, no extended precision.
// The build system compiles this translation unit with those flags
// (cmake/StrictFloatingPoint.cmake); that is the safety argument. The guards
// below are an extra layer: contraction (-ffp-contract=fast) defines no macro
// in GCC or clang, so it cannot be detected here. Do not move arithmetic from
// this file into a header.
#if defined(__FAST_MATH__) || defined(__ASSOCIATIVE_MATH__) || defined(__RECIPROCAL_MATH__) || \
    defined(__ARM_FP_FAST) || defined(_M_FP_FAST)
#error "the predicates are not exact under fast-math style floating-point flags"
#endif
#if defined(_M_FP_CONTRACT)
#error "the predicates are not exact when the compiler may contract a*b+c (MSVC /fp:contract)"
#endif

#include <cfloat>
#if FLT_RADIX != 2 || DBL_MANT_DIG != 53
#error "the predicates are only validated for IEEE 754 binary64 doubles"
#endif
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "the predicates need double arithmetic without extended precision"
#endif

#include <cmath>

#ifdef SHEWCHUK_PREDICATES_TEST_HOOKS
// Test-only build: the integer stage records the tier that decides a call.
#define SHEWCHUK_INTEGER_STAGE_TEST_HOOKS
#endif

#include "constants.h"
#include "filter_guard.h"
#include "integer_stage.h"

#ifdef SHEWCHUK_PREDICATES_TEST_HOOKS
// Test-only build: different symbol names, stage recording, exported
// expansion routines. See test_hooks.h.
#include "test_hooks.h"
#define shewchuk_orient2d shewchuk_hooked_orient2d
#define shewchuk_orient3d shewchuk_hooked_orient3d
#define shewchuk_incircle shewchuk_hooked_incircle
#define shewchuk_insphere shewchuk_hooked_insphere
#define shewchuk_orient2d_sign shewchuk_hooked_orient2d_sign
#define shewchuk_orient3d_sign shewchuk_hooked_orient3d_sign
#define shewchuk_incircle_sign shewchuk_hooked_incircle_sign
#define shewchuk_insphere_sign shewchuk_hooked_insphere_sign
// The test-only build does not include the public header.
#define SHEWCHUK_SIGN_NOT_FINITE 2
extern "C" {
int shewchuk_hooked_orient2d_sign(const double *pa, const double *pb, const double *pc);
int shewchuk_hooked_orient3d_sign(const double *pa, const double *pb, const double *pc,
                                  const double *pd);
int shewchuk_hooked_incircle_sign(const double *pa, const double *pb, const double *pc,
                                  const double *pd);
int shewchuk_hooked_insphere_sign(const double *pa, const double *pb, const double *pc,
                                  const double *pd, const double *pe);
}
int shewchuk_hooked_last_stage = -1;
int shewchuk_hooked_last_path = -1;
int shewchuk_hooked_last_integer_tier = -1;
#define SHEWCHUK_STAGE_HIT(stage) (shewchuk_hooked_last_stage = (stage))
#define SHEWCHUK_PATH_HIT(path) (shewchuk_hooked_last_path = (path))
// The integer stage decided: record the stage and the tier.
#define SHEWCHUK_INTEGER_STAGE_HIT()                 \
  (shewchuk_hooked_last_stage = SHEWCHUK_STAGE_I,    \
   shewchuk_hooked_last_integer_tier = shewchuk::detail::integer_stage::last_tier)
#else
#include "shewchuk/predicates.h"
#define SHEWCHUK_STAGE_HIT(stage) ((void)0)
#define SHEWCHUK_PATH_HIT(path) ((void)0)
#define SHEWCHUK_INTEGER_STAGE_HIT() ((void)0)
#endif

// The first filter of a predicate decides almost every call, so it should be
// a small leaf function: no stack frame, no saved registers, a tail call to
// the *adapt() function when the filter fails. The *adapt() functions have
// internal linkage and one caller each, so clang inlines them unless told not
// to, and the predicate then sets up the adaptive stages' stack frame (saved
// registers, a stack probe, a stack-protector check) on every call, including
// the calls the filter decides. Code generation only: the arithmetic is the
// same either way.
//
// insphereadapt() is deliberately not marked. Measured with clang, the
// first filter of insphere is faster when insphereadapt() is inlined into it
// (the compiler then vectorizes the filter), and GCC does not inline it
// either way. Measure before changing that (docs/benchmarking.md).
#if defined(__GNUC__) || defined(__clang__)
#define SHEWCHUK_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define SHEWCHUK_NOINLINE __declspec(noinline)
#else
#define SHEWCHUK_NOINLINE
#endif

namespace {

using shewchuk::detail::ccwerrboundA;
using shewchuk::detail::ccwerrboundB;
using shewchuk::detail::ccwerrboundC;
using shewchuk::detail::iccerrboundA;
using shewchuk::detail::iccerrboundB;
using shewchuk::detail::iccerrboundC;
using shewchuk::detail::isperrboundA;
using shewchuk::detail::isperrboundB;
using shewchuk::detail::isperrboundC;
using shewchuk::detail::o3derrboundA;
using shewchuk::detail::o3derrboundB;
using shewchuk::detail::o3derrboundC;
using shewchuk::detail::resulterrbound;
using shewchuk::detail::splitter;

#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
namespace guard = shewchuk::detail::guard;
namespace integer_stage = shewchuk::detail::integer_stage;

// What every predicate returns if a coordinate is an infinity or a NaN: a
// quiet NaN. (Made from its bits: std::numeric_limits would add symbols to an
// unoptimized library.)
inline double not_a_number() noexcept
{
  const guard::bits64 bits = 0x7ff8000000000000ull;
  double value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

// The domain of the floating-point stages admits coordinates whose lowest set
// bit is as low as 2^(floating_point_min_grid / degree). Its proof
// (docs/proofs/input-domain.md, section 5) needs every product of an
// error-bound constant and a value on the grid 2^floating_point_min_grid to be
// a multiple of the smallest subnormal number, i.e. every constant to be a
// multiple of 2^-error_constant_grid.
constexpr bool is_integer_below_2_to_112(double x) noexcept
{
  // x = high * 2^60 + low with high an integer below 2^52; x is an integer
  // exactly if low is one. Both conversions are in range and exact.
  const double high = static_cast<double>(static_cast<long long>(x * 0x1p-60));
  const double low = x - high * 0x1p60;
  return x >= 0.0 && x < 0x1p112 && low == static_cast<double>(static_cast<long long>(low));
}
constexpr bool on_error_constant_grid(double constant) noexcept
{
  // 2^160, written as a product because the grid is a named constant.
  static_assert(shewchuk::detail::integer_stage::error_constant_grid == 160);
  return is_integer_below_2_to_112(constant * 0x1p160);
}
static_assert(on_error_constant_grid(resulterrbound));
static_assert(on_error_constant_grid(ccwerrboundA) && on_error_constant_grid(ccwerrboundB) &&
              on_error_constant_grid(ccwerrboundC));
static_assert(on_error_constant_grid(o3derrboundA) && on_error_constant_grid(o3derrboundB) &&
              on_error_constant_grid(o3derrboundC));
static_assert(on_error_constant_grid(iccerrboundA) && on_error_constant_grid(iccerrboundB) &&
              on_error_constant_grid(iccerrboundC));
static_assert(on_error_constant_grid(isperrboundA) && on_error_constant_grid(isperrboundB) &&
              on_error_constant_grid(isperrboundC));
// The check has teeth: a constant with a bit below the grid fails it.
static_assert(!on_error_constant_grid(isperrboundC * 0x1p-12));
// The guards rely on every first-filter constant being below 2^-48, so that
// errbound <= permanent / 2^48 (and, for orient2d, detsum >= 2^51 errbound).
static_assert(ccwerrboundA < 0x1p-51 && o3derrboundA < 0x1p-48 && iccerrboundA < 0x1p-48 &&
              isperrboundA < 0x1p-48);
static_assert(ccwerrboundC > 0x1p-104);
#endif

// ---------------------------------------------------------------------------
// The arithmetic macros of predicates.c (lines 145-306), unchanged except for
// REAL -> double. They stay macros in this port so that the operation
// sequence is textually the original's. Each needs certain locals (bvirt, c,
// abig, _i, ...) declared by the calling function, as in 1996.
// ---------------------------------------------------------------------------

#define Absolute(a)  ((a) >= 0.0 ? (a) : -(a))

#define Fast_Two_Sum_Tail(a, b, x, y) \
  bvirt = x - a; \
  y = b - bvirt

#define Fast_Two_Sum(a, b, x, y) \
  x = (double) (a + b); \
  Fast_Two_Sum_Tail(a, b, x, y)

#define Two_Sum_Tail(a, b, x, y) \
  bvirt = (double) (x - a); \
  avirt = x - bvirt; \
  bround = b - bvirt; \
  around = a - avirt; \
  y = around + bround

#define Two_Sum(a, b, x, y) \
  x = (double) (a + b); \
  Two_Sum_Tail(a, b, x, y)

#define Two_Diff_Tail(a, b, x, y) \
  bvirt = (double) (a - x); \
  avirt = x + bvirt; \
  bround = bvirt - b; \
  around = a - avirt; \
  y = around + bround

#define Two_Diff(a, b, x, y) \
  x = (double) (a - b); \
  Two_Diff_Tail(a, b, x, y)

#define Split(a, ahi, alo) \
  c = (double) (splitter * a); \
  abig = (double) (c - a); \
  ahi = c - abig; \
  alo = a - ahi

#define Two_Product_Tail(a, b, x, y) \
  Split(a, ahi, alo); \
  Split(b, bhi, blo); \
  err1 = x - (ahi * bhi); \
  err2 = err1 - (alo * bhi); \
  err3 = err2 - (ahi * blo); \
  y = (alo * blo) - err3

#define Two_Product(a, b, x, y) \
  x = (double) (a * b); \
  Two_Product_Tail(a, b, x, y)

#define Two_Product_Presplit(a, b, bhi, blo, x, y) \
  x = (double) (a * b); \
  Split(a, ahi, alo); \
  err1 = x - (ahi * bhi); \
  err2 = err1 - (alo * bhi); \
  err3 = err2 - (ahi * blo); \
  y = (alo * blo) - err3

#define Square_Tail(a, x, y) \
  Split(a, ahi, alo); \
  err1 = x - (ahi * ahi); \
  err3 = err1 - ((ahi + ahi) * alo); \
  y = (alo * alo) - err3

#define Square(a, x, y) \
  x = (double) (a * a); \
  Square_Tail(a, x, y)

#define Two_One_Sum(a1, a0, b, x2, x1, x0) \
  Two_Sum(a0, b , _i, x0); \
  Two_Sum(a1, _i, x2, x1)

#define Two_One_Diff(a1, a0, b, x2, x1, x0) \
  Two_Diff(a0, b , _i, x0); \
  Two_Sum( a1, _i, x2, x1)

#define Two_Two_Sum(a1, a0, b1, b0, x3, x2, x1, x0) \
  Two_One_Sum(a1, a0, b0, _j, _0, x0); \
  Two_One_Sum(_j, _0, b1, x3, x2, x1)

#define Two_Two_Diff(a1, a0, b1, b0, x3, x2, x1, x0) \
  Two_One_Diff(a1, a0, b0, _j, _0, x0); \
  Two_One_Diff(_j, _0, b1, x3, x2, x1)

#define Two_One_Product(a1, a0, b, x3, x2, x1, x0) \
  Split(b, bhi, blo); \
  Two_Product_Presplit(a0, b, bhi, blo, _i, x0); \
  Two_Product_Presplit(a1, b, bhi, blo, _j, _0); \
  Two_Sum(_i, _0, _k, x1); \
  Fast_Two_Sum(_j, _k, x3, x2)

// ---------------------------------------------------------------------------
// Expansion arithmetic
// ---------------------------------------------------------------------------

/* predicates.c lines 1020-1092: fast_expansion_sum_zeroelim() */
/* h cannot be e or f. */
//
// The one deliberate difference from the original in this file. The original
// advances with `enow = e[++eindex];` (and the same for f) at eight places.
// When that consumes the last component of e it loads e[elen], one element
// past the end of the array: undefined behavior, and reported by
// AddressSanitizer (predicates.c line 1061 and its seven siblings). The
// loaded value is never used: every later use of enow is guarded by
// `eindex < elen`.
//
// Here the index still advances at the same place, but the load only happens
// if the new index is in range:  if (++eindex < elen) enow = e[eindex];
// Invariant (the same as the original's, minus the stray load): whenever
// eindex < elen, enow == e[eindex], and likewise for f. Past the end enow
// keeps its previous value, which is never read: the merge comparison
// `(fnow > enow) == (fnow > -enow)` is evaluated only for the initial e[0],
// f[0] and under `(eindex < elen) && (findex < flen)`, and the two tail loops
// read enow / fnow only under their own index test. No floating-point
// operation is added, removed or reordered, and the loads keep their position
// relative to the stores to h.
int fast_expansion_sum_zeroelim(int elen, const double *e, int flen,
                                const double *f, double *h)
{
  double Q;
  double Qnew;
  double hh;
  double bvirt;
  double avirt, bround, around;
  int eindex, findex, hindex;
  double enow, fnow;

  enow = e[0];
  fnow = f[0];
  eindex = findex = 0;
  if ((fnow > enow) == (fnow > -enow)) {
    Q = enow;
    if (++eindex < elen) enow = e[eindex];
  } else {
    Q = fnow;
    if (++findex < flen) fnow = f[findex];
  }
  hindex = 0;
  if ((eindex < elen) && (findex < flen)) {
    if ((fnow > enow) == (fnow > -enow)) {
      Fast_Two_Sum(enow, Q, Qnew, hh);
      if (++eindex < elen) enow = e[eindex];
    } else {
      Fast_Two_Sum(fnow, Q, Qnew, hh);
      if (++findex < flen) fnow = f[findex];
    }
    Q = Qnew;
    if (hh != 0.0) {
      h[hindex++] = hh;
    }
    while ((eindex < elen) && (findex < flen)) {
      if ((fnow > enow) == (fnow > -enow)) {
        Two_Sum(Q, enow, Qnew, hh);
        if (++eindex < elen) enow = e[eindex];
      } else {
        Two_Sum(Q, fnow, Qnew, hh);
        if (++findex < flen) fnow = f[findex];
      }
      Q = Qnew;
      if (hh != 0.0) {
        h[hindex++] = hh;
      }
    }
  }
  while (eindex < elen) {
    Two_Sum(Q, enow, Qnew, hh);
    if (++eindex < elen) enow = e[eindex];
    Q = Qnew;
    if (hh != 0.0) {
      h[hindex++] = hh;
    }
  }
  while (findex < flen) {
    Two_Sum(Q, fnow, Qnew, hh);
    if (++findex < flen) fnow = f[findex];
    Q = Qnew;
    if (hh != 0.0) {
      h[hindex++] = hh;
    }
  }
  if ((Q != 0.0) || (hindex == 0)) {
    h[hindex++] = Q;
  }
  return hindex;
}

/* predicates.c lines 1292-1333: scale_expansion_zeroelim() */
/* e and h cannot be the same. */
int scale_expansion_zeroelim(int elen, const double *e, double b, double *h)
{
  double Q, sum;
  double hh;
  double product1;
  double product0;
  int eindex, hindex;
  double enow;
  double bvirt;
  double avirt, bround, around;
  double c;
  double abig;
  double ahi, alo, bhi, blo;
  double err1, err2, err3;

  Split(b, bhi, blo);
  Two_Product_Presplit(e[0], b, bhi, blo, Q, hh);
  hindex = 0;
  if (hh != 0) {
    h[hindex++] = hh;
  }
  for (eindex = 1; eindex < elen; eindex++) {
    enow = e[eindex];
    Two_Product_Presplit(enow, b, bhi, blo, product1, product0);
    Two_Sum(Q, product0, sum, hh);
    if (hh != 0) {
      h[hindex++] = hh;
    }
    Fast_Two_Sum(product1, sum, Q, hh);
    if (hh != 0) {
      h[hindex++] = hh;
    }
  }
  if ((Q != 0.0) || (hindex == 0)) {
    h[hindex++] = Q;
  }
  return hindex;
}

/* predicates.c lines 1392-1404: estimate() */
double estimate(int elen, const double *e)
{
  double Q;
  int eindex;

  Q = e[0];
  for (eindex = 1; eindex < elen; eindex++) {
    Q += e[eindex];
  }
  return Q;
}

// ---------------------------------------------------------------------------
// Pairs of doubles. Code generation only.
//
// Stages B and C of orient2d do the same operations on the operands of the
// left and of the right product. Written with scalars, how those are paired
// into two-lane instructions is the compiler's choice, and that choice
// decides what the stages cost (measured: up to 9% between two versions of
// one compiler, and no pairing at all with another). With the compiler's
// generic two-lane vector type the pairing is in the source. Each lane of
// +, - and * is the correctly rounded binary64 operation, as for a double,
// and the flags that forbid contraction apply to it, so every value is the
// one the scalar code computes.
//
// The scalar code remains, for compilers without that type, and it is what
// the build without compiler extensions (SHEWCHUK_INTEGER_STAGE_PORTABLE)
// compiles, so that the tests compare the two on every platform.
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(SHEWCHUK_INTEGER_STAGE_PORTABLE)
#define SHEWCHUK_PREDICATES_PAIRS 1
typedef double Pair __attribute__((vector_size(16)));
static_assert(sizeof(Pair) == 2 * sizeof(double));

// (p[0], p[1])
inline Pair load_pair(const double *p) noexcept
{
  Pair pair;
  std::memcpy(&pair, p, sizeof pair);
  return pair;
}
// (x, y) -> (y, x)
inline Pair swap_pair(Pair pair) noexcept
{
  const Pair swapped = {pair[1], pair[0]};
  return swapped;
}
#endif

// ---------------------------------------------------------------------------
// orient2d
// ---------------------------------------------------------------------------

#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
// The exact sign of orient2d from the integer stage alone, for the calls on
// which no floating-point stage may be used. Never refuses a finite input.
// Takes the coordinates by value, for the reason given at orient2dadapt().
SHEWCHUK_NOINLINE double orient2d_exact(double ax, double ay, double bx, double by, double cx,
                                        double cy)
{
  const double a[2] = {ax, ay}, b[2] = {bx, by}, c[2] = {cx, cy};
  const int sign = integer_stage::orient2d(a, b, c, integer_stage::Request::always_exact);
  if (sign == integer_stage::not_finite) {
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
    return not_a_number();
  }
  SHEWCHUK_INTEGER_STAGE_HIT();
  return sign;
}
#endif

/* predicates.c lines 1535-1617, the part of orient2dadapt() behind stage C:
 * stage D.
 *
 * Stage D starts from the expansion B and the tails of the differences that
 * orient2dadapt() computed. They are computed again here, by the same
 * operations on the same coordinates, hence with the same values: that costs
 * the rare calls that reach stage D a few operations, and lets
 * orient2dadapt() be a function without a stack frame (see there). */
SHEWCHUK_NOINLINE double orient2d_stage_d(double ax, double ay, double bx, double by, double cx,
                                          double cy)
{
  double acx, acy, bcx, bcy;
  double acxtail, acytail, bcxtail, bcytail;
  double detleft, detright;
  double detlefttail, detrighttail;
  double B[4], C1[8], C2[12], D[16];
  double B3;
  int C1length, C2length, Dlength;
  double u[4];
  double u3;
  double s1, t1;
  double s0, t0;

  double bvirt;
  double avirt, bround, around;
  double c;
  double abig;
  double ahi, alo, bhi, blo;
  double err1, err2, err3;
  double _i, _j;
  double _0;

  acx = (double) (ax - cx);
  bcx = (double) (bx - cx);
  acy = (double) (ay - cy);
  bcy = (double) (by - cy);

  Two_Product(acx, bcy, detleft, detlefttail);
  Two_Product(acy, bcx, detright, detrighttail);

  Two_Two_Diff(detleft, detlefttail, detright, detrighttail,
               B3, B[2], B[1], B[0]);
  B[3] = B3;

  Two_Diff_Tail(ax, cx, acx, acxtail);
  Two_Diff_Tail(bx, cx, bcx, bcxtail);
  Two_Diff_Tail(ay, cy, acy, acytail);
  Two_Diff_Tail(by, cy, bcy, bcytail);

  Two_Product(acxtail, bcy, s1, s0);
  Two_Product(acytail, bcx, t1, t0);
  Two_Two_Diff(s1, s0, t1, t0, u3, u[2], u[1], u[0]);
  u[3] = u3;
  C1length = fast_expansion_sum_zeroelim(4, B, 4, u, C1);

  Two_Product(acx, bcytail, s1, s0);
  Two_Product(acy, bcxtail, t1, t0);
  Two_Two_Diff(s1, s0, t1, t0, u3, u[2], u[1], u[0]);
  u[3] = u3;
  C2length = fast_expansion_sum_zeroelim(C1length, C1, 4, u, C2);

  Two_Product(acxtail, bcytail, s1, s0);
  Two_Product(acytail, bcxtail, t1, t0);
  Two_Two_Diff(s1, s0, t1, t0, u3, u[2], u[1], u[0]);
  u[3] = u3;
  Dlength = fast_expansion_sum_zeroelim(C2length, C2, 4, u, D);

  SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_D);
  return(D[Dlength - 1]);
}

/* What follows stage C of orient2d.
 *
 * Not from 1996: the exact integer stage (integer_stage.h), tried before
 * stage D. For orient2d it is here and not directly behind stage A as for
 * the other predicates: stages B and C of orient2d cost less than the
 * integer stage, stage D costs more. It refuses only coordinates that are
 * too wide for its fast tiers and inside the floating-point domain: then
 * stage D runs as in 1996. Stage D is a function of its own so that the
 * calls the integer stage decides do not pay for its arrays. */
SHEWCHUK_NOINLINE double orient2d_after_stage_c(double ax, double ay, double bx, double by,
                                                double cx, double cy)
{
#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  {
    const double a[2] = {ax, ay}, b[2] = {bx, by}, c[2] = {cx, cy};
    const int sign = integer_stage::orient2d(a, b, c);
    if (sign == integer_stage::not_finite) {
      SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
      return not_a_number();
    }
    if (sign != integer_stage::refused) {
      SHEWCHUK_INTEGER_STAGE_HIT();
      return sign;
    }
  }
#endif
  return orient2d_stage_d(ax, ay, bx, by, cx, cy);
}

/* predicates.c lines 1535-1617: orient2dadapt(), stages B, Bx and C. What
 * follows them is orient2d_after_stage_c().
 *
 * Not from 1996 (docs/proofs/input-domain.md, section 4): this function is
 * entered only with finite coordinates and either inside the floating-point
 * domain or with the error bound of the first filter inside the window of
 * shewchuk_orient2d(). In the second case the two products are far from the
 * subnormal range, so stage B is exact unless Split overflows on a huge
 * difference, which makes its estimate a NaN: that is tested for, and the
 * integer stage then decides. Stage C is covered by the same window. Stage D
 * runs only inside the floating-point domain (the integer stage in front of
 * it refuses nothing else).
 *
 * Code generation: this function has no array and passes no pointer on, so
 * that the compiler hands it the six coordinates in registers and gives it
 * no stack frame: no register is saved or restored and nothing is stored.
 * The expansion B is four scalars here, and what stage B returns is
 * estimate(4, B) written out: B[0] + B[1] + B[2] + B[3], added in that
 * order. The functions it ends in take the coordinates by value for the same
 * reason. */
SHEWCHUK_NOINLINE double orient2dadapt(const double *pa, const double *pb, const double *pc,
                     double detsum)
{
  double acxtail, acytail, bcxtail, bcytail;
  double detleft, detright;
  double detlefttail, detrighttail;
  double det, errbound;
  double B0, B1, B2, B3;
  double correction_left, correction_right;

  double bvirt;
  double avirt, bround, around;
  double _i, _j;
  double _0;

#ifdef SHEWCHUK_PREDICATES_PAIRS
  // Two lanes: the left product's operand and the right product's.
  //   ac = (acx, acy), bc = (bcy, bcx), ac * bc = (detleft, detright).
  // Each line is the line of the same name in the macros Two_Product and
  // Two_Diff_Tail, done for both lanes.
  const Pair a = load_pair(pa), c_xy = load_pair(pc);
  const Pair b_yx = swap_pair(load_pair(pb)), c_yx = swap_pair(c_xy);
  const Pair ac = a - c_xy;
  const Pair bc = b_yx - c_yx;
  Pair ac_tail, bc_tail;
  {
    const Pair x = ac * bc;
    // Split(ac), Split(bc)
    Pair c = splitter * ac;
    Pair abig = c - ac;
    const Pair ahi = c - abig;
    const Pair alo = ac - ahi;
    c = splitter * bc;
    abig = c - bc;
    const Pair bhi = c - abig;
    const Pair blo = bc - bhi;
    const Pair err1 = x - (ahi * bhi);
    const Pair err2 = err1 - (alo * bhi);
    const Pair err3 = err2 - (ahi * blo);
    const Pair y = (alo * blo) - err3;
    detleft = x[0], detlefttail = y[0];
    detright = x[1], detrighttail = y[1];
  }
#else
  double acx, acy, bcx, bcy;
  double c;
  double abig;
  double ahi, alo, bhi, blo;
  double err1, err2, err3;

  acx = (double) (pa[0] - pc[0]);
  bcx = (double) (pb[0] - pc[0]);
  acy = (double) (pa[1] - pc[1]);
  bcy = (double) (pb[1] - pc[1]);

  Two_Product(acx, bcy, detleft, detlefttail);
  Two_Product(acy, bcx, detright, detrighttail);
#endif

  Two_Two_Diff(detleft, detlefttail, detright, detrighttail,
               B3, B2, B1, B0);

  det = ((B0 + B1) + B2) + B3;
  errbound = ccwerrboundB * detsum;
#ifdef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_B);
    return det;
  }
#else
  {
    // fabs(det) >= errbound as a comparison of bit patterns (errbound is
    // finite and not negative here), which also sees a det that is not
    // finite.
    const guard::bits64 magnitude = guard::bits_of(det) & guard::magnitude_mask;
    if (magnitude >= guard::infinity_bits) {
      return orient2d_exact(pa[0], pa[1], pb[0], pb[1], pc[0], pc[1]);
    }
    if (magnitude >= guard::bits_of(errbound)) {
      SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_B);
      return det;
    }
  }
#endif

#ifdef SHEWCHUK_PREDICATES_PAIRS
  {
    // Two_Diff_Tail(a, c, ac, ac_tail)
    Pair bvirt2 = a - ac;
    Pair avirt2 = ac + bvirt2;
    Pair bround2 = bvirt2 - c_xy;
    Pair around2 = a - avirt2;
    ac_tail = around2 + bround2;
    // Two_Diff_Tail(b, c, bc, bc_tail)
    bvirt2 = b_yx - bc;
    avirt2 = bc + bvirt2;
    bround2 = bvirt2 - c_yx;
    around2 = b_yx - avirt2;
    bc_tail = around2 + bround2;
  }
  acxtail = ac_tail[0], acytail = ac_tail[1];
  bcytail = bc_tail[0], bcxtail = bc_tail[1];
#else
  Two_Diff_Tail(pa[0], pc[0], acx, acxtail);
  Two_Diff_Tail(pb[0], pc[0], bcx, bcxtail);
  Two_Diff_Tail(pa[1], pc[1], acy, acytail);
  Two_Diff_Tail(pb[1], pc[1], bcy, bcytail);
#endif

  if ((acxtail == 0.0) && (acytail == 0.0)
      && (bcxtail == 0.0) && (bcytail == 0.0)) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_BX);
    return det;
  }

  errbound = ccwerrboundC * detsum + resulterrbound * std::fabs(det);
#ifdef SHEWCHUK_PREDICATES_PAIRS
  {
    // (acx * bcytail + bcy * acxtail, acy * bcxtail + bcx * acytail)
    const Pair correction = ac * bc_tail + bc * ac_tail;
    correction_left = correction[0], correction_right = correction[1];
  }
#else
  correction_left = acx * bcytail + bcy * acxtail;
  correction_right = acy * bcxtail + bcx * acytail;
#endif
  det += correction_left - correction_right;
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_C);
    return det;
  }

  return orient2d_after_stage_c(pa[0], pa[1], pb[0], pb[1], pc[0], pc[1]);
}

#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
// orient2d when the error bound of its first test is outside the window of
// shewchuk_orient2d() and the two products are not zeros with a zero factor
// each: the error bound is tiny, huge, infinite or NaN, or a product
// underflowed to zero. Rare, and not reached from inside the range the 1996
// paper states. Recomputes what the first test computed.
SHEWCHUK_NOINLINE double orient2d_outside_window(const double *pa, const double *pb,
                                                 const double *pc)
{
  const double acx = pa[0] - pc[0], bcy = pb[1] - pc[1];
  const double acy = pa[1] - pc[1], bcx = pb[0] - pc[0];
  const double detleft = acx * bcy, detright = acy * bcx;
  const double det = detleft - detright;
  const double detsum = std::fabs(detleft) + std::fabs(detright);

  SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_AFTER);
  const int sign =
      integer_stage::orient2d(pa, pb, pc, integer_stage::Request::prefer_floating_point);
  if (sign == integer_stage::not_finite) {
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
    return not_a_number();
  }
  if (sign != integer_stage::refused) {
    SHEWCHUK_INTEGER_STAGE_HIT();
    return sign;
  }

  // Inside the floating-point domain no operation of the 1996 code overflows
  // or underflows: its first test and what follows it, as they are.
  if (std::fabs(det) >= ccwerrboundA * detsum) {
    SHEWCHUK_STAGE_HIT(((detleft > 0.0) && (detright > 0.0)) ||
                       ((detleft < 0.0) && (detright < 0.0))
                           ? SHEWCHUK_STAGE_A
                           : SHEWCHUK_STAGE_A0);
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_RECHECKED);
    return det;
  }
  return orient2dadapt(pa, pb, pc, detsum);
}
#endif

}  // namespace

/* predicates.c lines 1619-1653: orient2d().
 *
 * The 1996 code first looks at the signs of the two products: if they differ,
 * or one product is zero, it returns det at once (stage A0); otherwise it
 * forms detsum = |detleft| + |detright| and applies the filter (stage A).
 * Here both are one test without a data-dependent branch on the signs. The
 * returned values are the same bit for bit (docs/exactness.md, "orient2d:
 * the merged first test"):
 *   - products of the same sign: detsum, errbound and the test are the
 *     original's;
 *   - opposite signs, or a zero product: |detleft - detright| is exactly
 *     |detleft| + |detright|, so fabs(det) == detsum bit for bit, and
 *     detsum >= ccwerrboundA * detsum always holds; det is returned, as in
 *     1996.
 * Keep `>=` and keep the two products separately rounded.
 *
 * Not from 1996: the window on the error bound (filter_guard.h,
 * docs/proofs/input-domain.md, section 4). The test above is trusted, and
 * orient2dadapt() is entered, only if
 *   2^orient2d_floor_exponent <= errbound < 2^orient2d_ceiling_exponent;
 * then no product was rounded in the subnormal range in a way that matters
 * and nothing overflowed. Everything else goes to orient2d_outside_window().
 * The window and the test itself compare bit patterns as integers; for a
 * finite, nonnegative errbound and a finite det that is the floating-point
 * comparison fabs(det) >= errbound, and the window makes both finite. Inside
 * the range of the 1996 paper the error bound is in the window unless both
 * products are zero.
 *
 * Code generation: the window and the test are one condition, marked as the
 * likely one, so that the common path falls through to the return and the
 * rare paths branch away. A branch taken on the common path costs this
 * function about a cycle in five (docs/benchmarking.md). */
double shewchuk_orient2d(const double *pa, const double *pb, const double *pc)
{
  double acx, acy, bcx, bcy;
  double detleft, detright, det;
  double detsum, errbound;

  acx = pa[0] - pc[0];
  bcy = pb[1] - pc[1];
  acy = pa[1] - pc[1];
  bcx = pb[0] - pc[0];
  detleft = acx * bcy;
  detright = acy * bcx;
  det = detleft - detright;

  detsum = std::fabs(detleft) + std::fabs(detright);
  errbound = ccwerrboundA * detsum;
#ifdef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  if (std::fabs(det) >= errbound) {
    // Test-only build: report the stage the 1996 code decides this call in.
    SHEWCHUK_STAGE_HIT(((detleft > 0.0) && (detright > 0.0)) ||
                       ((detleft < 0.0) && (detright < 0.0))
                           ? SHEWCHUK_STAGE_A
                           : SHEWCHUK_STAGE_A0);
    return det;
  }

  return orient2dadapt(pa, pb, pc, detsum);
#else
  const guard::bits64 bound = guard::bits_of(errbound);
  const guard::bits64 floor_bits = guard::power_of_two_bits(guard::orient2d_floor_exponent);
  const guard::bits64 ceiling_bits = guard::power_of_two_bits(guard::orient2d_ceiling_exponent);
  // floor <= errbound < ceiling, as one unsigned comparison. A NaN and a
  // negative zero have bit patterns above the ceiling's.
  const bool in_window = bound - floor_bits < ceiling_bits - floor_bits;
  if (in_window & ((guard::bits_of(det) & guard::magnitude_mask) >= bound)) [[likely]] {
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_LEAF);
    // Test-only build: report the stage the 1996 code decides this call in.
    SHEWCHUK_STAGE_HIT(((detleft > 0.0) && (detright > 0.0)) ||
                       ((detleft < 0.0) && (detright < 0.0))
                           ? SHEWCHUK_STAGE_A
                           : SHEWCHUK_STAGE_A0);
    return det;
  }
  if (in_window) {
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_LEAF);
    return orient2dadapt(pa, pb, pc, detsum);
  }
  // Outside the window. The one case that ordinary input reaches (points on
  // an axis-parallel line, repeated points): errbound is +0, so both products
  // are zeros (not NaNs), and each has a factor that is zero. A rounded
  // difference of two doubles is zero only if they are equal, so the
  // determinant is zero, and det, a zero, is returned as in 1996. Products of
  // nonzero differences that underflowed to zero do not pass. (One test
  // without short-circuit evaluation: which factor is the zero one changes
  // from call to call, and four branches would be mispredicted.)
  if ((bound == 0) & ((acx == 0.0) | (bcy == 0.0)) & ((acy == 0.0) | (bcx == 0.0))) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A0);
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_ZERO_PRODUCTS);
    return det;
  }
  return orient2d_outside_window(pa, pb, pc);
#endif
}

namespace {

// ---------------------------------------------------------------------------
// orient3d
// ---------------------------------------------------------------------------

/* predicates.c lines 1886-2289: orient3dadapt() */
SHEWCHUK_NOINLINE double orient3dadapt(const double *pa, const double *pb, const double *pc,
                     const double *pd, double permanent)
{
  double adx, bdx, cdx, ady, bdy, cdy, adz, bdz, cdz;
  double det, errbound;

  double bdxcdy1, cdxbdy1, cdxady1, adxcdy1, adxbdy1, bdxady1;
  double bdxcdy0, cdxbdy0, cdxady0, adxcdy0, adxbdy0, bdxady0;
  double bc[4], ca[4], ab[4];
  double bc3, ca3, ab3;
  double adet[8], bdet[8], cdet[8];
  int alen, blen, clen;
  double abdet[16];
  int ablen;
  double *finnow, *finother, *finswap;
  double fin1[192], fin2[192];
  int finlength;

  double adxtail, bdxtail, cdxtail;
  double adytail, bdytail, cdytail;
  double adztail, bdztail, cdztail;
  double at_blarge, at_clarge;
  double bt_clarge, bt_alarge;
  double ct_alarge, ct_blarge;
  double at_b[4], at_c[4], bt_c[4], bt_a[4], ct_a[4], ct_b[4];
  int at_blen, at_clen, bt_clen, bt_alen, ct_alen, ct_blen;
  double bdxt_cdy1, cdxt_bdy1, cdxt_ady1;
  double adxt_cdy1, adxt_bdy1, bdxt_ady1;
  double bdxt_cdy0, cdxt_bdy0, cdxt_ady0;
  double adxt_cdy0, adxt_bdy0, bdxt_ady0;
  double bdyt_cdx1, cdyt_bdx1, cdyt_adx1;
  double adyt_cdx1, adyt_bdx1, bdyt_adx1;
  double bdyt_cdx0, cdyt_bdx0, cdyt_adx0;
  double adyt_cdx0, adyt_bdx0, bdyt_adx0;
  double bct[8], cat[8], abt[8];
  int bctlen, catlen, abtlen;
  double bdxt_cdyt1, cdxt_bdyt1, cdxt_adyt1;
  double adxt_cdyt1, adxt_bdyt1, bdxt_adyt1;
  double bdxt_cdyt0, cdxt_bdyt0, cdxt_adyt0;
  double adxt_cdyt0, adxt_bdyt0, bdxt_adyt0;
  double u[4], v[12], w[16];
  double u3;
  int vlength, wlength;
  double negate;

  double bvirt;
  double avirt, bround, around;
  double c;
  double abig;
  double ahi, alo, bhi, blo;
  double err1, err2, err3;
  double _i, _j, _k;
  double _0;

  adx = (double) (pa[0] - pd[0]);
  bdx = (double) (pb[0] - pd[0]);
  cdx = (double) (pc[0] - pd[0]);
  ady = (double) (pa[1] - pd[1]);
  bdy = (double) (pb[1] - pd[1]);
  cdy = (double) (pc[1] - pd[1]);
  adz = (double) (pa[2] - pd[2]);
  bdz = (double) (pb[2] - pd[2]);
  cdz = (double) (pc[2] - pd[2]);

  Two_Product(bdx, cdy, bdxcdy1, bdxcdy0);
  Two_Product(cdx, bdy, cdxbdy1, cdxbdy0);
  Two_Two_Diff(bdxcdy1, bdxcdy0, cdxbdy1, cdxbdy0, bc3, bc[2], bc[1], bc[0]);
  bc[3] = bc3;
  alen = scale_expansion_zeroelim(4, bc, adz, adet);

  Two_Product(cdx, ady, cdxady1, cdxady0);
  Two_Product(adx, cdy, adxcdy1, adxcdy0);
  Two_Two_Diff(cdxady1, cdxady0, adxcdy1, adxcdy0, ca3, ca[2], ca[1], ca[0]);
  ca[3] = ca3;
  blen = scale_expansion_zeroelim(4, ca, bdz, bdet);

  Two_Product(adx, bdy, adxbdy1, adxbdy0);
  Two_Product(bdx, ady, bdxady1, bdxady0);
  Two_Two_Diff(adxbdy1, adxbdy0, bdxady1, bdxady0, ab3, ab[2], ab[1], ab[0]);
  ab[3] = ab3;
  clen = scale_expansion_zeroelim(4, ab, cdz, cdet);

  ablen = fast_expansion_sum_zeroelim(alen, adet, blen, bdet, abdet);
  finlength = fast_expansion_sum_zeroelim(ablen, abdet, clen, cdet, fin1);

  det = estimate(finlength, fin1);
  errbound = o3derrboundB * permanent;
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_B);
    return det;
  }

  Two_Diff_Tail(pa[0], pd[0], adx, adxtail);
  Two_Diff_Tail(pb[0], pd[0], bdx, bdxtail);
  Two_Diff_Tail(pc[0], pd[0], cdx, cdxtail);
  Two_Diff_Tail(pa[1], pd[1], ady, adytail);
  Two_Diff_Tail(pb[1], pd[1], bdy, bdytail);
  Two_Diff_Tail(pc[1], pd[1], cdy, cdytail);
  Two_Diff_Tail(pa[2], pd[2], adz, adztail);
  Two_Diff_Tail(pb[2], pd[2], bdz, bdztail);
  Two_Diff_Tail(pc[2], pd[2], cdz, cdztail);

  if ((adxtail == 0.0) && (bdxtail == 0.0) && (cdxtail == 0.0)
      && (adytail == 0.0) && (bdytail == 0.0) && (cdytail == 0.0)
      && (adztail == 0.0) && (bdztail == 0.0) && (cdztail == 0.0)) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_BX);
    return det;
  }

  errbound = o3derrboundC * permanent + resulterrbound * std::fabs(det);
  det += (adz * ((bdx * cdytail + cdy * bdxtail)
                 - (bdy * cdxtail + cdx * bdytail))
          + adztail * (bdx * cdy - bdy * cdx))
       + (bdz * ((cdx * adytail + ady * cdxtail)
                 - (cdy * adxtail + adx * cdytail))
          + bdztail * (cdx * ady - cdy * adx))
       + (cdz * ((adx * bdytail + bdy * adxtail)
                 - (ady * bdxtail + bdx * adytail))
          + cdztail * (adx * bdy - ady * bdx));
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_C);
    return det;
  }

  finnow = fin1;
  finother = fin2;

  if (adxtail == 0.0) {
    if (adytail == 0.0) {
      at_b[0] = 0.0;
      at_blen = 1;
      at_c[0] = 0.0;
      at_clen = 1;
    } else {
      negate = -adytail;
      Two_Product(negate, bdx, at_blarge, at_b[0]);
      at_b[1] = at_blarge;
      at_blen = 2;
      Two_Product(adytail, cdx, at_clarge, at_c[0]);
      at_c[1] = at_clarge;
      at_clen = 2;
    }
  } else {
    if (adytail == 0.0) {
      Two_Product(adxtail, bdy, at_blarge, at_b[0]);
      at_b[1] = at_blarge;
      at_blen = 2;
      negate = -adxtail;
      Two_Product(negate, cdy, at_clarge, at_c[0]);
      at_c[1] = at_clarge;
      at_clen = 2;
    } else {
      Two_Product(adxtail, bdy, adxt_bdy1, adxt_bdy0);
      Two_Product(adytail, bdx, adyt_bdx1, adyt_bdx0);
      Two_Two_Diff(adxt_bdy1, adxt_bdy0, adyt_bdx1, adyt_bdx0,
                   at_blarge, at_b[2], at_b[1], at_b[0]);
      at_b[3] = at_blarge;
      at_blen = 4;
      Two_Product(adytail, cdx, adyt_cdx1, adyt_cdx0);
      Two_Product(adxtail, cdy, adxt_cdy1, adxt_cdy0);
      Two_Two_Diff(adyt_cdx1, adyt_cdx0, adxt_cdy1, adxt_cdy0,
                   at_clarge, at_c[2], at_c[1], at_c[0]);
      at_c[3] = at_clarge;
      at_clen = 4;
    }
  }
  if (bdxtail == 0.0) {
    if (bdytail == 0.0) {
      bt_c[0] = 0.0;
      bt_clen = 1;
      bt_a[0] = 0.0;
      bt_alen = 1;
    } else {
      negate = -bdytail;
      Two_Product(negate, cdx, bt_clarge, bt_c[0]);
      bt_c[1] = bt_clarge;
      bt_clen = 2;
      Two_Product(bdytail, adx, bt_alarge, bt_a[0]);
      bt_a[1] = bt_alarge;
      bt_alen = 2;
    }
  } else {
    if (bdytail == 0.0) {
      Two_Product(bdxtail, cdy, bt_clarge, bt_c[0]);
      bt_c[1] = bt_clarge;
      bt_clen = 2;
      negate = -bdxtail;
      Two_Product(negate, ady, bt_alarge, bt_a[0]);
      bt_a[1] = bt_alarge;
      bt_alen = 2;
    } else {
      Two_Product(bdxtail, cdy, bdxt_cdy1, bdxt_cdy0);
      Two_Product(bdytail, cdx, bdyt_cdx1, bdyt_cdx0);
      Two_Two_Diff(bdxt_cdy1, bdxt_cdy0, bdyt_cdx1, bdyt_cdx0,
                   bt_clarge, bt_c[2], bt_c[1], bt_c[0]);
      bt_c[3] = bt_clarge;
      bt_clen = 4;
      Two_Product(bdytail, adx, bdyt_adx1, bdyt_adx0);
      Two_Product(bdxtail, ady, bdxt_ady1, bdxt_ady0);
      Two_Two_Diff(bdyt_adx1, bdyt_adx0, bdxt_ady1, bdxt_ady0,
                  bt_alarge, bt_a[2], bt_a[1], bt_a[0]);
      bt_a[3] = bt_alarge;
      bt_alen = 4;
    }
  }
  if (cdxtail == 0.0) {
    if (cdytail == 0.0) {
      ct_a[0] = 0.0;
      ct_alen = 1;
      ct_b[0] = 0.0;
      ct_blen = 1;
    } else {
      negate = -cdytail;
      Two_Product(negate, adx, ct_alarge, ct_a[0]);
      ct_a[1] = ct_alarge;
      ct_alen = 2;
      Two_Product(cdytail, bdx, ct_blarge, ct_b[0]);
      ct_b[1] = ct_blarge;
      ct_blen = 2;
    }
  } else {
    if (cdytail == 0.0) {
      Two_Product(cdxtail, ady, ct_alarge, ct_a[0]);
      ct_a[1] = ct_alarge;
      ct_alen = 2;
      negate = -cdxtail;
      Two_Product(negate, bdy, ct_blarge, ct_b[0]);
      ct_b[1] = ct_blarge;
      ct_blen = 2;
    } else {
      Two_Product(cdxtail, ady, cdxt_ady1, cdxt_ady0);
      Two_Product(cdytail, adx, cdyt_adx1, cdyt_adx0);
      Two_Two_Diff(cdxt_ady1, cdxt_ady0, cdyt_adx1, cdyt_adx0,
                   ct_alarge, ct_a[2], ct_a[1], ct_a[0]);
      ct_a[3] = ct_alarge;
      ct_alen = 4;
      Two_Product(cdytail, bdx, cdyt_bdx1, cdyt_bdx0);
      Two_Product(cdxtail, bdy, cdxt_bdy1, cdxt_bdy0);
      Two_Two_Diff(cdyt_bdx1, cdyt_bdx0, cdxt_bdy1, cdxt_bdy0,
                   ct_blarge, ct_b[2], ct_b[1], ct_b[0]);
      ct_b[3] = ct_blarge;
      ct_blen = 4;
    }
  }

  bctlen = fast_expansion_sum_zeroelim(bt_clen, bt_c, ct_blen, ct_b, bct);
  wlength = scale_expansion_zeroelim(bctlen, bct, adz, w);
  finlength = fast_expansion_sum_zeroelim(finlength, finnow, wlength, w,
                                          finother);
  finswap = finnow; finnow = finother; finother = finswap;

  catlen = fast_expansion_sum_zeroelim(ct_alen, ct_a, at_clen, at_c, cat);
  wlength = scale_expansion_zeroelim(catlen, cat, bdz, w);
  finlength = fast_expansion_sum_zeroelim(finlength, finnow, wlength, w,
                                          finother);
  finswap = finnow; finnow = finother; finother = finswap;

  abtlen = fast_expansion_sum_zeroelim(at_blen, at_b, bt_alen, bt_a, abt);
  wlength = scale_expansion_zeroelim(abtlen, abt, cdz, w);
  finlength = fast_expansion_sum_zeroelim(finlength, finnow, wlength, w,
                                          finother);
  finswap = finnow; finnow = finother; finother = finswap;

  if (adztail != 0.0) {
    vlength = scale_expansion_zeroelim(4, bc, adztail, v);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, vlength, v,
                                            finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (bdztail != 0.0) {
    vlength = scale_expansion_zeroelim(4, ca, bdztail, v);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, vlength, v,
                                            finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (cdztail != 0.0) {
    vlength = scale_expansion_zeroelim(4, ab, cdztail, v);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, vlength, v,
                                            finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }

  if (adxtail != 0.0) {
    if (bdytail != 0.0) {
      Two_Product(adxtail, bdytail, adxt_bdyt1, adxt_bdyt0);
      Two_One_Product(adxt_bdyt1, adxt_bdyt0, cdz, u3, u[2], u[1], u[0]);
      u[3] = u3;
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                              finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (cdztail != 0.0) {
        Two_One_Product(adxt_bdyt1, adxt_bdyt0, cdztail, u3, u[2], u[1], u[0]);
        u[3] = u3;
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                                finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
    }
    if (cdytail != 0.0) {
      negate = -adxtail;
      Two_Product(negate, cdytail, adxt_cdyt1, adxt_cdyt0);
      Two_One_Product(adxt_cdyt1, adxt_cdyt0, bdz, u3, u[2], u[1], u[0]);
      u[3] = u3;
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                              finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (bdztail != 0.0) {
        Two_One_Product(adxt_cdyt1, adxt_cdyt0, bdztail, u3, u[2], u[1], u[0]);
        u[3] = u3;
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                                finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
    }
  }
  if (bdxtail != 0.0) {
    if (cdytail != 0.0) {
      Two_Product(bdxtail, cdytail, bdxt_cdyt1, bdxt_cdyt0);
      Two_One_Product(bdxt_cdyt1, bdxt_cdyt0, adz, u3, u[2], u[1], u[0]);
      u[3] = u3;
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                              finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (adztail != 0.0) {
        Two_One_Product(bdxt_cdyt1, bdxt_cdyt0, adztail, u3, u[2], u[1], u[0]);
        u[3] = u3;
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                                finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
    }
    if (adytail != 0.0) {
      negate = -bdxtail;
      Two_Product(negate, adytail, bdxt_adyt1, bdxt_adyt0);
      Two_One_Product(bdxt_adyt1, bdxt_adyt0, cdz, u3, u[2], u[1], u[0]);
      u[3] = u3;
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                              finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (cdztail != 0.0) {
        Two_One_Product(bdxt_adyt1, bdxt_adyt0, cdztail, u3, u[2], u[1], u[0]);
        u[3] = u3;
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                                finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
    }
  }
  if (cdxtail != 0.0) {
    if (adytail != 0.0) {
      Two_Product(cdxtail, adytail, cdxt_adyt1, cdxt_adyt0);
      Two_One_Product(cdxt_adyt1, cdxt_adyt0, bdz, u3, u[2], u[1], u[0]);
      u[3] = u3;
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                              finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (bdztail != 0.0) {
        Two_One_Product(cdxt_adyt1, cdxt_adyt0, bdztail, u3, u[2], u[1], u[0]);
        u[3] = u3;
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                                finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
    }
    if (bdytail != 0.0) {
      negate = -cdxtail;
      Two_Product(negate, bdytail, cdxt_bdyt1, cdxt_bdyt0);
      Two_One_Product(cdxt_bdyt1, cdxt_bdyt0, adz, u3, u[2], u[1], u[0]);
      u[3] = u3;
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                              finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (adztail != 0.0) {
        Two_One_Product(cdxt_bdyt1, cdxt_bdyt0, adztail, u3, u[2], u[1], u[0]);
        u[3] = u3;
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, 4, u,
                                                finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
    }
  }

  if (adztail != 0.0) {
    wlength = scale_expansion_zeroelim(bctlen, bct, adztail, w);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, wlength, w,
                                            finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (bdztail != 0.0) {
    wlength = scale_expansion_zeroelim(catlen, cat, bdztail, w);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, wlength, w,
                                            finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (cdztail != 0.0) {
    wlength = scale_expansion_zeroelim(abtlen, abt, cdztail, w);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, wlength, w,
                                            finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }

  SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_D);
  return finnow[finlength - 1];
}

// What runs when stage A cannot certify the sign: the exact integer stage
// (integer_stage.h), and behind it the adaptive stages of 1996, unchanged.
// Not from 1996: the integer stage decides every input that fits its fast
// tiers AND every input outside the floating-point domain; it refuses, and
// the adaptive stages run, only inside that domain, where no operation of
// theirs overflows or underflows (docs/proofs/input-domain.md, section 5).
// Out of line, so that stage A stays a small leaf function.
SHEWCHUK_NOINLINE
double orient3d_after_filter(const double *pa, const double *pb, const double *pc,
                             const double *pd, double permanent)
{
#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_AFTER);
  const int sign = integer_stage::orient3d(pa, pb, pc, pd);
  if (sign != integer_stage::refused) {
    if (sign == integer_stage::not_finite) {
      SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
      return not_a_number();
    }
    SHEWCHUK_INTEGER_STAGE_HIT();
    return sign;
  }
#endif
  return orient3dadapt(pa, pb, pc, pd, permanent);
}

#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
// What runs when stage A certifies det but its guard does not hold (rare;
// never inlined, so that stage A does not pay for it). Inside the
// floating-point domain no operation of stage A overflowed or underflowed,
// so det is returned after all, as in 1996. Outside, the integer stage
// decides.
SHEWCHUK_NOINLINE double orient3d_guard_failed(const double *pa, const double *pb, const double *pc,
                                               const double *pd, double det)
{
  SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_AFTER);
  const int sign =
      integer_stage::orient3d(pa, pb, pc, pd, integer_stage::Request::prefer_floating_point);
  if (sign == integer_stage::refused) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_RECHECKED);
    return det;
  }
  if (sign == integer_stage::not_finite) {
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
    return not_a_number();
  }
  SHEWCHUK_INTEGER_STAGE_HIT();
  return sign;
}
#endif

}  // namespace

/* predicates.c lines 2291-2334: orient3d() */
double shewchuk_orient3d(const double *pa, const double *pb, const double *pc,
                         const double *pd)
{
  double adx, bdx, cdx, ady, bdy, cdy, adz, bdz, cdz;
  double bdxcdy, cdxbdy, cdxady, adxcdy, adxbdy, bdxady;
  double det;
  double permanent, errbound;

  adx = pa[0] - pd[0];
  bdx = pb[0] - pd[0];
  cdx = pc[0] - pd[0];
  ady = pa[1] - pd[1];
  bdy = pb[1] - pd[1];
  cdy = pc[1] - pd[1];
  adz = pa[2] - pd[2];
  bdz = pb[2] - pd[2];
  cdz = pc[2] - pd[2];

  bdxcdy = bdx * cdy;
  cdxbdy = cdx * bdy;

  cdxady = cdx * ady;
  adxcdy = adx * cdy;

  adxbdy = adx * bdy;
  bdxady = bdx * ady;

  det = adz * (bdxcdy - cdxbdy)
      + bdz * (cdxady - adxcdy)
      + cdz * (adxbdy - bdxady);

  permanent = (std::fabs(bdxcdy) + std::fabs(cdxbdy)) * std::fabs(adz)
            + (std::fabs(cdxady) + std::fabs(adxcdy)) * std::fabs(bdz)
            + (std::fabs(adxbdy) + std::fabs(bdxady)) * std::fabs(cdz);
  errbound = o3derrboundA * permanent;
#ifdef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  if (std::fabs(det) > errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
    return det;
  }
#else
  // Not from 1996: fabs(det) > errbound is tested on the bit patterns, and
  // det is returned only if, in addition, the guard of the filter holds
  // (filter_guard.h): then det, permanent and errbound are what an unbounded
  // exponent range would give, and the 1996 analysis applies. The guard reads
  // bit patterns and compares integers; coordinates are read from memory.
  if (guard::filter_certifies(det, errbound)) {
    const double *qa = pa, *qb = pb, *qc = pc, *qd = pd;
    SHEWCHUK_GUARD_FORGET_ORIGIN(qa);
    SHEWCHUK_GUARD_FORGET_ORIGIN(qb);
    SHEWCHUK_GUARD_FORGET_ORIGIN(qc);
    SHEWCHUK_GUARD_FORGET_ORIGIN(qd);
    // The z coordinates: only the z differences multiply a value that may
    // have been rounded in the subnormal range.
    if (guard::holds(guard::orient3d_guard, errbound, guard::bits_at(qa, 2),
                     guard::bits_at(qb, 2), guard::bits_at(qc, 2), guard::bits_at(qd, 2)))
        [[likely]] {
      SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
      SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_LEAF);
      return det;
    }
    return orient3d_guard_failed(pa, pb, pc, pd, det);
  }
#endif

  return orient3d_after_filter(pa, pb, pc, pd, permanent);
}

namespace {

// ---------------------------------------------------------------------------
// incircle
// ---------------------------------------------------------------------------

/* predicates.c lines 2651-3223: incircleadapt() */
SHEWCHUK_NOINLINE double incircleadapt(const double *pa, const double *pb, const double *pc,
                     const double *pd, double permanent)
{
  double adx, bdx, cdx, ady, bdy, cdy;
  double det, errbound;

  double bdxcdy1, cdxbdy1, cdxady1, adxcdy1, adxbdy1, bdxady1;
  double bdxcdy0, cdxbdy0, cdxady0, adxcdy0, adxbdy0, bdxady0;
  double bc[4], ca[4], ab[4];
  double bc3, ca3, ab3;
  double axbc[8], axxbc[16], aybc[8], ayybc[16], adet[32];
  int axbclen, axxbclen, aybclen, ayybclen, alen;
  double bxca[8], bxxca[16], byca[8], byyca[16], bdet[32];
  int bxcalen, bxxcalen, bycalen, byycalen, blen;
  double cxab[8], cxxab[16], cyab[8], cyyab[16], cdet[32];
  int cxablen, cxxablen, cyablen, cyyablen, clen;
  double abdet[64];
  int ablen;
  double fin1[1152], fin2[1152];
  double *finnow, *finother, *finswap;
  int finlength;

  double adxtail, bdxtail, cdxtail, adytail, bdytail, cdytail;
  double adxadx1, adyady1, bdxbdx1, bdybdy1, cdxcdx1, cdycdy1;
  double adxadx0, adyady0, bdxbdx0, bdybdy0, cdxcdx0, cdycdy0;
  double aa[4], bb[4], cc[4];
  double aa3, bb3, cc3;
  double ti1, tj1;
  double ti0, tj0;
  double u[4], v[4];
  double u3, v3;
  double temp8[8], temp16a[16], temp16b[16], temp16c[16];
  double temp32a[32], temp32b[32], temp48[48], temp64[64];
  int temp8len, temp16alen, temp16blen, temp16clen;
  int temp32alen, temp32blen, temp48len, temp64len;
  double axtbb[8], axtcc[8], aytbb[8], aytcc[8];
  int axtbblen, axtcclen, aytbblen, aytcclen;
  double bxtaa[8], bxtcc[8], bytaa[8], bytcc[8];
  int bxtaalen, bxtcclen, bytaalen, bytcclen;
  double cxtaa[8], cxtbb[8], cytaa[8], cytbb[8];
  int cxtaalen, cxtbblen, cytaalen, cytbblen;
  double axtbc[8], aytbc[8], bxtca[8], bytca[8], cxtab[8], cytab[8];
  /* Initialized only to silence GCC's -Wmaybe-uninitialized: each length is
     set under `if (<tail> != 0.0)` and read under the same condition. */
  int axtbclen = 0, aytbclen = 0, bxtcalen = 0, bytcalen = 0, cxtablen = 0, cytablen = 0;
  double axtbct[16], aytbct[16], bxtcat[16], bytcat[16], cxtabt[16], cytabt[16];
  int axtbctlen, aytbctlen, bxtcatlen, bytcatlen, cxtabtlen, cytabtlen;
  double axtbctt[8], aytbctt[8], bxtcatt[8];
  double bytcatt[8], cxtabtt[8], cytabtt[8];
  int axtbcttlen, aytbcttlen, bxtcattlen, bytcattlen, cxtabttlen, cytabttlen;
  double abt[8], bct[8], cat[8];
  int abtlen, bctlen, catlen;
  double abtt[4], bctt[4], catt[4];
  int abttlen, bcttlen, cattlen;
  double abtt3, bctt3, catt3;
  double negate;

  double bvirt;
  double avirt, bround, around;
  double c;
  double abig;
  double ahi, alo, bhi, blo;
  double err1, err2, err3;
  double _i, _j;
  double _0;

  adx = (double) (pa[0] - pd[0]);
  bdx = (double) (pb[0] - pd[0]);
  cdx = (double) (pc[0] - pd[0]);
  ady = (double) (pa[1] - pd[1]);
  bdy = (double) (pb[1] - pd[1]);
  cdy = (double) (pc[1] - pd[1]);

  Two_Product(bdx, cdy, bdxcdy1, bdxcdy0);
  Two_Product(cdx, bdy, cdxbdy1, cdxbdy0);
  Two_Two_Diff(bdxcdy1, bdxcdy0, cdxbdy1, cdxbdy0, bc3, bc[2], bc[1], bc[0]);
  bc[3] = bc3;
  axbclen = scale_expansion_zeroelim(4, bc, adx, axbc);
  axxbclen = scale_expansion_zeroelim(axbclen, axbc, adx, axxbc);
  aybclen = scale_expansion_zeroelim(4, bc, ady, aybc);
  ayybclen = scale_expansion_zeroelim(aybclen, aybc, ady, ayybc);
  alen = fast_expansion_sum_zeroelim(axxbclen, axxbc, ayybclen, ayybc, adet);

  Two_Product(cdx, ady, cdxady1, cdxady0);
  Two_Product(adx, cdy, adxcdy1, adxcdy0);
  Two_Two_Diff(cdxady1, cdxady0, adxcdy1, adxcdy0, ca3, ca[2], ca[1], ca[0]);
  ca[3] = ca3;
  bxcalen = scale_expansion_zeroelim(4, ca, bdx, bxca);
  bxxcalen = scale_expansion_zeroelim(bxcalen, bxca, bdx, bxxca);
  bycalen = scale_expansion_zeroelim(4, ca, bdy, byca);
  byycalen = scale_expansion_zeroelim(bycalen, byca, bdy, byyca);
  blen = fast_expansion_sum_zeroelim(bxxcalen, bxxca, byycalen, byyca, bdet);

  Two_Product(adx, bdy, adxbdy1, adxbdy0);
  Two_Product(bdx, ady, bdxady1, bdxady0);
  Two_Two_Diff(adxbdy1, adxbdy0, bdxady1, bdxady0, ab3, ab[2], ab[1], ab[0]);
  ab[3] = ab3;
  cxablen = scale_expansion_zeroelim(4, ab, cdx, cxab);
  cxxablen = scale_expansion_zeroelim(cxablen, cxab, cdx, cxxab);
  cyablen = scale_expansion_zeroelim(4, ab, cdy, cyab);
  cyyablen = scale_expansion_zeroelim(cyablen, cyab, cdy, cyyab);
  clen = fast_expansion_sum_zeroelim(cxxablen, cxxab, cyyablen, cyyab, cdet);

  ablen = fast_expansion_sum_zeroelim(alen, adet, blen, bdet, abdet);
  finlength = fast_expansion_sum_zeroelim(ablen, abdet, clen, cdet, fin1);

  det = estimate(finlength, fin1);
  errbound = iccerrboundB * permanent;
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_B);
    return det;
  }

  Two_Diff_Tail(pa[0], pd[0], adx, adxtail);
  Two_Diff_Tail(pa[1], pd[1], ady, adytail);
  Two_Diff_Tail(pb[0], pd[0], bdx, bdxtail);
  Two_Diff_Tail(pb[1], pd[1], bdy, bdytail);
  Two_Diff_Tail(pc[0], pd[0], cdx, cdxtail);
  Two_Diff_Tail(pc[1], pd[1], cdy, cdytail);
  if ((adxtail == 0.0) && (bdxtail == 0.0) && (cdxtail == 0.0)
      && (adytail == 0.0) && (bdytail == 0.0) && (cdytail == 0.0)) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_BX);
    return det;
  }

  errbound = iccerrboundC * permanent + resulterrbound * std::fabs(det);
  det += ((adx * adx + ady * ady) * ((bdx * cdytail + cdy * bdxtail)
                                     - (bdy * cdxtail + cdx * bdytail))
          + 2.0 * (adx * adxtail + ady * adytail) * (bdx * cdy - bdy * cdx))
       + ((bdx * bdx + bdy * bdy) * ((cdx * adytail + ady * cdxtail)
                                     - (cdy * adxtail + adx * cdytail))
          + 2.0 * (bdx * bdxtail + bdy * bdytail) * (cdx * ady - cdy * adx))
       + ((cdx * cdx + cdy * cdy) * ((adx * bdytail + bdy * adxtail)
                                     - (ady * bdxtail + bdx * adytail))
          + 2.0 * (cdx * cdxtail + cdy * cdytail) * (adx * bdy - ady * bdx));
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_C);
    return det;
  }

  finnow = fin1;
  finother = fin2;

  if ((bdxtail != 0.0) || (bdytail != 0.0)
      || (cdxtail != 0.0) || (cdytail != 0.0)) {
    Square(adx, adxadx1, adxadx0);
    Square(ady, adyady1, adyady0);
    Two_Two_Sum(adxadx1, adxadx0, adyady1, adyady0, aa3, aa[2], aa[1], aa[0]);
    aa[3] = aa3;
  }
  if ((cdxtail != 0.0) || (cdytail != 0.0)
      || (adxtail != 0.0) || (adytail != 0.0)) {
    Square(bdx, bdxbdx1, bdxbdx0);
    Square(bdy, bdybdy1, bdybdy0);
    Two_Two_Sum(bdxbdx1, bdxbdx0, bdybdy1, bdybdy0, bb3, bb[2], bb[1], bb[0]);
    bb[3] = bb3;
  }
  if ((adxtail != 0.0) || (adytail != 0.0)
      || (bdxtail != 0.0) || (bdytail != 0.0)) {
    Square(cdx, cdxcdx1, cdxcdx0);
    Square(cdy, cdycdy1, cdycdy0);
    Two_Two_Sum(cdxcdx1, cdxcdx0, cdycdy1, cdycdy0, cc3, cc[2], cc[1], cc[0]);
    cc[3] = cc3;
  }

  if (adxtail != 0.0) {
    axtbclen = scale_expansion_zeroelim(4, bc, adxtail, axtbc);
    temp16alen = scale_expansion_zeroelim(axtbclen, axtbc, 2.0 * adx,
                                          temp16a);

    axtcclen = scale_expansion_zeroelim(4, cc, adxtail, axtcc);
    temp16blen = scale_expansion_zeroelim(axtcclen, axtcc, bdy, temp16b);

    axtbblen = scale_expansion_zeroelim(4, bb, adxtail, axtbb);
    temp16clen = scale_expansion_zeroelim(axtbblen, axtbb, -cdy, temp16c);

    temp32alen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                            temp16blen, temp16b, temp32a);
    temp48len = fast_expansion_sum_zeroelim(temp16clen, temp16c,
                                            temp32alen, temp32a, temp48);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                            temp48, finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (adytail != 0.0) {
    aytbclen = scale_expansion_zeroelim(4, bc, adytail, aytbc);
    temp16alen = scale_expansion_zeroelim(aytbclen, aytbc, 2.0 * ady,
                                          temp16a);

    aytbblen = scale_expansion_zeroelim(4, bb, adytail, aytbb);
    temp16blen = scale_expansion_zeroelim(aytbblen, aytbb, cdx, temp16b);

    aytcclen = scale_expansion_zeroelim(4, cc, adytail, aytcc);
    temp16clen = scale_expansion_zeroelim(aytcclen, aytcc, -bdx, temp16c);

    temp32alen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                            temp16blen, temp16b, temp32a);
    temp48len = fast_expansion_sum_zeroelim(temp16clen, temp16c,
                                            temp32alen, temp32a, temp48);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                            temp48, finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (bdxtail != 0.0) {
    bxtcalen = scale_expansion_zeroelim(4, ca, bdxtail, bxtca);
    temp16alen = scale_expansion_zeroelim(bxtcalen, bxtca, 2.0 * bdx,
                                          temp16a);

    bxtaalen = scale_expansion_zeroelim(4, aa, bdxtail, bxtaa);
    temp16blen = scale_expansion_zeroelim(bxtaalen, bxtaa, cdy, temp16b);

    bxtcclen = scale_expansion_zeroelim(4, cc, bdxtail, bxtcc);
    temp16clen = scale_expansion_zeroelim(bxtcclen, bxtcc, -ady, temp16c);

    temp32alen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                            temp16blen, temp16b, temp32a);
    temp48len = fast_expansion_sum_zeroelim(temp16clen, temp16c,
                                            temp32alen, temp32a, temp48);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                            temp48, finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (bdytail != 0.0) {
    bytcalen = scale_expansion_zeroelim(4, ca, bdytail, bytca);
    temp16alen = scale_expansion_zeroelim(bytcalen, bytca, 2.0 * bdy,
                                          temp16a);

    bytcclen = scale_expansion_zeroelim(4, cc, bdytail, bytcc);
    temp16blen = scale_expansion_zeroelim(bytcclen, bytcc, adx, temp16b);

    bytaalen = scale_expansion_zeroelim(4, aa, bdytail, bytaa);
    temp16clen = scale_expansion_zeroelim(bytaalen, bytaa, -cdx, temp16c);

    temp32alen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                            temp16blen, temp16b, temp32a);
    temp48len = fast_expansion_sum_zeroelim(temp16clen, temp16c,
                                            temp32alen, temp32a, temp48);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                            temp48, finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (cdxtail != 0.0) {
    cxtablen = scale_expansion_zeroelim(4, ab, cdxtail, cxtab);
    temp16alen = scale_expansion_zeroelim(cxtablen, cxtab, 2.0 * cdx,
                                          temp16a);

    cxtbblen = scale_expansion_zeroelim(4, bb, cdxtail, cxtbb);
    temp16blen = scale_expansion_zeroelim(cxtbblen, cxtbb, ady, temp16b);

    cxtaalen = scale_expansion_zeroelim(4, aa, cdxtail, cxtaa);
    temp16clen = scale_expansion_zeroelim(cxtaalen, cxtaa, -bdy, temp16c);

    temp32alen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                            temp16blen, temp16b, temp32a);
    temp48len = fast_expansion_sum_zeroelim(temp16clen, temp16c,
                                            temp32alen, temp32a, temp48);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                            temp48, finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }
  if (cdytail != 0.0) {
    cytablen = scale_expansion_zeroelim(4, ab, cdytail, cytab);
    temp16alen = scale_expansion_zeroelim(cytablen, cytab, 2.0 * cdy,
                                          temp16a);

    cytaalen = scale_expansion_zeroelim(4, aa, cdytail, cytaa);
    temp16blen = scale_expansion_zeroelim(cytaalen, cytaa, bdx, temp16b);

    cytbblen = scale_expansion_zeroelim(4, bb, cdytail, cytbb);
    temp16clen = scale_expansion_zeroelim(cytbblen, cytbb, -adx, temp16c);

    temp32alen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                            temp16blen, temp16b, temp32a);
    temp48len = fast_expansion_sum_zeroelim(temp16clen, temp16c,
                                            temp32alen, temp32a, temp48);
    finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                            temp48, finother);
    finswap = finnow; finnow = finother; finother = finswap;
  }

  if ((adxtail != 0.0) || (adytail != 0.0)) {
    if ((bdxtail != 0.0) || (bdytail != 0.0)
        || (cdxtail != 0.0) || (cdytail != 0.0)) {
      Two_Product(bdxtail, cdy, ti1, ti0);
      Two_Product(bdx, cdytail, tj1, tj0);
      Two_Two_Sum(ti1, ti0, tj1, tj0, u3, u[2], u[1], u[0]);
      u[3] = u3;
      negate = -bdy;
      Two_Product(cdxtail, negate, ti1, ti0);
      negate = -bdytail;
      Two_Product(cdx, negate, tj1, tj0);
      Two_Two_Sum(ti1, ti0, tj1, tj0, v3, v[2], v[1], v[0]);
      v[3] = v3;
      bctlen = fast_expansion_sum_zeroelim(4, u, 4, v, bct);

      Two_Product(bdxtail, cdytail, ti1, ti0);
      Two_Product(cdxtail, bdytail, tj1, tj0);
      Two_Two_Diff(ti1, ti0, tj1, tj0, bctt3, bctt[2], bctt[1], bctt[0]);
      bctt[3] = bctt3;
      bcttlen = 4;
    } else {
      bct[0] = 0.0;
      bctlen = 1;
      bctt[0] = 0.0;
      bcttlen = 1;
    }

    if (adxtail != 0.0) {
      temp16alen = scale_expansion_zeroelim(axtbclen, axtbc, adxtail, temp16a);
      axtbctlen = scale_expansion_zeroelim(bctlen, bct, adxtail, axtbct);
      temp32alen = scale_expansion_zeroelim(axtbctlen, axtbct, 2.0 * adx,
                                            temp32a);
      temp48len = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp32alen, temp32a, temp48);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                              temp48, finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (bdytail != 0.0) {
        temp8len = scale_expansion_zeroelim(4, cc, adxtail, temp8);
        temp16alen = scale_expansion_zeroelim(temp8len, temp8, bdytail,
                                              temp16a);
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp16alen,
                                                temp16a, finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
      if (cdytail != 0.0) {
        temp8len = scale_expansion_zeroelim(4, bb, -adxtail, temp8);
        temp16alen = scale_expansion_zeroelim(temp8len, temp8, cdytail,
                                              temp16a);
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp16alen,
                                                temp16a, finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }

      temp32alen = scale_expansion_zeroelim(axtbctlen, axtbct, adxtail,
                                            temp32a);
      axtbcttlen = scale_expansion_zeroelim(bcttlen, bctt, adxtail, axtbctt);
      temp16alen = scale_expansion_zeroelim(axtbcttlen, axtbctt, 2.0 * adx,
                                            temp16a);
      temp16blen = scale_expansion_zeroelim(axtbcttlen, axtbctt, adxtail,
                                            temp16b);
      temp32blen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp16blen, temp16b, temp32b);
      temp64len = fast_expansion_sum_zeroelim(temp32alen, temp32a,
                                              temp32blen, temp32b, temp64);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp64len,
                                              temp64, finother);
      finswap = finnow; finnow = finother; finother = finswap;
    }
    if (adytail != 0.0) {
      temp16alen = scale_expansion_zeroelim(aytbclen, aytbc, adytail, temp16a);
      aytbctlen = scale_expansion_zeroelim(bctlen, bct, adytail, aytbct);
      temp32alen = scale_expansion_zeroelim(aytbctlen, aytbct, 2.0 * ady,
                                            temp32a);
      temp48len = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp32alen, temp32a, temp48);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                              temp48, finother);
      finswap = finnow; finnow = finother; finother = finswap;


      temp32alen = scale_expansion_zeroelim(aytbctlen, aytbct, adytail,
                                            temp32a);
      aytbcttlen = scale_expansion_zeroelim(bcttlen, bctt, adytail, aytbctt);
      temp16alen = scale_expansion_zeroelim(aytbcttlen, aytbctt, 2.0 * ady,
                                            temp16a);
      temp16blen = scale_expansion_zeroelim(aytbcttlen, aytbctt, adytail,
                                            temp16b);
      temp32blen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp16blen, temp16b, temp32b);
      temp64len = fast_expansion_sum_zeroelim(temp32alen, temp32a,
                                              temp32blen, temp32b, temp64);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp64len,
                                              temp64, finother);
      finswap = finnow; finnow = finother; finother = finswap;
    }
  }
  if ((bdxtail != 0.0) || (bdytail != 0.0)) {
    if ((cdxtail != 0.0) || (cdytail != 0.0)
        || (adxtail != 0.0) || (adytail != 0.0)) {
      Two_Product(cdxtail, ady, ti1, ti0);
      Two_Product(cdx, adytail, tj1, tj0);
      Two_Two_Sum(ti1, ti0, tj1, tj0, u3, u[2], u[1], u[0]);
      u[3] = u3;
      negate = -cdy;
      Two_Product(adxtail, negate, ti1, ti0);
      negate = -cdytail;
      Two_Product(adx, negate, tj1, tj0);
      Two_Two_Sum(ti1, ti0, tj1, tj0, v3, v[2], v[1], v[0]);
      v[3] = v3;
      catlen = fast_expansion_sum_zeroelim(4, u, 4, v, cat);

      Two_Product(cdxtail, adytail, ti1, ti0);
      Two_Product(adxtail, cdytail, tj1, tj0);
      Two_Two_Diff(ti1, ti0, tj1, tj0, catt3, catt[2], catt[1], catt[0]);
      catt[3] = catt3;
      cattlen = 4;
    } else {
      cat[0] = 0.0;
      catlen = 1;
      catt[0] = 0.0;
      cattlen = 1;
    }

    if (bdxtail != 0.0) {
      temp16alen = scale_expansion_zeroelim(bxtcalen, bxtca, bdxtail, temp16a);
      bxtcatlen = scale_expansion_zeroelim(catlen, cat, bdxtail, bxtcat);
      temp32alen = scale_expansion_zeroelim(bxtcatlen, bxtcat, 2.0 * bdx,
                                            temp32a);
      temp48len = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp32alen, temp32a, temp48);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                              temp48, finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (cdytail != 0.0) {
        temp8len = scale_expansion_zeroelim(4, aa, bdxtail, temp8);
        temp16alen = scale_expansion_zeroelim(temp8len, temp8, cdytail,
                                              temp16a);
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp16alen,
                                                temp16a, finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
      if (adytail != 0.0) {
        temp8len = scale_expansion_zeroelim(4, cc, -bdxtail, temp8);
        temp16alen = scale_expansion_zeroelim(temp8len, temp8, adytail,
                                              temp16a);
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp16alen,
                                                temp16a, finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }

      temp32alen = scale_expansion_zeroelim(bxtcatlen, bxtcat, bdxtail,
                                            temp32a);
      bxtcattlen = scale_expansion_zeroelim(cattlen, catt, bdxtail, bxtcatt);
      temp16alen = scale_expansion_zeroelim(bxtcattlen, bxtcatt, 2.0 * bdx,
                                            temp16a);
      temp16blen = scale_expansion_zeroelim(bxtcattlen, bxtcatt, bdxtail,
                                            temp16b);
      temp32blen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp16blen, temp16b, temp32b);
      temp64len = fast_expansion_sum_zeroelim(temp32alen, temp32a,
                                              temp32blen, temp32b, temp64);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp64len,
                                              temp64, finother);
      finswap = finnow; finnow = finother; finother = finswap;
    }
    if (bdytail != 0.0) {
      temp16alen = scale_expansion_zeroelim(bytcalen, bytca, bdytail, temp16a);
      bytcatlen = scale_expansion_zeroelim(catlen, cat, bdytail, bytcat);
      temp32alen = scale_expansion_zeroelim(bytcatlen, bytcat, 2.0 * bdy,
                                            temp32a);
      temp48len = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp32alen, temp32a, temp48);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                              temp48, finother);
      finswap = finnow; finnow = finother; finother = finswap;


      temp32alen = scale_expansion_zeroelim(bytcatlen, bytcat, bdytail,
                                            temp32a);
      bytcattlen = scale_expansion_zeroelim(cattlen, catt, bdytail, bytcatt);
      temp16alen = scale_expansion_zeroelim(bytcattlen, bytcatt, 2.0 * bdy,
                                            temp16a);
      temp16blen = scale_expansion_zeroelim(bytcattlen, bytcatt, bdytail,
                                            temp16b);
      temp32blen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp16blen, temp16b, temp32b);
      temp64len = fast_expansion_sum_zeroelim(temp32alen, temp32a,
                                              temp32blen, temp32b, temp64);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp64len,
                                              temp64, finother);
      finswap = finnow; finnow = finother; finother = finswap;
    }
  }
  if ((cdxtail != 0.0) || (cdytail != 0.0)) {
    if ((adxtail != 0.0) || (adytail != 0.0)
        || (bdxtail != 0.0) || (bdytail != 0.0)) {
      Two_Product(adxtail, bdy, ti1, ti0);
      Two_Product(adx, bdytail, tj1, tj0);
      Two_Two_Sum(ti1, ti0, tj1, tj0, u3, u[2], u[1], u[0]);
      u[3] = u3;
      negate = -ady;
      Two_Product(bdxtail, negate, ti1, ti0);
      negate = -adytail;
      Two_Product(bdx, negate, tj1, tj0);
      Two_Two_Sum(ti1, ti0, tj1, tj0, v3, v[2], v[1], v[0]);
      v[3] = v3;
      abtlen = fast_expansion_sum_zeroelim(4, u, 4, v, abt);

      Two_Product(adxtail, bdytail, ti1, ti0);
      Two_Product(bdxtail, adytail, tj1, tj0);
      Two_Two_Diff(ti1, ti0, tj1, tj0, abtt3, abtt[2], abtt[1], abtt[0]);
      abtt[3] = abtt3;
      abttlen = 4;
    } else {
      abt[0] = 0.0;
      abtlen = 1;
      abtt[0] = 0.0;
      abttlen = 1;
    }

    if (cdxtail != 0.0) {
      temp16alen = scale_expansion_zeroelim(cxtablen, cxtab, cdxtail, temp16a);
      cxtabtlen = scale_expansion_zeroelim(abtlen, abt, cdxtail, cxtabt);
      temp32alen = scale_expansion_zeroelim(cxtabtlen, cxtabt, 2.0 * cdx,
                                            temp32a);
      temp48len = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp32alen, temp32a, temp48);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                              temp48, finother);
      finswap = finnow; finnow = finother; finother = finswap;
      if (adytail != 0.0) {
        temp8len = scale_expansion_zeroelim(4, bb, cdxtail, temp8);
        temp16alen = scale_expansion_zeroelim(temp8len, temp8, adytail,
                                              temp16a);
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp16alen,
                                                temp16a, finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }
      if (bdytail != 0.0) {
        temp8len = scale_expansion_zeroelim(4, aa, -cdxtail, temp8);
        temp16alen = scale_expansion_zeroelim(temp8len, temp8, bdytail,
                                              temp16a);
        finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp16alen,
                                                temp16a, finother);
        finswap = finnow; finnow = finother; finother = finswap;
      }

      temp32alen = scale_expansion_zeroelim(cxtabtlen, cxtabt, cdxtail,
                                            temp32a);
      cxtabttlen = scale_expansion_zeroelim(abttlen, abtt, cdxtail, cxtabtt);
      temp16alen = scale_expansion_zeroelim(cxtabttlen, cxtabtt, 2.0 * cdx,
                                            temp16a);
      temp16blen = scale_expansion_zeroelim(cxtabttlen, cxtabtt, cdxtail,
                                            temp16b);
      temp32blen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp16blen, temp16b, temp32b);
      temp64len = fast_expansion_sum_zeroelim(temp32alen, temp32a,
                                              temp32blen, temp32b, temp64);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp64len,
                                              temp64, finother);
      finswap = finnow; finnow = finother; finother = finswap;
    }
    if (cdytail != 0.0) {
      temp16alen = scale_expansion_zeroelim(cytablen, cytab, cdytail, temp16a);
      cytabtlen = scale_expansion_zeroelim(abtlen, abt, cdytail, cytabt);
      temp32alen = scale_expansion_zeroelim(cytabtlen, cytabt, 2.0 * cdy,
                                            temp32a);
      temp48len = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp32alen, temp32a, temp48);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp48len,
                                              temp48, finother);
      finswap = finnow; finnow = finother; finother = finswap;


      temp32alen = scale_expansion_zeroelim(cytabtlen, cytabt, cdytail,
                                            temp32a);
      cytabttlen = scale_expansion_zeroelim(abttlen, abtt, cdytail, cytabtt);
      temp16alen = scale_expansion_zeroelim(cytabttlen, cytabtt, 2.0 * cdy,
                                            temp16a);
      temp16blen = scale_expansion_zeroelim(cytabttlen, cytabtt, cdytail,
                                            temp16b);
      temp32blen = fast_expansion_sum_zeroelim(temp16alen, temp16a,
                                              temp16blen, temp16b, temp32b);
      temp64len = fast_expansion_sum_zeroelim(temp32alen, temp32a,
                                              temp32blen, temp32b, temp64);
      finlength = fast_expansion_sum_zeroelim(finlength, finnow, temp64len,
                                              temp64, finother);
      finswap = finnow; finnow = finother; finother = finswap;
    }
  }

  SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_D);
  return finnow[finlength - 1];
}

// What runs when stage A cannot certify the sign: the exact integer stage
// (integer_stage.h), and behind it the adaptive stages of 1996, unchanged.
// Not from 1996: the integer stage decides every input that fits its fast
// tiers AND every input outside the floating-point domain; it refuses, and
// the adaptive stages run, only inside that domain, where no operation of
// theirs overflows or underflows (docs/proofs/input-domain.md, section 5).
// Out of line, so that stage A stays a small leaf function.
SHEWCHUK_NOINLINE
double incircle_after_filter(const double *pa, const double *pb, const double *pc,
                             const double *pd, double permanent)
{
#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_AFTER);
  const int sign = integer_stage::incircle(pa, pb, pc, pd);
  if (sign != integer_stage::refused) {
    if (sign == integer_stage::not_finite) {
      SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
      return not_a_number();
    }
    SHEWCHUK_INTEGER_STAGE_HIT();
    return sign;
  }
#endif
  return incircleadapt(pa, pb, pc, pd, permanent);
}

#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
// What runs when stage A certifies det but its guard does not hold (rare;
// never inlined, so that stage A does not pay for it). Inside the
// floating-point domain no operation of stage A overflowed or underflowed,
// so det is returned after all, as in 1996. Outside, the integer stage
// decides.
SHEWCHUK_NOINLINE double incircle_guard_failed(const double *pa, const double *pb, const double *pc,
                                               const double *pd, double det)
{
  SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_AFTER);
  const int sign =
      integer_stage::incircle(pa, pb, pc, pd, integer_stage::Request::prefer_floating_point);
  if (sign == integer_stage::refused) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_RECHECKED);
    return det;
  }
  if (sign == integer_stage::not_finite) {
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
    return not_a_number();
  }
  SHEWCHUK_INTEGER_STAGE_HIT();
  return sign;
}
#endif

}  // namespace

/* predicates.c lines 3225-3269: incircle() */
double shewchuk_incircle(const double *pa, const double *pb, const double *pc,
                         const double *pd)
{
  double adx, bdx, cdx, ady, bdy, cdy;
  double bdxcdy, cdxbdy, cdxady, adxcdy, adxbdy, bdxady;
  double alift, blift, clift;
  double det;
  double permanent, errbound;

  adx = pa[0] - pd[0];
  bdx = pb[0] - pd[0];
  cdx = pc[0] - pd[0];
  ady = pa[1] - pd[1];
  bdy = pb[1] - pd[1];
  cdy = pc[1] - pd[1];

  bdxcdy = bdx * cdy;
  cdxbdy = cdx * bdy;
  alift = adx * adx + ady * ady;

  cdxady = cdx * ady;
  adxcdy = adx * cdy;
  blift = bdx * bdx + bdy * bdy;

  adxbdy = adx * bdy;
  bdxady = bdx * ady;
  clift = cdx * cdx + cdy * cdy;

  det = alift * (bdxcdy - cdxbdy)
      + blift * (cdxady - adxcdy)
      + clift * (adxbdy - bdxady);

  permanent = (std::fabs(bdxcdy) + std::fabs(cdxbdy)) * alift
            + (std::fabs(cdxady) + std::fabs(adxcdy)) * blift
            + (std::fabs(adxbdy) + std::fabs(bdxady)) * clift;
  errbound = iccerrboundA * permanent;
#ifdef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  if (std::fabs(det) > errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
    return det;
  }
#else
  // Not from 1996: fabs(det) > errbound is tested on the bit patterns, and
  // det is returned only if, in addition, the guard of the filter holds
  // (filter_guard.h): then det, permanent and errbound are what an unbounded
  // exponent range would give, and the 1996 analysis applies. The guard reads
  // bit patterns and compares integers; coordinates are read from memory.
  if (guard::filter_certifies(det, errbound)) {
    const double *qa = pa, *qb = pb, *qc = pc, *qd = pd;
    SHEWCHUK_GUARD_FORGET_ORIGIN(qa);
    SHEWCHUK_GUARD_FORGET_ORIGIN(qb);
    SHEWCHUK_GUARD_FORGET_ORIGIN(qc);
    SHEWCHUK_GUARD_FORGET_ORIGIN(qd);
    if (guard::holds(guard::incircle_guard, errbound, guard::bits_at(qa, 0),
                     guard::bits_at(qa, 1), guard::bits_at(qb, 0), guard::bits_at(qb, 1),
                     guard::bits_at(qc, 0), guard::bits_at(qc, 1), guard::bits_at(qd, 0),
                     guard::bits_at(qd, 1))) [[likely]] {
      SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
      SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_LEAF);
      return det;
    }
    return incircle_guard_failed(pa, pb, pc, pd, det);
  }
#endif

  return incircle_after_filter(pa, pb, pc, pd, permanent);
}

namespace {

// ---------------------------------------------------------------------------
// insphere
// ---------------------------------------------------------------------------

/* predicates.c lines 3346-3601: insphereexact() */
double insphereexact(const double *pa, const double *pb, const double *pc,
                     const double *pd, const double *pe)
{
  double axby1, bxcy1, cxdy1, dxey1, exay1;
  double bxay1, cxby1, dxcy1, exdy1, axey1;
  double axcy1, bxdy1, cxey1, dxay1, exby1;
  double cxay1, dxby1, excy1, axdy1, bxey1;
  double axby0, bxcy0, cxdy0, dxey0, exay0;
  double bxay0, cxby0, dxcy0, exdy0, axey0;
  double axcy0, bxdy0, cxey0, dxay0, exby0;
  double cxay0, dxby0, excy0, axdy0, bxey0;
  double ab[4], bc[4], cd[4], de[4], ea[4];
  double ac[4], bd[4], ce[4], da[4], eb[4];
  double temp8a[8], temp8b[8], temp16[16];
  int temp8alen, temp8blen, temp16len;
  double abc[24], bcd[24], cde[24], dea[24], eab[24];
  double abd[24], bce[24], cda[24], deb[24], eac[24];
  int abclen, bcdlen, cdelen, dealen, eablen;
  int abdlen, bcelen, cdalen, deblen, eaclen;
  double temp48a[48], temp48b[48];
  int temp48alen, temp48blen;
  double abcd[96], bcde[96], cdea[96], deab[96], eabc[96];
  int abcdlen, bcdelen, cdealen, deablen, eabclen;
  double temp192[192];
  double det384x[384], det384y[384], det384z[384];
  int xlen, ylen, zlen;
  double detxy[768];
  int xylen;
  double adet[1152], bdet[1152], cdet[1152], ddet[1152], edet[1152];
  int alen, blen, clen, dlen, elen;
  double abdet[2304], cddet[2304], cdedet[3456];
  int ablen, cdlen;
  double deter[5760];
  int deterlen;
  int i;

  double bvirt;
  double avirt, bround, around;
  double c;
  double abig;
  double ahi, alo, bhi, blo;
  double err1, err2, err3;
  double _i, _j;
  double _0;

  Two_Product(pa[0], pb[1], axby1, axby0);
  Two_Product(pb[0], pa[1], bxay1, bxay0);
  Two_Two_Diff(axby1, axby0, bxay1, bxay0, ab[3], ab[2], ab[1], ab[0]);

  Two_Product(pb[0], pc[1], bxcy1, bxcy0);
  Two_Product(pc[0], pb[1], cxby1, cxby0);
  Two_Two_Diff(bxcy1, bxcy0, cxby1, cxby0, bc[3], bc[2], bc[1], bc[0]);

  Two_Product(pc[0], pd[1], cxdy1, cxdy0);
  Two_Product(pd[0], pc[1], dxcy1, dxcy0);
  Two_Two_Diff(cxdy1, cxdy0, dxcy1, dxcy0, cd[3], cd[2], cd[1], cd[0]);

  Two_Product(pd[0], pe[1], dxey1, dxey0);
  Two_Product(pe[0], pd[1], exdy1, exdy0);
  Two_Two_Diff(dxey1, dxey0, exdy1, exdy0, de[3], de[2], de[1], de[0]);

  Two_Product(pe[0], pa[1], exay1, exay0);
  Two_Product(pa[0], pe[1], axey1, axey0);
  Two_Two_Diff(exay1, exay0, axey1, axey0, ea[3], ea[2], ea[1], ea[0]);

  Two_Product(pa[0], pc[1], axcy1, axcy0);
  Two_Product(pc[0], pa[1], cxay1, cxay0);
  Two_Two_Diff(axcy1, axcy0, cxay1, cxay0, ac[3], ac[2], ac[1], ac[0]);

  Two_Product(pb[0], pd[1], bxdy1, bxdy0);
  Two_Product(pd[0], pb[1], dxby1, dxby0);
  Two_Two_Diff(bxdy1, bxdy0, dxby1, dxby0, bd[3], bd[2], bd[1], bd[0]);

  Two_Product(pc[0], pe[1], cxey1, cxey0);
  Two_Product(pe[0], pc[1], excy1, excy0);
  Two_Two_Diff(cxey1, cxey0, excy1, excy0, ce[3], ce[2], ce[1], ce[0]);

  Two_Product(pd[0], pa[1], dxay1, dxay0);
  Two_Product(pa[0], pd[1], axdy1, axdy0);
  Two_Two_Diff(dxay1, dxay0, axdy1, axdy0, da[3], da[2], da[1], da[0]);

  Two_Product(pe[0], pb[1], exby1, exby0);
  Two_Product(pb[0], pe[1], bxey1, bxey0);
  Two_Two_Diff(exby1, exby0, bxey1, bxey0, eb[3], eb[2], eb[1], eb[0]);

  temp8alen = scale_expansion_zeroelim(4, bc, pa[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, ac, -pb[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, ab, pc[2], temp8a);
  abclen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       abc);

  temp8alen = scale_expansion_zeroelim(4, cd, pb[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, bd, -pc[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, bc, pd[2], temp8a);
  bcdlen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       bcd);

  temp8alen = scale_expansion_zeroelim(4, de, pc[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, ce, -pd[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, cd, pe[2], temp8a);
  cdelen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       cde);

  temp8alen = scale_expansion_zeroelim(4, ea, pd[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, da, -pe[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, de, pa[2], temp8a);
  dealen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       dea);

  temp8alen = scale_expansion_zeroelim(4, ab, pe[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, eb, -pa[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, ea, pb[2], temp8a);
  eablen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       eab);

  temp8alen = scale_expansion_zeroelim(4, bd, pa[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, da, pb[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, ab, pd[2], temp8a);
  abdlen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       abd);

  temp8alen = scale_expansion_zeroelim(4, ce, pb[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, eb, pc[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, bc, pe[2], temp8a);
  bcelen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       bce);

  temp8alen = scale_expansion_zeroelim(4, da, pc[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, ac, pd[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, cd, pa[2], temp8a);
  cdalen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       cda);

  temp8alen = scale_expansion_zeroelim(4, eb, pd[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, bd, pe[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, de, pb[2], temp8a);
  deblen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       deb);

  temp8alen = scale_expansion_zeroelim(4, ac, pe[2], temp8a);
  temp8blen = scale_expansion_zeroelim(4, ce, pa[2], temp8b);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp8blen, temp8b,
                                          temp16);
  temp8alen = scale_expansion_zeroelim(4, ea, pc[2], temp8a);
  eaclen = fast_expansion_sum_zeroelim(temp8alen, temp8a, temp16len, temp16,
                                       eac);

  temp48alen = fast_expansion_sum_zeroelim(cdelen, cde, bcelen, bce, temp48a);
  temp48blen = fast_expansion_sum_zeroelim(deblen, deb, bcdlen, bcd, temp48b);
  for (i = 0; i < temp48blen; i++) {
    temp48b[i] = -temp48b[i];
  }
  bcdelen = fast_expansion_sum_zeroelim(temp48alen, temp48a,
                                        temp48blen, temp48b, bcde);
  xlen = scale_expansion_zeroelim(bcdelen, bcde, pa[0], temp192);
  xlen = scale_expansion_zeroelim(xlen, temp192, pa[0], det384x);
  ylen = scale_expansion_zeroelim(bcdelen, bcde, pa[1], temp192);
  ylen = scale_expansion_zeroelim(ylen, temp192, pa[1], det384y);
  zlen = scale_expansion_zeroelim(bcdelen, bcde, pa[2], temp192);
  zlen = scale_expansion_zeroelim(zlen, temp192, pa[2], det384z);
  xylen = fast_expansion_sum_zeroelim(xlen, det384x, ylen, det384y, detxy);
  alen = fast_expansion_sum_zeroelim(xylen, detxy, zlen, det384z, adet);

  temp48alen = fast_expansion_sum_zeroelim(dealen, dea, cdalen, cda, temp48a);
  temp48blen = fast_expansion_sum_zeroelim(eaclen, eac, cdelen, cde, temp48b);
  for (i = 0; i < temp48blen; i++) {
    temp48b[i] = -temp48b[i];
  }
  cdealen = fast_expansion_sum_zeroelim(temp48alen, temp48a,
                                        temp48blen, temp48b, cdea);
  xlen = scale_expansion_zeroelim(cdealen, cdea, pb[0], temp192);
  xlen = scale_expansion_zeroelim(xlen, temp192, pb[0], det384x);
  ylen = scale_expansion_zeroelim(cdealen, cdea, pb[1], temp192);
  ylen = scale_expansion_zeroelim(ylen, temp192, pb[1], det384y);
  zlen = scale_expansion_zeroelim(cdealen, cdea, pb[2], temp192);
  zlen = scale_expansion_zeroelim(zlen, temp192, pb[2], det384z);
  xylen = fast_expansion_sum_zeroelim(xlen, det384x, ylen, det384y, detxy);
  blen = fast_expansion_sum_zeroelim(xylen, detxy, zlen, det384z, bdet);

  temp48alen = fast_expansion_sum_zeroelim(eablen, eab, deblen, deb, temp48a);
  temp48blen = fast_expansion_sum_zeroelim(abdlen, abd, dealen, dea, temp48b);
  for (i = 0; i < temp48blen; i++) {
    temp48b[i] = -temp48b[i];
  }
  deablen = fast_expansion_sum_zeroelim(temp48alen, temp48a,
                                        temp48blen, temp48b, deab);
  xlen = scale_expansion_zeroelim(deablen, deab, pc[0], temp192);
  xlen = scale_expansion_zeroelim(xlen, temp192, pc[0], det384x);
  ylen = scale_expansion_zeroelim(deablen, deab, pc[1], temp192);
  ylen = scale_expansion_zeroelim(ylen, temp192, pc[1], det384y);
  zlen = scale_expansion_zeroelim(deablen, deab, pc[2], temp192);
  zlen = scale_expansion_zeroelim(zlen, temp192, pc[2], det384z);
  xylen = fast_expansion_sum_zeroelim(xlen, det384x, ylen, det384y, detxy);
  clen = fast_expansion_sum_zeroelim(xylen, detxy, zlen, det384z, cdet);

  temp48alen = fast_expansion_sum_zeroelim(abclen, abc, eaclen, eac, temp48a);
  temp48blen = fast_expansion_sum_zeroelim(bcelen, bce, eablen, eab, temp48b);
  for (i = 0; i < temp48blen; i++) {
    temp48b[i] = -temp48b[i];
  }
  eabclen = fast_expansion_sum_zeroelim(temp48alen, temp48a,
                                        temp48blen, temp48b, eabc);
  xlen = scale_expansion_zeroelim(eabclen, eabc, pd[0], temp192);
  xlen = scale_expansion_zeroelim(xlen, temp192, pd[0], det384x);
  ylen = scale_expansion_zeroelim(eabclen, eabc, pd[1], temp192);
  ylen = scale_expansion_zeroelim(ylen, temp192, pd[1], det384y);
  zlen = scale_expansion_zeroelim(eabclen, eabc, pd[2], temp192);
  zlen = scale_expansion_zeroelim(zlen, temp192, pd[2], det384z);
  xylen = fast_expansion_sum_zeroelim(xlen, det384x, ylen, det384y, detxy);
  dlen = fast_expansion_sum_zeroelim(xylen, detxy, zlen, det384z, ddet);

  temp48alen = fast_expansion_sum_zeroelim(bcdlen, bcd, abdlen, abd, temp48a);
  temp48blen = fast_expansion_sum_zeroelim(cdalen, cda, abclen, abc, temp48b);
  for (i = 0; i < temp48blen; i++) {
    temp48b[i] = -temp48b[i];
  }
  abcdlen = fast_expansion_sum_zeroelim(temp48alen, temp48a,
                                        temp48blen, temp48b, abcd);
  xlen = scale_expansion_zeroelim(abcdlen, abcd, pe[0], temp192);
  xlen = scale_expansion_zeroelim(xlen, temp192, pe[0], det384x);
  ylen = scale_expansion_zeroelim(abcdlen, abcd, pe[1], temp192);
  ylen = scale_expansion_zeroelim(ylen, temp192, pe[1], det384y);
  zlen = scale_expansion_zeroelim(abcdlen, abcd, pe[2], temp192);
  zlen = scale_expansion_zeroelim(zlen, temp192, pe[2], det384z);
  xylen = fast_expansion_sum_zeroelim(xlen, det384x, ylen, det384y, detxy);
  elen = fast_expansion_sum_zeroelim(xylen, detxy, zlen, det384z, edet);

  ablen = fast_expansion_sum_zeroelim(alen, adet, blen, bdet, abdet);
  cdlen = fast_expansion_sum_zeroelim(clen, cdet, dlen, ddet, cddet);
  cdelen = fast_expansion_sum_zeroelim(cdlen, cddet, elen, edet, cdedet);
  deterlen = fast_expansion_sum_zeroelim(ablen, abdet, cdelen, cdedet, deter);

  return deter[deterlen - 1];
}

/* predicates.c lines 3937-4155: insphereadapt() */
double insphereadapt(const double *pa, const double *pb, const double *pc,
                     const double *pd, const double *pe, double permanent)
{
  double aex, bex, cex, dex, aey, bey, cey, dey, aez, bez, cez, dez;
  double det, errbound;

  double aexbey1, bexaey1, bexcey1, cexbey1;
  double cexdey1, dexcey1, dexaey1, aexdey1;
  double aexcey1, cexaey1, bexdey1, dexbey1;
  double aexbey0, bexaey0, bexcey0, cexbey0;
  double cexdey0, dexcey0, dexaey0, aexdey0;
  double aexcey0, cexaey0, bexdey0, dexbey0;
  double ab[4], bc[4], cd[4], da[4], ac[4], bd[4];
  double ab3, bc3, cd3, da3, ac3, bd3;
  double abeps, bceps, cdeps, daeps, aceps, bdeps;
  double temp8a[8], temp8b[8], temp8c[8], temp16[16], temp24[24], temp48[48];
  int temp8alen, temp8blen, temp8clen, temp16len, temp24len, temp48len;
  double xdet[96], ydet[96], zdet[96], xydet[192];
  int xlen, ylen, zlen, xylen;
  double adet[288], bdet[288], cdet[288], ddet[288];
  int alen, blen, clen, dlen;
  double abdet[576], cddet[576];
  int ablen, cdlen;
  double fin1[1152];
  int finlength;

  double aextail, bextail, cextail, dextail;
  double aeytail, beytail, ceytail, deytail;
  double aeztail, beztail, ceztail, deztail;

  double bvirt;
  double avirt, bround, around;
  double c;
  double abig;
  double ahi, alo, bhi, blo;
  double err1, err2, err3;
  double _i, _j;
  double _0;

  aex = (double) (pa[0] - pe[0]);
  bex = (double) (pb[0] - pe[0]);
  cex = (double) (pc[0] - pe[0]);
  dex = (double) (pd[0] - pe[0]);
  aey = (double) (pa[1] - pe[1]);
  bey = (double) (pb[1] - pe[1]);
  cey = (double) (pc[1] - pe[1]);
  dey = (double) (pd[1] - pe[1]);
  aez = (double) (pa[2] - pe[2]);
  bez = (double) (pb[2] - pe[2]);
  cez = (double) (pc[2] - pe[2]);
  dez = (double) (pd[2] - pe[2]);

  Two_Product(aex, bey, aexbey1, aexbey0);
  Two_Product(bex, aey, bexaey1, bexaey0);
  Two_Two_Diff(aexbey1, aexbey0, bexaey1, bexaey0, ab3, ab[2], ab[1], ab[0]);
  ab[3] = ab3;

  Two_Product(bex, cey, bexcey1, bexcey0);
  Two_Product(cex, bey, cexbey1, cexbey0);
  Two_Two_Diff(bexcey1, bexcey0, cexbey1, cexbey0, bc3, bc[2], bc[1], bc[0]);
  bc[3] = bc3;

  Two_Product(cex, dey, cexdey1, cexdey0);
  Two_Product(dex, cey, dexcey1, dexcey0);
  Two_Two_Diff(cexdey1, cexdey0, dexcey1, dexcey0, cd3, cd[2], cd[1], cd[0]);
  cd[3] = cd3;

  Two_Product(dex, aey, dexaey1, dexaey0);
  Two_Product(aex, dey, aexdey1, aexdey0);
  Two_Two_Diff(dexaey1, dexaey0, aexdey1, aexdey0, da3, da[2], da[1], da[0]);
  da[3] = da3;

  Two_Product(aex, cey, aexcey1, aexcey0);
  Two_Product(cex, aey, cexaey1, cexaey0);
  Two_Two_Diff(aexcey1, aexcey0, cexaey1, cexaey0, ac3, ac[2], ac[1], ac[0]);
  ac[3] = ac3;

  Two_Product(bex, dey, bexdey1, bexdey0);
  Two_Product(dex, bey, dexbey1, dexbey0);
  Two_Two_Diff(bexdey1, bexdey0, dexbey1, dexbey0, bd3, bd[2], bd[1], bd[0]);
  bd[3] = bd3;

  temp8alen = scale_expansion_zeroelim(4, cd, bez, temp8a);
  temp8blen = scale_expansion_zeroelim(4, bd, -cez, temp8b);
  temp8clen = scale_expansion_zeroelim(4, bc, dez, temp8c);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a,
                                          temp8blen, temp8b, temp16);
  temp24len = fast_expansion_sum_zeroelim(temp8clen, temp8c,
                                          temp16len, temp16, temp24);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, aex, temp48);
  xlen = scale_expansion_zeroelim(temp48len, temp48, -aex, xdet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, aey, temp48);
  ylen = scale_expansion_zeroelim(temp48len, temp48, -aey, ydet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, aez, temp48);
  zlen = scale_expansion_zeroelim(temp48len, temp48, -aez, zdet);
  xylen = fast_expansion_sum_zeroelim(xlen, xdet, ylen, ydet, xydet);
  alen = fast_expansion_sum_zeroelim(xylen, xydet, zlen, zdet, adet);

  temp8alen = scale_expansion_zeroelim(4, da, cez, temp8a);
  temp8blen = scale_expansion_zeroelim(4, ac, dez, temp8b);
  temp8clen = scale_expansion_zeroelim(4, cd, aez, temp8c);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a,
                                          temp8blen, temp8b, temp16);
  temp24len = fast_expansion_sum_zeroelim(temp8clen, temp8c,
                                          temp16len, temp16, temp24);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, bex, temp48);
  xlen = scale_expansion_zeroelim(temp48len, temp48, bex, xdet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, bey, temp48);
  ylen = scale_expansion_zeroelim(temp48len, temp48, bey, ydet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, bez, temp48);
  zlen = scale_expansion_zeroelim(temp48len, temp48, bez, zdet);
  xylen = fast_expansion_sum_zeroelim(xlen, xdet, ylen, ydet, xydet);
  blen = fast_expansion_sum_zeroelim(xylen, xydet, zlen, zdet, bdet);

  temp8alen = scale_expansion_zeroelim(4, ab, dez, temp8a);
  temp8blen = scale_expansion_zeroelim(4, bd, aez, temp8b);
  temp8clen = scale_expansion_zeroelim(4, da, bez, temp8c);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a,
                                          temp8blen, temp8b, temp16);
  temp24len = fast_expansion_sum_zeroelim(temp8clen, temp8c,
                                          temp16len, temp16, temp24);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, cex, temp48);
  xlen = scale_expansion_zeroelim(temp48len, temp48, -cex, xdet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, cey, temp48);
  ylen = scale_expansion_zeroelim(temp48len, temp48, -cey, ydet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, cez, temp48);
  zlen = scale_expansion_zeroelim(temp48len, temp48, -cez, zdet);
  xylen = fast_expansion_sum_zeroelim(xlen, xdet, ylen, ydet, xydet);
  clen = fast_expansion_sum_zeroelim(xylen, xydet, zlen, zdet, cdet);

  temp8alen = scale_expansion_zeroelim(4, bc, aez, temp8a);
  temp8blen = scale_expansion_zeroelim(4, ac, -bez, temp8b);
  temp8clen = scale_expansion_zeroelim(4, ab, cez, temp8c);
  temp16len = fast_expansion_sum_zeroelim(temp8alen, temp8a,
                                          temp8blen, temp8b, temp16);
  temp24len = fast_expansion_sum_zeroelim(temp8clen, temp8c,
                                          temp16len, temp16, temp24);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, dex, temp48);
  xlen = scale_expansion_zeroelim(temp48len, temp48, dex, xdet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, dey, temp48);
  ylen = scale_expansion_zeroelim(temp48len, temp48, dey, ydet);
  temp48len = scale_expansion_zeroelim(temp24len, temp24, dez, temp48);
  zlen = scale_expansion_zeroelim(temp48len, temp48, dez, zdet);
  xylen = fast_expansion_sum_zeroelim(xlen, xdet, ylen, ydet, xydet);
  dlen = fast_expansion_sum_zeroelim(xylen, xydet, zlen, zdet, ddet);

  ablen = fast_expansion_sum_zeroelim(alen, adet, blen, bdet, abdet);
  cdlen = fast_expansion_sum_zeroelim(clen, cdet, dlen, ddet, cddet);
  finlength = fast_expansion_sum_zeroelim(ablen, abdet, cdlen, cddet, fin1);

  det = estimate(finlength, fin1);
  errbound = isperrboundB * permanent;
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_B);
    return det;
  }

  Two_Diff_Tail(pa[0], pe[0], aex, aextail);
  Two_Diff_Tail(pa[1], pe[1], aey, aeytail);
  Two_Diff_Tail(pa[2], pe[2], aez, aeztail);
  Two_Diff_Tail(pb[0], pe[0], bex, bextail);
  Two_Diff_Tail(pb[1], pe[1], bey, beytail);
  Two_Diff_Tail(pb[2], pe[2], bez, beztail);
  Two_Diff_Tail(pc[0], pe[0], cex, cextail);
  Two_Diff_Tail(pc[1], pe[1], cey, ceytail);
  Two_Diff_Tail(pc[2], pe[2], cez, ceztail);
  Two_Diff_Tail(pd[0], pe[0], dex, dextail);
  Two_Diff_Tail(pd[1], pe[1], dey, deytail);
  Two_Diff_Tail(pd[2], pe[2], dez, deztail);
  if ((aextail == 0.0) && (aeytail == 0.0) && (aeztail == 0.0)
      && (bextail == 0.0) && (beytail == 0.0) && (beztail == 0.0)
      && (cextail == 0.0) && (ceytail == 0.0) && (ceztail == 0.0)
      && (dextail == 0.0) && (deytail == 0.0) && (deztail == 0.0)) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_BX);
    return det;
  }

  errbound = isperrboundC * permanent + resulterrbound * std::fabs(det);
  abeps = (aex * beytail + bey * aextail)
        - (aey * bextail + bex * aeytail);
  bceps = (bex * ceytail + cey * bextail)
        - (bey * cextail + cex * beytail);
  cdeps = (cex * deytail + dey * cextail)
        - (cey * dextail + dex * ceytail);
  daeps = (dex * aeytail + aey * dextail)
        - (dey * aextail + aex * deytail);
  aceps = (aex * ceytail + cey * aextail)
        - (aey * cextail + cex * aeytail);
  bdeps = (bex * deytail + dey * bextail)
        - (bey * dextail + dex * beytail);
  det += (((bex * bex + bey * bey + bez * bez)
           * ((cez * daeps + dez * aceps + aez * cdeps)
              + (ceztail * da3 + deztail * ac3 + aeztail * cd3))
           + (dex * dex + dey * dey + dez * dez)
           * ((aez * bceps - bez * aceps + cez * abeps)
              + (aeztail * bc3 - beztail * ac3 + ceztail * ab3)))
          - ((aex * aex + aey * aey + aez * aez)
           * ((bez * cdeps - cez * bdeps + dez * bceps)
              + (beztail * cd3 - ceztail * bd3 + deztail * bc3))
           + (cex * cex + cey * cey + cez * cez)
           * ((dez * abeps + aez * bdeps + bez * daeps)
              + (deztail * ab3 + aeztail * bd3 + beztail * da3))))
       + 2.0 * (((bex * bextail + bey * beytail + bez * beztail)
                 * (cez * da3 + dez * ac3 + aez * cd3)
                 + (dex * dextail + dey * deytail + dez * deztail)
                 * (aez * bc3 - bez * ac3 + cez * ab3))
                - ((aex * aextail + aey * aeytail + aez * aeztail)
                 * (bez * cd3 - cez * bd3 + dez * bc3)
                 + (cex * cextail + cey * ceytail + cez * ceztail)
                 * (dez * ab3 + aez * bd3 + bez * da3)));
  if (std::fabs(det) >= errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_C);
    return det;
  }

  SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_D);
  return insphereexact(pa, pb, pc, pd, pe);
}

// What runs when stage A cannot certify the sign: the exact integer stage
// (integer_stage.h), and behind it the adaptive stages of 1996, unchanged.
// Not from 1996: the integer stage decides every input that fits its fast
// tiers AND every input outside the floating-point domain; it refuses, and
// the adaptive stages run, only inside that domain, where no operation of
// theirs overflows or underflows (docs/proofs/input-domain.md, section 5).
// Not marked SHEWCHUK_NOINLINE, for the reason insphereadapt() is not (see
// there).
double insphere_after_filter(const double *pa, const double *pb, const double *pc,
                             const double *pd, const double *pe, double permanent)
{
#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_AFTER);
  const int sign = integer_stage::insphere(pa, pb, pc, pd, pe);
  if (sign != integer_stage::refused) {
    if (sign == integer_stage::not_finite) {
      SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
      return not_a_number();
    }
    SHEWCHUK_INTEGER_STAGE_HIT();
    return sign;
  }
#endif
  return insphereadapt(pa, pb, pc, pd, pe, permanent);
}

#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
// What runs when stage A certifies det but its guard does not hold (rare;
// never inlined, so that stage A does not pay for it). Inside the
// floating-point domain no operation of stage A overflowed or underflowed,
// so det is returned after all, as in 1996. Outside, the integer stage
// decides.
SHEWCHUK_NOINLINE double insphere_guard_failed(const double *pa, const double *pb, const double *pc,
                                               const double *pd, const double *pe, double det)
{
  SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_AFTER);
  const int sign =
      integer_stage::insphere(pa, pb, pc, pd, pe, integer_stage::Request::prefer_floating_point);
  if (sign == integer_stage::refused) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_RECHECKED);
    return det;
  }
  if (sign == integer_stage::not_finite) {
    SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_NOT_FINITE);
    return not_a_number();
  }
  SHEWCHUK_INTEGER_STAGE_HIT();
  return sign;
}
#endif

}  // namespace

/* predicates.c lines 4157-4262: insphere() */
double shewchuk_insphere(const double *pa, const double *pb, const double *pc,
                         const double *pd, const double *pe)
{
  double aex, bex, cex, dex;
  double aey, bey, cey, dey;
  double aez, bez, cez, dez;
  double aexbey, bexaey, bexcey, cexbey, cexdey, dexcey, dexaey, aexdey;
  double aexcey, cexaey, bexdey, dexbey;
  double alift, blift, clift, dlift;
  double ab, bc, cd, da, ac, bd;
  double abc, bcd, cda, dab;
  double aezplus, bezplus, cezplus, dezplus;
  double aexbeyplus, bexaeyplus, bexceyplus, cexbeyplus;
  double cexdeyplus, dexceyplus, dexaeyplus, aexdeyplus;
  double aexceyplus, cexaeyplus, bexdeyplus, dexbeyplus;
  double det;
  double permanent, errbound;

  aex = pa[0] - pe[0];
  bex = pb[0] - pe[0];
  cex = pc[0] - pe[0];
  dex = pd[0] - pe[0];
  aey = pa[1] - pe[1];
  bey = pb[1] - pe[1];
  cey = pc[1] - pe[1];
  dey = pd[1] - pe[1];
  aez = pa[2] - pe[2];
  bez = pb[2] - pe[2];
  cez = pc[2] - pe[2];
  dez = pd[2] - pe[2];

  aexbey = aex * bey;
  bexaey = bex * aey;
  ab = aexbey - bexaey;
  bexcey = bex * cey;
  cexbey = cex * bey;
  bc = bexcey - cexbey;
  cexdey = cex * dey;
  dexcey = dex * cey;
  cd = cexdey - dexcey;
  dexaey = dex * aey;
  aexdey = aex * dey;
  da = dexaey - aexdey;

  aexcey = aex * cey;
  cexaey = cex * aey;
  ac = aexcey - cexaey;
  bexdey = bex * dey;
  dexbey = dex * bey;
  bd = bexdey - dexbey;

  abc = aez * bc - bez * ac + cez * ab;
  bcd = bez * cd - cez * bd + dez * bc;
  cda = cez * da + dez * ac + aez * cd;
  dab = dez * ab + aez * bd + bez * da;

  alift = aex * aex + aey * aey + aez * aez;
  blift = bex * bex + bey * bey + bez * bez;
  clift = cex * cex + cey * cey + cez * cez;
  dlift = dex * dex + dey * dey + dez * dez;

  det = (dlift * abc - clift * dab) + (blift * cda - alift * bcd);

  aezplus = std::fabs(aez);
  bezplus = std::fabs(bez);
  cezplus = std::fabs(cez);
  dezplus = std::fabs(dez);
  aexbeyplus = std::fabs(aexbey);
  bexaeyplus = std::fabs(bexaey);
  bexceyplus = std::fabs(bexcey);
  cexbeyplus = std::fabs(cexbey);
  cexdeyplus = std::fabs(cexdey);
  dexceyplus = std::fabs(dexcey);
  dexaeyplus = std::fabs(dexaey);
  aexdeyplus = std::fabs(aexdey);
  aexceyplus = std::fabs(aexcey);
  cexaeyplus = std::fabs(cexaey);
  bexdeyplus = std::fabs(bexdey);
  dexbeyplus = std::fabs(dexbey);
  permanent = ((cexdeyplus + dexceyplus) * bezplus
               + (dexbeyplus + bexdeyplus) * cezplus
               + (bexceyplus + cexbeyplus) * dezplus)
            * alift
            + ((dexaeyplus + aexdeyplus) * cezplus
               + (aexceyplus + cexaeyplus) * dezplus
               + (cexdeyplus + dexceyplus) * aezplus)
            * blift
            + ((aexbeyplus + bexaeyplus) * dezplus
               + (bexdeyplus + dexbeyplus) * aezplus
               + (dexaeyplus + aexdeyplus) * bezplus)
            * clift
            + ((bexceyplus + cexbeyplus) * aezplus
               + (cexaeyplus + aexceyplus) * bezplus
               + (aexbeyplus + bexaeyplus) * cezplus)
            * dlift;
  errbound = isperrboundA * permanent;
#ifdef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
  if (std::fabs(det) > errbound) {
    SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
    return det;
  }
#else
  // Not from 1996: fabs(det) > errbound is tested on the bit patterns, and
  // det is returned only if, in addition, the guard of the filter holds
  // (filter_guard.h): then det, permanent and errbound are what an unbounded
  // exponent range would give, and the 1996 analysis applies. The guard reads
  // bit patterns and compares integers; coordinates are read from memory.
  if (guard::filter_certifies(det, errbound)) {
    // The four lifted coordinates bound all twelve differences, with far
    // fewer operations than testing the fifteen coordinates. Which form costs
    // less depends on what the compiler made of the filter (measured, see
    // docs/benchmarking.md): clang keeps the lifts in vector registers, where
    // adding them up is cheaper than taking them out one by one; either form
    // is a set of sums of squares in the sense of holds_for_squares().
#if defined(__clang__)
    if (guard::holds_for_squares(guard::insphere_guard, errbound,
                                 (alift + blift) + (clift + dlift))) [[likely]] {
#else
    if (guard::holds_for_squares(guard::insphere_guard, errbound, alift, blift, clift,
                                 dlift)) [[likely]] {
#endif
      SHEWCHUK_STAGE_HIT(SHEWCHUK_STAGE_A);
      SHEWCHUK_PATH_HIT(SHEWCHUK_PATH_LEAF);
      return det;
    }
    return insphere_guard_failed(pa, pb, pc, pd, pe, det);
  }
#endif

  return insphere_after_filter(pa, pb, pc, pd, pe, permanent);
}

// ---------------------------------------------------------------------------
// The sign-returning interface. Not from 1996.
//
// The sign of the value the predicate returns, which is the exact sign for
// every finite input (docs/proofs/input-domain.md). A predicate returns a NaN
// exactly if a coordinate is not finite; a NaN compares false with
// everything, also with itself, and becomes SHEWCHUK_SIGN_NOT_FINITE here,
// never 0. These comparisons are in this translation unit on purpose: a
// caller compiled with -ffinite-math-only could not make them.
// ---------------------------------------------------------------------------
#ifndef SHEWCHUK_PREDICATES_NO_INTEGER_STAGE
namespace {

inline int sign_of_result(double value) noexcept
{
  if (value > 0.0) return 1;
  if (value < 0.0) return -1;
  if (value == 0.0) return 0;
  return SHEWCHUK_SIGN_NOT_FINITE;
}

}  // namespace

int shewchuk_orient2d_sign(const double *pa, const double *pb, const double *pc)
{
  return sign_of_result(shewchuk_orient2d(pa, pb, pc));
}

int shewchuk_orient3d_sign(const double *pa, const double *pb, const double *pc,
                           const double *pd)
{
  return sign_of_result(shewchuk_orient3d(pa, pb, pc, pd));
}

int shewchuk_incircle_sign(const double *pa, const double *pb, const double *pc,
                           const double *pd)
{
  return sign_of_result(shewchuk_incircle(pa, pb, pc, pd));
}

int shewchuk_insphere_sign(const double *pa, const double *pb, const double *pc,
                           const double *pd, const double *pe)
{
  return sign_of_result(shewchuk_insphere(pa, pb, pc, pd, pe));
}
#endif

#ifdef SHEWCHUK_PREDICATES_TEST_HOOKS
// Test-only exports of the internal expansion routines (test_hooks.h).
int shewchuk_hooked_fast_expansion_sum_zeroelim(int elen, const double *e, int flen,
                                                const double *f, double *h)
{
  return fast_expansion_sum_zeroelim(elen, e, flen, f, h);
}

int shewchuk_hooked_scale_expansion_zeroelim(int elen, const double *e, double b, double *h)
{
  return scale_expansion_zeroelim(elen, e, b, h);
}

double shewchuk_hooked_estimate(int elen, const double *e)
{
  return estimate(elen, e);
}
#endif
