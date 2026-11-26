# Exactness ledger

What each stage of each predicate claims, what the claim rests on, and where
the argument is written down. The rule of the project: the sign of every
result is exactly the sign of the determinant, and no change may weaken that.

**Current state.** The library (`src/predicates.cpp`) is a port of the four
adaptive predicates of the 1996 `predicates.c`, with two additions:

- an exact integer stage (stage I, `src/integer_stage.h`) that is tried
  before the adaptive stages B, C and D (for `orient2d`: before stage D);
- an input-domain guard (`src/filter_guard.h`): a floating-point stage is
  used only where no overflow or underflow can have affected it, and the
  integer stage decides every other finite input.

With them **the sign is exact for every finite input**; the 1996 code returns
wrong signs, silently, outside an exponent range.

- Apart from the rewrites listed under [The filters](#the-filters), which
  cannot change a returned value, the floating-point stages perform the same
  operations in the same order as the 1996 code: built without the two
  additions, the library returns values bit-identical to the pristine file's
  on the whole test corpus, and with them, every value a floating-point stage
  returns still is (see [testing.md](testing.md)). Their claims below are
  therefore the 1996 paper's claims, inherited unchanged. Nothing there has
  been re-derived yet.
- The integer stage is new, and its claim is proved in
  [proofs/integer-stage.md](proofs/integer-stage.md).
- The guard is new, and what it guarantees, together with the routing of
  every finite input, is proved in
  [proofs/input-domain.md](proofs/input-domain.md).

"The paper" is J. R. Shewchuk, *Adaptive Precision Floating-Point Arithmetic
and Fast Robust Geometric Predicates*, Discrete & Computational Geometry
18(3):305-363, 1997 (technical report CMU-CS-96-140R). Theorem and section
numbers are that version's.

## Assumptions

Every claim holds only if all of these do.

| # | Assumption | What enforces or checks it |
|---|---|---|
| 1 | `double` is IEEE 754 binary64, and every `+ - *` is one correctly rounded operation in round-to-nearest, ties-to-even. The rounding mode is never changed by the caller. | `#error` / `static_assert` on the format and on `FLT_EVAL_METHOD`. The rounding mode is the caller's responsibility. Fast-Expansion-Sum specifically needs ties-to-even (paper, section 2.4). |
| 2 | Operations are evaluated as written: no contraction into fused multiply-add, no reassociation, no extended precision. | The translation unit is compiled with flags set by the project (`cmake/StrictFloatingPoint.cmake`), plus `#error` on fast-math macros. Checked after the build by `library.no_fma` (no fused multiply-add in the object code) and by the value hash. |
| 3 | No intermediate result of a floating-point stage that decides a call overflows or underflows in a way that changes a value. The paper does not analyse overflow or underflow; it states that the four predicates neither overflow nor underflow for inputs whose exponents are in [-142, 201]. | **Enforced at run time**, see [Input domain](#input-domain): the first filter returns only under a guard that proves it, the later stages run only inside a proven exponent domain, and the integer stage decides everything else. |
| 4 | Gradual underflow is on (no flush-to-zero, no denormals-are-zero mode), so that `x - y == 0` only if `x == y`. Inside the domain of assumption 3 no subnormal number should arise at all; this assumption is what keeps the first sign tests meaningful at the edge. | The caller's responsibility; not checked. |
| 5 | Inputs are finite. | Checked: no stage decides a call with a coordinate that is not finite, and such a call returns a NaN. |

The integer stage needs none of these assumptions: it uses no floating-point
arithmetic. It needs only that a `double` is an IEEE 754 binary64 (checked at
compile time). The guard compares bit patterns and needs no more than that
either. But the floating-point stages decide most calls, so the predicates as
a whole still rest on assumptions 1, 2 and 4; assumptions 3 and 5 are no
longer assumptions about the input.

### Callers' flags and link-time optimization

Assumption 2 is about the library's translation unit only. Code that calls
the library may be compiled with any flags, including `-Ofast` /
`-ffast-math`, and the whole program may be link-time optimized (`-flto`,
`CMAKE_INTERPROCEDURAL_OPTIMIZATION`, MSVC `/GL`), with those flags on the
link line. This rests on how the compilers record floating-point semantics,
not on anything the library does:

- GCC stores the optimization options of each function in its link-time
  representation and compiles each function with its own.
- clang records the permission to contract or reassociate on each
  floating-point instruction, so library code that is inlined into a
  fast-math caller keeps its strict semantics.

Observed on arm64 with GCC 16, LLVM 22 and Apple clang (thin and full LTO):
clang inlines `shewchuk_orient2d` into the fast-math caller, GCC keeps every
library function out of line, and in both cases the linked executable has no
fused multiply-add in any code that came from the library.

Tested by `library.lto_fast_math_caller`, with a negative control
(see [testing.md](testing.md)). What is not covered: options that change code
generation for the whole link regardless of how each translation unit was
compiled (for example passing `-fp-contract=fast` to the LLVM back end
through the linker), and compiling `src/predicates.cpp` outside the project's
build system without the flags of `cmake/StrictFloatingPoint.cmake`. A
program linked with `-ffast-math` may also start with flush-to-zero enabled
(GCC links `crtfastmath.o` on some targets), which is assumption 4.

## Building blocks

Each is an error-free transformation: the outputs represent the exact result.

| Operation (macro or function) | Claim | Paper |
|---|---|---|
| `Fast_Two_Sum` | x + y = a + b exactly, if \|a\| >= \|b\| | Theorem 6 |
| `Two_Sum`, `Two_Diff` | x + y = a ± b exactly | Theorem 7 |
| `Split` | a = ahi + alo exactly, each half fits in 26 bits | Theorem 17 |
| `Two_Product`, `Square` | x + y = a * b exactly | Theorem 18 |
| `Two_One_Sum` ... `Two_Two_Diff` | fixed-length expansion sums, exact | Theorem 12 (Expansion-Sum) |
| `fast_expansion_sum_zeroelim` | h = e + f exactly; h is strongly nonoverlapping if e and f are | Theorem 13 |
| `scale_expansion_zeroelim` | h = b * e exactly; preserves nonoverlapping and strongly nonoverlapping | Theorem 19, Corollary 22 |
| `estimate` | one-word approximation of an expansion | section 2.7 |

For a nonoverlapping expansion with zero components eliminated, the most
significant component has the sign of the whole expansion (paper, section
2.8). That is how the exact stages produce their sign.

## Stages

Every predicate evaluates a determinant of coordinate differences. Stage
names are those of the paper (section 4.3 and Figure 21) and of the test suite
(`tests/stages.h`). ε = 2^-53. The error-bound constants are in
`src/constants.h`.

A stage that is "certified" returns its approximation `det` only if
`|det| >= errbound`, where `errbound` bounds the distance between `det` and
the exact determinant; then `det` has the exact sign. Otherwise the next
stage runs.

### The integer stage (I), all four predicates

| Stage | Claim | Bound | Proof |
|---|---|---|---|
| I | Exact, or declines. Scales the coordinates by one power of two to integers and evaluates the determinant in fixed-width integers: for a width `W` (bit length of the largest scaled magnitude) of at most 189 in a fast tier (`W <= 61`: one 64-bit limb per coordinate difference, `<= 125`: two, `<= 189`: three), for a wider input in the wide tier (33 limbs, which hold any finite double). Returns the sign as -1.0, 0.0 or +1.0. Declines, leaving the call to the floating-point stages, only if the input is inside the [floating-point domain](#input-domain) (and, where it is asked behind a first filter that could not certify, wider than 189 bits). Reports a coordinate that is not finite. Exact for every finite input it decides, subnormals included. | none | [proofs/integer-stage.md](proofs/integer-stage.md) |

Position: behind stage A for `orient3d`, `incircle` and `insphere`; behind
stage C for `orient2d` (its stages B and C are cheaper than the integer
stage). The stages listed after that position in the tables below run only
when stage I declines, hence only inside the floating-point domain.

### The guard of the first filter

| Stage | Claim | Proof |
|---|---|---|
| A (A0 and A for `orient2d`), under its guard | The first filter's `det` is returned only if a guard on bit patterns holds. For `orient3d`, `incircle`, `insphere`: `det` is finite and the error bound is at least `2^-853 D`, `2^-852 D²`, `2^-683 D³`, with `D >= 1` a power of two that bounds the magnitudes of the coordinate differences that can amplify an underflowed value. For `orient2d`: `2^-600 <= errbound < 2^950`. Then `det`, `permanent` (`detsum`) and the error bound are bit for bit what arithmetic with an unbounded exponent range gives, and the 1996 claim for stage A applies. The same window covers stages B and C of `orient2d`. | [proofs/input-domain.md](proofs/input-domain.md), sections 3 and 4 |

### orient2d

| Stage | Claim | Bound |
|---|---|---|
| A0 | Exact sign without a bound: the two products have opposite signs or one is zero, so their difference cannot cancel. Relies on assumptions 3 and 4 (a rounded difference or product is zero only if the exact one is); the library's guard makes sure of 3. In the library A0 and A are one comparison, see [orient2d: the merged first test](#orient2d-the-merged-first-test). | none |
| A | Certified. `det` is the determinant in plain double arithmetic. | `ccwerrboundA * detsum`, (3ε + 16ε²)(\|detleft\| + \|detright\|) |
| B | Certified. `B` is the exact determinant of the rounded differences; `det = estimate(B)`. | `ccwerrboundB * detsum`, (2ε + 12ε²) |
| Bx | Exact. All four difference tails are zero, so the differences were exact and `B` is the exact determinant. | none |
| C | Certified. `det` plus a first-order correction for the difference tails. | `ccwerrboundC * detsum + resulterrbound * \|det\|`, (9ε² + 64ε³) and (3ε + 8ε²) |
| D | Exact. The full determinant as an expansion of at most 16 components; returns its most significant component. | none |

The paper derives the bound for A as a worked example in section 4.3; the
others are stated there.

### orient3d, incircle, insphere

Same structure, without stage A0. `permanent` is the sum of the absolute
values of the terms of the determinant, computed in double arithmetic.

| Stage | Claim | orient3d | incircle | insphere |
|---|---|---|---|---|
| A | Certified, plain double arithmetic. Bound: `errboundA * permanent` | (7ε + 56ε²) | (10ε + 96ε²) | (16ε + 224ε²) |
| B | Certified, exact determinant of the rounded differences. Bound: `errboundB * permanent` | (3ε + 28ε²) | (4ε + 48ε²) | (5ε + 72ε²) |
| Bx | Exact: all difference tails are zero | | | |
| C | Certified, first-order tail correction. Bound: `errboundC * permanent + resulterrbound * \|det\|` | (26ε² + 288ε³) | (44ε² + 576ε³) | (71ε² + 1408ε³) |
| D | Exact expansion arithmetic; returns the most significant component | adaptive, reuses stage B | adaptive, reuses stage B | `insphereexact()`: recomputed from the coordinates |

The paper describes these predicates in section 4.4. It does not print the
derivations of their constants; they are the values computed by `exactinit()`
in `predicates.c`.

## The filters

The filters (stages A, B and C) of every predicate are written differently
from the 1996 code. Each rewrite below returns the same double as the
original, for every input in the first two cases and for every input that does
not make a product NaN in the third, so the claims above are unchanged. The proof obligation is the
argument here plus the recorded value hash, which must still match
(`differential.library`).

**One comparison.** The 1996 code tests `(det > errbound) || (-det > errbound)`;
the library tests `fabs(det) > errbound`. For any `det` that is not NaN,
`(det > e) || (-det > e)` says that the larger of `det` and `-det` exceeds
`e`, and that larger value is `fabs(det)` (for a zero of either sign both are
zero, and the sign of a zero does not affect a comparison). If `det` or `e` is
NaN every one of the comparisons is false. So the two tests are the same
predicate on all doubles, for every `e`. The same holds with `>=` in place of
`>`. The first filters of orient3d, incircle and insphere use `>`, as in 1996.
orient2d's merged first test (below) and the later filters (stages B and C of
every predicate) use `>=`, as in 1996.

**`fabs` for absolute values.** The 1996 macro `Absolute(a)` is
`(a >= 0.0 ? a : -a)`. It equals `fabs(a)` except that `Absolute(-0.0)` is
`-0.0` and that for a NaN the sign bit differs. The absolute values feed only
`permanent`, a sum of products of them. Replacing a `-0.0` term by `+0.0` can
change `permanent` only if `permanent` is a zero, and then only its sign; a
NaN stays a NaN. `permanent` is used only to form error bounds (`errbound =
c * permanent` here, and `c * permanent + resulterrbound * |det|` after it is
passed to the `*adapt` function), and an error bound is used only as the
right-hand side of comparisons, which the sign of a zero and the bits of a NaN
do not affect. No returned value is computed from `permanent`.

Stage C forms its bound as `c * permanent + resulterrbound * Absolute(det)`
(`detsum` in place of `permanent` in orient2d); the library uses `fabs(det)`.
The two differ only for `det` a negative zero or a NaN, which changes the
bound at most in the sign of a zero or in the bits of a NaN, and the bound is
only compared.

### orient2d: the merged first test

With `detleft` and `detright` the two rounded products and
`det = detleft - detright`, the 1996 code is

```c
if (detleft > 0.0) {
  if (detright <= 0.0) return det;            /* A0 */
  else detsum = detleft + detright;
} else if (detleft < 0.0) {
  if (detright >= 0.0) return det;            /* A0 */
  else detsum = -detleft - detright;
} else {
  return det;                                 /* A0 */
}
errbound = ccwerrboundA * detsum;
if ((det >= errbound) || (-det >= errbound)) return det;   /* A */
return orient2dadapt(pa, pb, pc, detsum);
```

and the library is, leaving its input-domain guard aside,

```c
detsum = fabs(detleft) + fabs(detright);
errbound = ccwerrboundA * detsum;
if (fabs(det) >= errbound) return det;
return orient2dadapt(pa, pb, pc, detsum);
```

(With the guard, this code runs as it is when `errbound` is inside the
guard's window or the input is inside the floating-point domain; the
comparison is then done on the bit patterns, which is the same comparison for
a finite `det` and a finite, nonnegative `errbound`. See
[Input domain](#input-domain).)

Both return either `det`, computed identically, or the value of
`orient2dadapt(pa, pb, pc, detsum)`. So it suffices to show that they choose
the same of the two, and pass the same `detsum`. Let neither product be NaN.

1. *Both products positive, or both negative.* The 1996 code reaches the
   filter with `detsum = detleft + detright` or `(-detleft) - detright`. Both
   are the correctly rounded sum of the magnitudes: negation is exact, and
   round-to-nearest is symmetric, so `(-a) - b`, which is `(-a) + (-b)`,
   equals `fabs(a) + fabs(b)` bit for bit when `a` and `b` are negative. So
   `detsum` and `errbound` are the same doubles in both versions, the two
   tests are the same predicate (previous section, with `>=`), and the same
   `detsum` goes to `orient2dadapt`.
2. *Every other case: the products have opposite signs, or at least one is a
   zero (of either sign).* The 1996 code returns `det`. In the library, the
   real number `detleft - detright` has magnitude exactly
   `|detleft| + |detright|`, because the two terms do not cancel. `det` is
   that difference rounded and `detsum` is that sum rounded, and rounding is
   symmetric, so `fabs(det) == detsum`, including when both overflow to
   infinity and when both are zero. `errbound` is `detsum` times a constant
   less than one, rounded; rounding is monotonic and `detsum` is a double, so
   `errbound <= detsum` (and infinity `>=` infinity holds). The test
   `fabs(det) >= errbound` therefore passes and the library returns `det`
   too. This is why the operator must stay `>=`: with two zero products
   `fabs(det)` and `errbound` are both zero.

The argument uses only that each `+`, `-`, `*` is correctly rounded to
nearest; it holds for subnormal and overflowing products as well, outside the
domain of assumption 3. The two products must stay separately rounded doubles.

If a product is NaN (which needs a NaN or infinite intermediate) the 1996
code returns a NaN or calls `orient2dadapt`. The library's guard never lets
this code see such a product: `errbound` is then a NaN, outside the window,
and the integer stage decides the call (or reports a coordinate that is not
finite).

Tests: `library.stages` requires, for every corpus case the reference decides
in A0, that the library's single test decides it; `library.orient2d_first_test`
compares values and stages on special products.

## Input domain

**Every finite double.** For finite coordinates each predicate returns a
finite value whose sign is the sign of the determinant. There is no exponent
range to respect: subnormal coordinates, coordinates next to the largest
double and any mixture are decided exactly. For a coordinate that is an
infinity or a NaN each predicate returns a NaN. The proof is
[proofs/input-domain.md](proofs/input-domain.md); this section says how the
library gets there.

The 1996 analysis assumes that no operation overflows or underflows, and the
1996 code does not check. Outside an exponent range it returns, silently,
zero for a nonzero determinant, a nonzero value for a zero determinant, the
opposite nonzero sign (when the first filter certifies a determinant computed
from products that underflowed), a NaN, or an infinity of either sign. The
library uses a floating-point stage only where that cannot have happened:

1. **The first filter under a guard.** `det` is returned only if a comparison
   of bit patterns proves that `det` and the error bound are what an unbounded
   exponent range would give (the table above). The guard is relative to the
   size of the coordinates, so it holds far outside the 1996 range too.
2. **The floating-point domain.** With `L` the binary exponent of the lowest
   set bit of any coordinate, `H` the binary exponent of the largest
   magnitude and `d` the degree of the determinant (2, 3, 4, 5), the stages
   behind the first filter are used only if

       H <= 201   and   d L >= -914,

   that is `L >= -457, -304, -228, -182`. Inside this domain no operation of
   stages A to D overflows, and none is rounded in the subnormal range
   (proofs/input-domain.md, section 5). `orient2d`'s stages B and C also run
   under the window of its first test, which is proved to be enough for them.
3. **The integer stage for everything else.** It has a tier for every finite
   input.

| Input | What the library returns |
|---|---|
| finite, any exponents | a finite value with the exact sign |
| a NaN or infinite coordinate | NaN |

**The range of the 1996 paper** (every coordinate zero or with a binary
exponent in [-142, 201]) is where the library's values are comparable with
the 1996 code's. Inside it the stages take the decisions they took before
the guard existed and return the same values, with these exceptions, none of
which changes a sign:

- a call whose first filter certifies while its guard fails is returned with
  the filter's value after a check that the input is in the floating-point
  domain. On the test corpus that is `insphere` with every coordinate below
  2^-120 in magnitude (the error bound is then below the guard's floor), and
  it costs time only;
- `insphere` with a coordinate whose lowest set bit is below 2^-182 is
  outside the floating-point domain (the paper's range reaches 2^-194). A
  call the first filter does not return under its guard is then decided by
  the integer stage and returns -1, 0 or +1 instead of an estimate of the
  determinant. The
  reason: there the product of an error-bound constant and a tiny permanent
  is rounded in the subnormal range, which the paper's analysis does not
  cover.

**What the 1996 code does outside its range** is recorded by
`library.input_domain` as a negative control: on the rescaled corpus slice it
is exact as long as the largest coordinate exponent is at most 510, 339, 254,
203 and the lowest set bit at least 2^-537, 2^-358, 2^-268, 2^-214
(`orient2d`, `orient3d`, `incircle`, `insphere`), that is about 1024 / d and
exactly -floor(1074 / d), and wrong on most of the slice beyond. These are
observations on a sample, not theorems. The library built without guard and
integer stage behaves the same.

## The two return-value contracts

Each predicate has two entry points (`include/shewchuk/predicates.h`).

| Function | Finite coordinates | A NaN or infinite coordinate |
|---|---|---|
| `shewchuk_<name>_sign()` | `+1`, `0` or `-1`: the exact sign | `SHEWCHUK_SIGN_NOT_FINITE` (2) |
| `shewchuk_<name>()` | a finite double with the exact sign; its magnitude means nothing (an estimate of the determinant, or -1.0, 0.0, +1.0, depending on the stage that decides) | NaN |

The sign-returning function is the one to use: an input without a
determinant can never be mistaken for a degenerate one. With the
double-returning function, which is kept because it is the interface of the
1996 code, a caller that maps the result to a sign with `(x > 0) - (x < 0)`,
or that tests only `x > 0` and `x < 0`, turns a NaN into "zero".

`SHEWCHUK_SIGN_NOT_FINITE` is 2: not a sign, and neither is its negation
(code that flips an orientation by negating the result still does not get a
sign). It is not an error code to be ignored: every value other than -1, 0
and +1 means that there was no determinant to take the sign of.

The sign-returning functions are the double-returning ones followed by
`value > 0 ? 1 : value < 0 ? -1 : value == 0 ? 0 : SHEWCHUK_SIGN_NOT_FINITE`,
inside the library's translation unit (a caller compiled with
`-ffinite-math-only` could not make the last comparison). That this is the
contract above follows from [proofs/input-domain.md](proofs/input-domain.md),
Theorem 4: the double-returning function returns a finite value with the
exact sign for finite coordinates and a NaN otherwise, so the three
comparisons give the exact sign in the first case and all fail in the
second. Tested by `library.api`, `library.api_c` and, on every case and
every non-finite call of the input-domain test, by `library.input_domain`.

## What is and is not established

- Established by test: built without the integer stage and the guard, the
  library returns the reference's values bit for bit; with them, every value
  on the corpus is the reference's or, where the integer stage decides,
  exactly the oracle's sign; the reference's signs equal an exact oracle's
  on a corpus that reaches every stage above, including stages B to D with
  inputs too wide for the integer stage; and outside the 1996 range, on
  several hundred thousand inputs up to both ends of the format, the
  library's sign is the oracle's while the reference fails.
- Proved (and tested): the integer stage, in
  [proofs/integer-stage.md](proofs/integer-stage.md); the guard of the first
  filter and the routing of every finite input, in
  [proofs/input-domain.md](proofs/input-domain.md). The latter says which of
  its parts are proofs, which rest on a premise about the 1996 code that was
  established by reading it, and what the tests can and cannot show (a sign
  test cannot validate a threshold: loosened guard floors pass every test).
- Not established by test, and not re-proved here: that each error-bound
  constant is large enough. A loosened constant can pass every sign test
  (issue #11). Until independent derivations exist under `docs/proofs/`, the
  constants rest on the paper and on decades of use.
- Any future change to a constant, a filter expression, a guard, an expansion
  routine or a buffer capacity must update this ledger and add its derivation
  (issue #11).

## Differences from the 1996 code

- The integer stage (above). It changes returned magnitudes, never signs: a
  call it decides returns -1.0, 0.0 or +1.0 where the 1996 code returns an
  approximation of the determinant.
- The input-domain guard (above). It changes results only where the 1996
  code is outside its analysis: there the library returns the exact sign
  (from the integer stage) where the 1996 code returns whatever its
  floating-point stages produce, and a NaN for a coordinate that is not
  finite.

Not arithmetic:

- The constants are compile-time values. Each is checked against the formula
  `exactinit()` uses and against the globals of the pristine build.
- `fast_expansion_sum_zeroelim` does not load `e[elen]` / `f[flen]`. The 1996
  code loads that element (one past the end) and never uses it. The comment
  on the function in `src/predicates.cpp` states the invariant; the
  `library.expansions` test compares every output component with the 1996
  routine's.
