# Exactness ledger

What each stage of each predicate claims, what the claim rests on, and where
the argument is written down. The rule of the project: the sign of every
result is exactly the sign of the determinant, and no change may weaken that.

**Current state.** The library (`src/predicates.cpp`) is a port of the four
adaptive predicates of the 1996 `predicates.c`, with one addition: an exact
integer stage (stage I, `src/integer_stage.h`) that is tried before the
adaptive stages B, C and D (for `orient2d`: before stage D).

- Apart from the rewrites listed under [The filters](#the-filters), which
  cannot change a returned value, the floating-point stages perform the same
  operations in the same order as the 1996 code: built without the integer
  stage, the library returns values bit-identical to the pristine file's on
  the whole test corpus, and with it, every value a floating-point stage
  returns still is (see [testing.md](testing.md)). Their claims below are
  therefore the 1996 paper's claims, inherited unchanged. Nothing there has
  been re-derived yet.
- The integer stage is new, and its claim is proved in
  [proofs/integer-stage.md](proofs/integer-stage.md).

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
| 3 | No intermediate result overflows or underflows. The paper does not analyse overflow or underflow; it states that the four predicates neither overflow nor underflow for inputs whose exponents are in [-142, 201]. | Not checked at run time. The test corpus stays inside this domain. Behaviour outside it is not claimed; see [Input domain](#input-domain). |
| 4 | Gradual underflow is on (no flush-to-zero, no denormals-are-zero mode), so that `x - y == 0` only if `x == y`. Inside the domain of assumption 3 no subnormal number should arise at all; this assumption is what keeps the first sign tests meaningful at the edge. | The caller's responsibility; not checked. |
| 5 | Inputs are finite. | Not checked by the floating-point stages; NaN and infinities give unspecified results. The integer stage detects them and declines, so that their results are the ones of the floating-point stages alone. |

The integer stage needs none of these assumptions: it uses no floating-point
arithmetic. It needs only that a `double` is an IEEE 754 binary64 (checked at
compile time). But it runs only when the stages before it have not decided,
so the predicates as a whole still rest on assumptions 1 to 5.

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
| I | Exact, or declines. Scales the coordinates by one power of two to integers; if their width `W` (bit length of the largest scaled magnitude) is at most 189, evaluates the determinant in fixed-width integers (`W <= 61`: one 64-bit limb per coordinate difference, `<= 125`: two, `<= 189`: three) and returns its sign as -1.0, 0.0 or +1.0. Declines, leaving the call to the next stage, if `W > 189` or a coordinate is not finite. Exact for all finite inputs it accepts, subnormals included. | none | [proofs/integer-stage.md](proofs/integer-stage.md) |

Position: behind stage A for `orient3d`, `incircle` and `insphere`; behind
stage C for `orient2d` (its stages B and C are cheaper than the integer
stage). The stages listed after that position in the tables below run only
when stage I declines.

### orient2d

| Stage | Claim | Bound |
|---|---|---|
| A0 | Exact sign without a bound: the two products have opposite signs or one is zero, so their difference cannot cancel. Relies on assumptions 3 and 4 (a rounded difference or product is zero only if the exact one is). In the library A0 and A are one comparison, see [orient2d: the merged first test](#orient2d-the-merged-first-test). | none |
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

and the library is

```c
detsum = fabs(detleft) + fabs(detright);
errbound = ccwerrboundA * detsum;
if (fabs(det) >= errbound) return det;
return orient2dadapt(pa, pb, pc, detsum);
```

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

If `detleft` is NaN (which needs a NaN or infinite intermediate, excluded by
assumptions 3 and 5) the 1996 code returns `det`, a NaN, at once, and the
library calls `orient2dadapt`; on the cases tried that returns NaN as well. If
only `detright` is NaN both versions call `orient2dadapt`.

Tests: `library.stages` requires, for every corpus case the reference decides
in A0, that the library's single test decides it; `library.orient2d_first_test`
compares values and stages on special products.

## Input domain

**Supported.** Every coordinate is zero or has a binary exponent in
[-142, 201], that is a magnitude in [2^-142, 2^202). This is the range the
paper states. Results outside it are not promised, and nothing checks at run
time that an input is inside it.

**Why that range suffices (a sketch, not a proof).** Let d be the degree of
the determinant in the coordinates: 2 for orient2d, 3 for orient3d, 4 for
incircle, 5 for insphere.

- *Overflow.* Coordinates are below 2^202, differences below 2^203, a product
  of d differences below 2^(203d), at most 2^1015 for insphere. The
  determinant and the `permanent` of the filters are sums of at most 72 such
  products, below 2^1022. The largest operand that `Split` multiplies by
  2^27 + 1 has degree d - 1. So nothing reaches 2^1024.
- *Underflow.* Let 2^L be the lowest set bit of any coordinate; in the
  supported range L >= -142 - 52 = -194. Every coordinate difference, rounded
  or exact, and every tail is a multiple of 2^L, and every product of j of
  them is a multiple of 2^(jL), j <= d. As long as dL >= -1074 all of these
  are multiples of the smallest subnormal number, so rounding one of them
  never loses anything to underflow: each `+ - *` returns what it would
  return with an unbounded exponent range, which is what the paper's
  analysis assumes. In the supported range dL >= -970.

**Observed limits.** `library.input_domain` rescales a slice of the corpus by
powers of two (exact, and sign-preserving) and compares with the oracle. On
that sample every result is exact as long as

| | orient2d | orient3d | incircle | insphere |
|---|---|---|---|---|
| binary exponent of the largest coordinate is at most | 510 | 339 | 254 | 203 |
| lowest set bit of every coordinate is at least 2^L, L = | -537 | -358 | -268 | -214 |

and the first failures appear one step further. The lower row is exactly
-floor(1074 / d), the condition of the sketch; the upper row is about
1024 / d. These are observations on a sample. Only the range above is
supported.

**Current behaviour outside the domain** (recorded by the same test so that a
change is noticed; not a contract):

| Input | What the library returns today |
|---|---|
| too large (an intermediate product overflows) | NaN; orient2d can also return an infinity, which had the exact sign on the sample. Every result that was still finite had the exact sign |
| too small (a roundoff term underflows) | a finite value whose sign can be wrong, with nothing to signal it: zero for a nonzero determinant, or nonzero for a zero determinant. An opposite nonzero sign was not observed |
| a NaN coordinate | NaN |
| an infinite coordinate | NaN; orient2d can also return an infinity |

A caller that maps the result to a sign with `(x > 0) - (x < 0)` turns a NaN
into 0, "degenerate". Inputs that may be outside the domain must be checked
by the caller.

## What is and is not established

- Established by test: built without the integer stage the library returns
  the reference's values bit for bit; with it, every value is the
  reference's or, where the integer stage decides, exactly the oracle's sign;
  and the reference's signs equal an exact oracle's on a corpus that reaches
  every stage above, including stages B to D with inputs too wide for the
  integer stage.
- Proved (and tested): the integer stage, in
  [proofs/integer-stage.md](proofs/integer-stage.md).
- Not established by test, and not re-proved here: that each error-bound
  constant is large enough. A loosened constant can pass every sign test
  (issue #11). Until independent derivations exist under `docs/proofs/`, the
  constants rest on the paper and on decades of use.
- Any future change to a constant, a filter expression, an expansion routine
  or a buffer capacity must update this ledger and add its derivation
  (issue #11).

## Differences from the 1996 code

- The integer stage (above). It changes returned magnitudes, never signs: a
  call it decides returns -1.0, 0.0 or +1.0 where the 1996 code returns an
  approximation of the determinant.

Not arithmetic:

- The constants are compile-time values. Each is checked against the formula
  `exactinit()` uses and against the globals of the pristine build.
- `fast_expansion_sum_zeroelim` does not load `e[elen]` / `f[flen]`. The 1996
  code loads that element (one past the end) and never uses it. The comment
  on the function in `src/predicates.cpp` states the invariant; the
  `library.expansions` test compares every output component with the 1996
  routine's.
