# Exactness ledger

What each stage of each predicate claims, what the claim rests on, and where
the argument is written down. The rule of the project: the sign of every
result is exactly the sign of the determinant, and no change may weaken that.

**Current state.** The library (`src/predicates.cpp`) is a port of the four
adaptive predicates of the 1996 `predicates.c`. Apart from the rewrites listed
under [The filters](#the-filters), which cannot change a returned
value, it performs the same floating-point operations in the same order: on
the whole test corpus every returned value is bit-identical to the pristine
file's (see [testing.md](testing.md)). Every claim below is therefore the 1996
paper's claim, inherited unchanged. Nothing here has been re-derived yet.

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
| 3 | No intermediate result overflows or underflows. The paper does not analyse overflow or underflow; it states that the four predicates neither overflow nor underflow for inputs whose exponents are in [-142, 201]. | Not checked at run time. The test corpus stays inside this domain. Behaviour outside it is not claimed (issue #5). |
| 4 | Gradual underflow is on (no flush-to-zero, no denormals-are-zero mode), so that `x - y == 0` only if `x == y`. Inside the domain of assumption 3 no subnormal number should arise at all; this assumption is what keeps the first sign tests meaningful at the edge. | The caller's responsibility; not checked. |
| 5 | Inputs are finite. | Not checked; NaN and infinities give unspecified results. |

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

## What is and is not established

- Established by test: the library returns the reference's values bit for
  bit, and the reference's signs equal an exact oracle's on a corpus that
  reaches every stage above.
- Not established by test, and not re-proved here: that each error-bound
  constant is large enough. A loosened constant can pass every sign test
  (issue #11). Until independent derivations exist under `docs/proofs/`, the
  constants rest on the paper and on decades of use.
- Any future change to a constant, a filter expression, an expansion routine
  or a buffer capacity must update this ledger and add its derivation
  (issue #11).

## Differences from the 1996 code that are not arithmetic

- The constants are compile-time values. Each is checked against the formula
  `exactinit()` uses and against the globals of the pristine build.
- `fast_expansion_sum_zeroelim` does not load `e[elen]` / `f[flen]`. The 1996
  code loads that element (one past the end) and never uses it. The comment
  on the function in `src/predicates.cpp` states the invariant; the
  `library.expansions` test compares every output component with the 1996
  routine's.
