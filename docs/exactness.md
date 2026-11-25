# Exactness ledger

What each stage of each predicate claims, what the claim rests on, and where
the argument is written down. The rule of the project: the sign of every
result is exactly the sign of the determinant, and no change may weaken that.

**Current state.** The library (`src/predicates.cpp`) is a port of the four
adaptive predicates of the 1996 `predicates.c`. It performs the same
floating-point operations in the same order and changes no arithmetic: on the
whole test corpus every returned value is bit-identical to the pristine file's
(see [testing.md](testing.md)). Every claim below is therefore the 1996
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
| A0 | Exact sign without a bound: the two products have opposite signs or one is zero, so their difference cannot cancel. Relies on assumptions 3 and 4 (a rounded difference or product is zero only if the exact one is). | none |
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
