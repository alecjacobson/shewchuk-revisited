# Proof: the exact sign for every finite input

Code: [`src/filter_guard.h`](../../src/filter_guard.h) (the guard of the first
filter, and its derivation as code), [`src/predicates.cpp`](../../src/predicates.cpp)
(where the guard and the routing are), [`src/integer_stage.h`](../../src/integer_stage.h)
(`in_floating_point_domain()`, `Request`, the wide tier). Ledger:
[`docs/exactness.md`](../exactness.md).

## 1. Claim

For `orient2d`, `orient3d`, `incircle` and `insphere`, and **any** coordinates
(any bit patterns):

- if every coordinate is finite, the function returns a finite double whose
  sign is the sign of the determinant. Subnormal coordinates, coordinates
  near the largest double, and any mix of them are included: there is no
  exponent domain;
- if a coordinate is an infinity or a NaN, the function returns a NaN.

The 1996 code does not have this property. Its analysis assumes that no
operation overflows or underflows; where one does, it returns zero for a
nonzero determinant, a nonzero value for a zero determinant, the opposite
nonzero sign, a NaN, or an infinity of either sign, with nothing to signal it.

The claim rests on four things, and on nothing else:

| | | status |
|---|---|---|
| R1 | Each `+ - *` of the library is one correctly rounded binary64 operation, round to nearest, ties to even, with gradual underflow (assumptions 1, 2 and 4 of the ledger). | assumed, as before |
| R2 | The claims of the 1996 stages (ledger, "Stages") **for arithmetic with an unbounded exponent range**: that is what the paper analyses when it assumes that nothing overflows or underflows. | inherited from the paper, as before |
| R3 | The integer stage returns the exact sign of every finite input it is asked to decide ([integer-stage.md](integer-stage.md)). | proved there |
| R4 | A premise about the structure of the 1996 code, used only in section 5 (which values it multiplies), and the paper's statement that nothing overflows for coordinates below 2^202. | by inspection, and inherited; tested (section 10) |

Sections 2 to 4 are about the first tests, the code almost every call ends
in. Section 5 is about the stages behind them. Section 6 puts the routing
together. Section 10 says what is proved, what is only tested, and what the
mutation tests showed.

## 2. Two arithmetics, and clean values

Let **F** be binary64 arithmetic as the processor does it (R1), and **F∞** the
same arithmetic with an unbounded exponent range: 53-bit significands, every
exponent, the same rounding. Every double, subnormal or not, is a number of
F∞. For a program without branches, write `v` for a value it computes in F
and `v'` for the value the same operations give in F∞ on the same inputs.

**Definition.** `v` is *clean* if it is finite and `v = v'`.

If every value a stage compares is clean, the stage takes the decision it
takes in F∞, and R2 says that decision is right. So the question is when
values stop being clean, and what can be said about them then.

The two arithmetics differ only in two ways. *Overflow*: F returns an
infinity where F∞ returns a number of magnitude 2^1024 or more.
*Underflow*: for a real number `r` with `|r| < 2^-1022`, F rounds `r` to a
multiple of 2^-1074, F∞ to 53 significant bits. For `|r| >= 2^-1022` (below
overflow) both round `r` to the same double.

**Lemma G (grid).** If `r` is an integer multiple of 2^-1074 and
`|r| < 2^1024`, then F and F∞ round `r` to the same double.
*Proof.* If `|r| >= 2^-1022`, see above. Otherwise `r` is a multiple of
2^-1074 below 2^-1022 in magnitude: it has at most 52 significant bits and is
a double. Both arithmetics return it unchanged. ∎

**Lemma D (sums of clean values).** If `x` and `y` are clean and `x ± y` does
not overflow in F, then `fl(x ± y)` is clean. In particular every coordinate
difference `a - b` that does not overflow is clean.
*Proof.* `x` and `y` are doubles, so `x ± y` is a multiple of 2^-1074. Lemma
G. ∎

**Lemma P (products).** Let `x` and `y` be clean and `xy` not overflow in F.
If `|xy| >= 2^-1022` then `fl(xy)` is clean. Otherwise `|fl(xy)| <= 2^-1022`
and `|fl'(xy)| <= 2^-1022`. So: *a product of clean values that is larger
than 2^-1022 in magnitude is clean* (if `|fl(xy)| > 2^-1022` then
`|xy| >= 2^-1022`).
*Proof.* The first case is the remark before Lemma G. In the second,
rounding is monotonic and 2^-1022 is a number of both arithmetics. ∎

To follow values that are not clean, call `v` *β-controlled*, for a real
number β >= 0, if `v` is clean **or** both `|v| <= β` and `|v'| <= β`. A
clean value is 0-controlled; a value larger than β that is β-controlled is
clean. Lemma P says that the product of two clean values is
2^-1022-controlled.

**Lemma S (sums).** If `x` is β_x-controlled, `y` is β_y-controlled, β a power
of two with β >= max(β_x, β_y), and the sum does not overflow, then
`fl(x ± y)` is 2^56 β-controlled.
*Proof.* If both are clean, Lemma D. If neither is clean, `|x ± y| <= 2β` in
both arithmetics, and so is the rounded sum (monotonic rounding; 2β is a
number of F∞, and in F it is a double or lies beyond every finite one). Let
`x` be clean and `y` not, so `|y|, |y'| <= β`.

- If `|x| >= 2^55 β`: let `2^e <= |x| < 2^(e+1)`. The doubles nearest to `x`
  are at distance at least 2^(e-53) from it (the gap below a power of two is
  half the gap above it), in F and in F∞ (the gaps of F are those of F∞, or
  larger in the subnormal range). `|y| <= 2^-55 |x| < 2^(e-54)`, less than
  half that distance, and the same holds for `y'`. So both arithmetics return
  `x`: the sum is clean.
- Otherwise `|x ± y| < (2^55 + 1) β <= 2^56 β` in both arithmetics, and so is
  the rounded sum. ∎

**Lemma P' (products with bounds).** Let `x` be β_x-controlled with
`|x|, |x'| <= X`, and `y` be β_y-controlled with `|y|, |y'| <= Y`, where β_x,
β_y, X, Y are powers of two (or β = 0 for a clean factor), and let the product
not overflow. Then `fl(xy)` is max(2^-1022, β_x Y, X β_y)-controlled, and
`|fl(xy)|, |fl'(xy)| <= XY`.
*Proof.* Both clean: Lemma P. If `x` is not clean, `|xy| <= β_x Y` and
`|x'y'| <= β_x Y`, and the rounded products obey the same bound (monotonic
rounding; if β_x Y is below 2^-1074 the F product is at most 2^-1074, which
is below 2^-1022). Symmetrically for `y`. ∎

Sizes propagate the obvious way: if `|x| <= X` and `|y| <= Y` with powers of
two `X >= Y`, then `|fl(x ± y)| <= 2X`, in both arithmetics.

`src/filter_guard.h` is these lemmas as `constexpr` functions (`sum`,
`product`, `absolute`) on a small record per value: whether it is clean, its
control `2^control2 * D^controlD`, its size `2^size2 * D^sizeD`. `D` is a
power of two, `D >= 1`, that bounds the magnitude of certain coordinate
differences; because `D >= 1`, the maximum of two terms `2^a D^i` and
`2^b D^j` is at most `2^max(a,b) D^max(i,j)`, which is how the code takes
maxima.

## 3. The guard of the first filter: orient3d, incircle, insphere

The first filter computes `det` and `permanent` in plain double arithmetic
and returns `det` if `|det| > errbound`, `errbound = c * permanent` with `c`
the constant of the predicate (`o3derrboundA` etc., all below 2^-48). The
library returns `det` from this stage only if two functions of
`src/filter_guard.h` both say yes. Both read bit patterns and compare
integers:

- `filter_certifies(det, errbound)`: the 1996 test, and `det` is finite;
- the *guard*, `holds()` or `holds_for_squares()`: `errbound` is at least
  `2^floor * D^p` for a power of two `D >= 1` that bounds the differences
  that need a bound.

**What the derivation gives.** Evaluating the expressions of each filter with
the rules of section 2 (`orient3d_filter()`, `incircle_filter()`,
`insphere_filter()` mirror the expressions of `src/predicates.cpp` term by
term, in the order the code adds them) gives:

| | bounded differences | `det` is controlled by | `permanent` is controlled by | guard: `errbound >=` |
|---|---|---|---|---|
| `orient3d` | the z differences `adz, bdz, cdz` | 2^-854 D | 2^-854 D | 2^-853 D |
| `incircle` | all six | 2^-853 D² | 2^-853 D² | 2^-852 D² |
| `insphere` | all twelve | 2^-740 D³ | 2^-684 D³ | 2^-683 D³ |

where `D >= 1` is a power of two with `|difference| <= D` for every bounded
difference. For `orient3d` the x and y differences need no bound: they are
multiplied only with each other, and the products are clean or at most
2^-1022 whatever their size; only a z difference multiplies a value (a 2x2
minor) that may be unclean. The `static_assert`s in the header check that
each guard exceeds both controls, that the power of `D` is at least the
derived one, and that the three floors (-853, -852, -683) are exactly one
above the derived controls: the proof stops there, and a lower floor does
not compile.

Where the numbers come from, for `orient3d`: a product of two differences is
2^-1022-controlled (Lemma P); a 2x2 minor, the difference of two of them, is
2^-966-controlled (Lemma S); a z difference times a minor is 2^-966
D-controlled (Lemma P'); the sum of three such terms is 2^-854 D-controlled
(Lemma S twice). The permanent has the same shape.

**Theorem 1.** Let the coordinates be finite. If `filter_certifies()` and the
guard both hold in the first filter of `orient3d`, `incircle` or `insphere`,
then `det` has the sign of the determinant.

*Proof.* Let `a` be the bit pattern of `det` without its sign bit and `e` the
bit pattern of `errbound`. For nonnegative doubles the bit pattern, read as an
unsigned integer, increases with the value; +infinity has the pattern
`infinity_bits`, every NaN a larger one once its sign bit is cleared, and
every double with the sign bit set a pattern of at least 2^63.

*Step 1: nothing overflowed.* `filter_certifies()` is `e < a < infinity_bits`.
So `det` is finite. An operation `+ - *` with an operand that is not finite
does not return a finite value, so every intermediate of `det` is finite,
including every coordinate difference (each one is a factor of a term of
`det`). And `e` is the pattern of a finite `errbound` that is not negative;
`permanent` is a sum of products of absolute values, so by the same argument
all its intermediates are finite. So no operation of the filter overflowed,
and each returned the correctly rounded result. Also, `|det| > errbound`
holds as a comparison of doubles: for these patterns the two orders agree.

*Step 2: the bound D.* The guard tests `e >= floor_bits`, the pattern of
`2^floor`, a normal number. So the exponent field of `errbound` is `x >= 1`,
`errbound >= 2^(x - 1023)`, and with `q = x - (floor + 1023) >= 0`,
`errbound >= 2^(floor + q)`. The exponent field of `e - floor_bits` is `q`
(the subtraction leaves the fraction field alone), and
`difference_bound_exponent()` returns `j = floor(q / p)`, with `p` the power
of `D` in the guard (`third()` is `floor(q / 3)`, checked for every
`q < 2^12` at compile time). Let `D = 2^j`; `j >= 0`, so `D >= 1`, and

    2^floor * D^p = 2^(floor + p j) <= 2^(floor + q) <= errbound.

It remains to show that `D` bounds the bounded differences, in both
arithmetics (they are clean by Lemma D, so there is one value).

- `holds()` (`orient3d`: the z coordinates of the four points; `incircle`:
  all eight coordinates) tests that the pattern of every given coordinate,
  without its sign, is below `limit`, and `limit` is at most
  `(1022 + j) * 2^52`: exactly that for `p = 3`; for `p = 1, 2` it is
  `(1021 + j) * 2^52` plus the bits the shift by `p - 1` leaves in the
  fraction field, which are less than 2^52. A coordinate whose pattern
  without the sign is below `(F + 1) * 2^52` has an exponent field of at most
  `F`, hence a magnitude below 2^(F - 1022) (also if it is subnormal or
  zero). With `F = 1021 + j`: every tested coordinate is below `2^(j - 1)`,
  the difference of two of them is below `2^j`, and the rounded difference is
  at most `2^j = D`.
- `holds_for_squares()` (`insphere`) tests that each given value is below
  `2^(2j - 1)`. The values are the four lifted coordinates
  `alift ... dlift`, each a rounded sum of the three rounded squares of one
  point's differences, or (when compiled by clang, where that costs less)
  their rounded sum; either way the rounded square `fl(x * x)` of every
  bounded difference `x` is a term of a sum of nonnegative terms that the
  test bounds, and rounding is monotonic, so `fl(x * x) < 2^(2j - 1)`. If
  `x * x < 2^-1022` then `|x| < 2^-511 <= D`. Otherwise the product was
  rounded with a relative error of at most 2^-53, so
  `x * x < 2^(2j - 1) / (1 - 2^-53) < 2^(2j)` and `|x| < D`. (A sum of
  finite lifts that overflowed to +infinity passes the test only if
  `2^(2j - 1)` is beyond every double, `j >= 513`; then `D >= 2^513` bounds
  every `x` anyway, since `fl(x * x)` is finite by step 1.)

*Step 3: the three compared values are clean.* By step 1 no operation
overflowed, so the lemmas of section 2 apply and give the controls of the
table. `errbound = fl(c * permanent) <= permanent`, because `c < 1` and
`permanent` is a double. So `permanent >= errbound >= 2^floor D^p`, which
exceeds its control: `permanent` is clean. `errbound` is the product of the
clean values `c` and `permanent` and is larger than 2^-1022: clean (Lemma P).
`|det| > errbound`, which exceeds the control of `det`: clean.

*Step 4.* So `det = det'`, `errbound = errbound'` and `|det'| > errbound'`:
in F∞ the first filter certifies `det'`, and by R2 `det'` has the sign of the
determinant. ∎

Two remarks. The theorem needs no assumption on the coordinates beyond
finiteness: it holds for inputs far outside the 1996 domain, and many of them
do pass the guard (the guard is relative: it compares the error bound with
the size of the coordinates). And it uses R2 as a black box: no error-bound
constant is re-derived here.

## 4. orient2d: the window, and stages B and C

The first test of `orient2d` computes `detleft`, `detright`,
`det = detleft - detright`, `detsum = |detleft| + |detright|` and
`errbound = ccwerrboundA * detsum`, and returns `det` if `|det| >= errbound`
(one test for stages A0 and A, see the ledger). No value is amplified by a
coordinate difference here, so the guard is a constant *window*:

    2^-600 <= errbound < 2^950.

Inside the window the first test is trusted, and if it does not return,
`orient2dadapt()` runs (stages B, Bx, C), and behind it
`orient2d_after_stage_c()` (the integer stage, or stage D).
The window test is one unsigned comparison of the bit pattern of `errbound`;
a NaN or a negative zero has a pattern above the ceiling's.

**Theorem 2.** Let the coordinates be finite and `errbound` be inside the
window. Then:

1. no operation of the first test overflowed, and `det`, `detsum` and
   `errbound` are clean; if `|det| >= errbound`, `det` has the sign of the
   determinant;
2. otherwise, in `orient2dadapt()`: if the estimate `det` of stage B is not a
   NaN or an infinity, the expansion `B` is the exact determinant of the
   rounded differences and stages B and Bx decide as in F∞; stage C decides
   as in F∞.

*Proof of 1.* `errbound` is finite, so `detsum` is, so both products are, so
all four differences are (a product with a factor that is not finite is not
finite). `|det| <= detsum` by monotonic rounding. `orient2d_filter()` gives:
`det` and `detsum` are 2^-966-controlled. `detsum >= errbound >= 2^-600`
(`ccwerrboundA < 1`): clean. `errbound > 2^-1022`: clean (Lemma P).
If `|det| >= errbound`, then `|det| >= 2^-600`: clean. So the test passes in
F∞, with the same `det`, and R2 applies (the equivalence of the merged test
with the 1996 stages A0 and A, in the ledger, uses only correct rounding and
holds in F∞). The integer comparison of the code is `|det| >= errbound`
because both are finite and `errbound` is not negative. ∎

*Proof of 2.* Here `|det| < errbound`.

*(a) The two products are large.* `ccwerrboundA < 2^-51`, so
`errbound <= 2^-51 detsum` and `detsum >= 2^-549`. If the products had
opposite signs or one were zero, `|det|` would equal `detsum`, which is at
least `errbound`. So they have the same sign, neither is zero, and
`|detleft - detright| <= 2 |det| < 2^-50 detsum`: each product is at least a
quarter of `detsum`, so at least 2^-551 in magnitude. Also
`detsum < 2^52 errbound < 2^1002`.

*(b) `Two_Product` is exact unless `Split` overflows.* Let `a`, `b` be the two
factors of one product, `|ab| >= 2^-552`. Let `u_a` be the unit in the last
place of `a` (2^-1074 if `a` is subnormal), likewise `u_b`. Every value
`Split` derives from `a` is a multiple of `u_a`: the splitter is an integer,
sums and differences of multiples of `u_a` are multiples of `u_a`, and so is
their rounding. So is every exact result in `Split`, and since
`u_a >= 2^-1074` they are clean by Lemma G. The products in
`Two_Product_Tail` are multiples of `u_a u_b`, and `u_a u_b >= 2^-1074`: if
both factors are normal, `u_a u_b > 2^-106 |ab| >= 2^-658`; if `a` is
subnormal, `|b| > 2^-552 / 2^-1022 = 2^470`, so `u_b >= 2^417` and
`u_a u_b >= 2^-657`. So every operation of `Two_Product` returns what it
returns in F∞ (Lemma G), where the paper's Theorem 18 says the result is
exact, provided nothing overflowed. The products themselves are below 2^1003,
and so are their halves' products up to a factor below 2. What can overflow
is `splitter * a` for `|a| >= 2^996` or so. Then `c` is an infinity,
`abig = c - a` is the same infinity, `ahi = c - abig` is a NaN, the tail of
the product is a NaN, every component of `B` is computed from it, and the
estimate `det` is a NaN. Hence: if `det` is finite, no `Split` overflowed,
both products with their tails are exact, the sums of `Two_Two_Diff` are
below 2^1005 and exact as in F∞, `B` is the exact determinant of the rounded
differences, and all of it is clean. The code tests the bit pattern of `det`
first and hands a call whose `det` is not finite to the integer stage
(`orient2d_exact()`).

*(c) Stages B and Bx.* `errbound = ccwerrboundB * detsum >= 2^-53 * 2^-549`:
clean. With `B` and its estimate clean, the test of stage B is the F∞ test.
The tails of the differences are sums of clean values (Lemma D); if all are
zero, stage Bx returns the estimate of the exact determinant, as in F∞.

*(d) Stage C.* The correction is a difference of two sums of two products of
clean values: 2^(-1022 + 2*56)-controlled; added to the clean estimate of
stage B, `det` is 2^(-1022 + 3*56) = 2^-854-controlled. The error bound is
`ccwerrboundC * detsum + resulterrbound * |det|`. Its first term is at least
2^-104 * 2^-549 = 2^-653: clean, and at least 2^55 * 2^-1022, so by the first
case of Lemma S the sum is clean whether or not the second term is. If stage
C certifies, `|det| >= errbound >= 2^-653 > 2^-854`: `det` is clean, and the
test is the F∞ test. ∎

`static_assert`s in `src/filter_guard.h` tie the window to this proof: the
floor exceeds the controls of the first test, `floor + 51 - 104` exceeds
`3 * 56 - 1022 + 56`, and `ceiling + 53 + 3 < 1024`. Those in
`src/predicates.cpp` check the facts about the constants (`ccwerrboundA <
2^-51`, `ccwerrboundC > 2^-104`, every first-filter constant below 2^-48).

After stage C, `orient2d_after_stage_c()` asks the integer stage, which decides the
call unless its coordinates are wider than 189 bits **and** inside the
floating-point domain of section 5; only then does stage D run
(`orient2d_stage_d()`). Stage D starts from the expansion `B` and the
difference tails of stage B; the code computes them a second time there,
with the operations and operands of `orient2dadapt()`, so they are the same
doubles, and Theorem 3 covers them like every other value of stage D.

**Two zero products.** Outside the window the first test returns `det` in
one more case, tested right there: `errbound` is +0 and each product has a
factor that is zero (`acx == 0 || bcy == 0`, and `acy == 0 || bcx == 0`).
`errbound` is +0 only if it is not a NaN, so `detsum` and both products are
finite; a rounded difference of two doubles is zero only if they are equal
(gradual underflow, R1), so a zero factor is an exactly zero difference, both
exact products are zero, the determinant is zero, and `det`, a difference of
two zeros, is a zero. This is the case points on an axis-parallel line and
repeated points reach, and the 1996 code returns the same zero for it.
Products of nonzero differences that underflowed to zero do not have a zero
factor and do not pass.

## 5. The floating-point domain

Let `L` be the binary exponent of the lowest set bit of any coordinate, `H`
the binary exponent of the largest coordinate magnitude, and `d` the degree
of the determinant (2, 3, 4, 5).

**Definition.** The input is in the *floating-point domain* if

    H <= 201   and   d L >= -914.

(`in_floating_point_domain()` in `src/integer_stage.h`, computed from the
integers `low` and `high` that `scale()` derives from the bit patterns;
`-914 = -1074 + 160`.) For `L`, that is `L >= -457, -304, -228, -182` for
`orient2d`, `orient3d`, `incircle`, `insphere`.

**Theorem 3.** For an input in the floating-point domain, no operation of
stages A, B, C and D of the predicate overflows, and every value they compute
is clean. Hence they decide as in F∞, and R2 applies to them as they are.

*Proof, underflow.* Let `u = 2^min(L, 0)`. Every coordinate is an integer
multiple of `u`. Give every value the stages compute a *degree*: the number
of coordinate factors in it (a coordinate, a difference or a tail has degree
1; a product has the sum of the degrees of its factors; a sum has the larger
degree of its operands). The premise R4 is:

> every multiplication in stages A to D is (i) of two values whose degrees
> add up to at most `d`, or (ii) of the splitter 2^27 + 1 or the constant 2
> with a value, or (iii) of an error-bound constant with a value of degree at
> most `d`, and a value that has an error-bound constant as a factor is never
> multiplied again.

That is the dimensional consistency of the code: every value is a part of the
determinant, which is homogeneous of degree `d`, or an error bound for such a
part. Given R4, by induction every exact result of an operation is an integer
multiple of `u^j` for a value of degree `j` without an error-bound constant,
and of `2^-160 u^j` with one: sums and integer multiples of multiples of a
power of two are multiples of it, so are their roundings, and every
error-bound constant is a multiple of 2^-160 (`on_error_constant_grid()` in
`src/predicates.cpp`, a `static_assert` per constant; the lowest is
`o3derrboundC`, a multiple of 2^-154). Since `j <= d` and `u <= 1`,
`2^-160 u^j >= 2^(d min(L, 0) - 160) >= 2^-1074`. So every exact result is a
multiple of 2^-1074, and by Lemma G and induction every value is clean, as
long as nothing overflows.

*Proof, overflow.* The 1996 paper states that the predicates do not overflow
for coordinates with exponents up to 201; the library keeps that limit, and
this proof does not extend it. For the record, the bound behind it: every
coordinate is below `X = 2^(H+1) <= 2^202`, every difference at most `2X`.
Every double in a stage is a rounded partial sum of terms of the expanded
determinant, or a part of one such term, and the sum of the magnitudes of
all terms is at most `N (2X)^d` with `N = 2, 6, 12, 72` terms (for
`insphereexact()`, which works on the coordinates: 360 terms of size `X^5`).
For `insphere` that is `72 * 2^1015 < 2^1022`, and the largest value `Split`
multiplies by `2^27 + 1` has degree `d - 1`. The buffer sizes of the code
agree with these counts (16, 192, 1152 and 5760 components are the terms of
the fully expanded determinants, with tails, times 2^(d-1)). ∎

*Status.* The underflow half is a proof from R4. R4 itself is established by
reading the code, not mechanically. The overflow half is the paper's
statement with the supporting bound. Both halves are tested where a test can
see them: at the corners of the domain no call that a floating-point stage
decides raises the underflow or the overflow exception flag (section 10).
Neither half is new risk: before this change the same stages ran on every
input, and now they run only inside this domain.

The domain is smaller than "exponents in [-142, 201]", the range of the
paper, in one place: for `insphere` the paper's range allows `L` down to
-194, the domain only to -182. Below that, the product of an error-bound
constant and a tiny `permanent` is rounded in the subnormal range, which the
paper's analysis does not cover. Such inputs are decided by the first filter
under its guard or by the integer stage.

## 6. Every finite input is decided exactly

**Theorem 4.** For finite coordinates each predicate returns a finite value
with the sign of the determinant. For coordinates that are not all finite it
returns a NaN.

*Proof.* By cases, following the code. The integer stage
([integer-stage.md](integer-stage.md), section 7) returns `not_finite` if a
coordinate is not finite; `refused` only
if the input is in the floating-point domain (and, unless asked with
`prefer_floating_point`, wider than 189 bits); never `refused` if asked with
`always_exact`; otherwise the exact sign (R3).

`orient3d`, `incircle`, `insphere`:

1. `filter_certifies()` and the guard hold: `det` is returned. Theorem 1.
2. `filter_certifies()` holds, the guard does not: `*_guard_failed()` asks
   the integer stage with `prefer_floating_point`. A sign is returned as it
   is. `refused`: the input is in the floating-point domain, so stage A
   computed clean values (Theorem 3), its certification is the F∞
   certification (R2), and `det` is returned.
3. `filter_certifies()` does not hold: `*_after_filter()` asks the integer
   stage with the default request. A sign is returned as it is. `refused`:
   the adaptive stages B, C, D of 1996 run, on an input in the floating-point
   domain (Theorem 3, R2).

`orient2d`:

1. `errbound` inside the window and `|det| >= errbound`: `det`. Theorem 2.
2. Inside the window otherwise: `orient2dadapt()`. Stage B's estimate not
   finite: the integer stage with `always_exact` (the coordinates are finite
   by Theorem 2, part 1). Stages B, Bx, C: Theorem 2. Then the integer stage
   with the default request: a sign, or `refused`, and stage D runs on an
   input in the floating-point domain (Theorem 3, R2).
3. Outside the window, two zero products with a zero factor each: `det`, a
   zero (section 4).
4. Outside the window otherwise: `orient2d_outside_window()` asks the integer
   stage with `prefer_floating_point`: a sign, or `refused`: the input is in
   the floating-point domain, and the 1996 code runs as it is, first test
   included (Theorem 3, R2).

*Coordinates that are not finite.* A coordinate that is an infinity or a NaN
makes every difference it is part of an infinity or a NaN, and with it every
product and sum that depends on that difference. For `orient3d`, `incircle`
and `insphere`, every coordinate is part of a difference that is a factor in
`det`, so `det` is not finite and `filter_certifies()` is false;
`*_after_filter()` gets `not_finite` from the integer stage and returns NaN.
For `orient2d`, every coordinate is part of a difference that is a factor of
`detleft` or `detright`, so `detsum` and `errbound` are not finite: the
window fails (the bit pattern of +infinity or a NaN is above the ceiling's,
or has the sign bit set), `errbound` is not +0, and the integer stage returns
`not_finite`.

*Finite results.* Every returned value is `det` of a first test (finite by
Theorems 1 and 2, or a zero), a sign from the integer stage, or a value of a
1996 stage inside the floating-point domain, where nothing overflows. ∎

## 7. The wide tier, and the stack

The integer stage's tiers are instances of one template, `Tier<D>`, whose
proof ([integer-stage.md](integer-stage.md), sections 3 to 6) is for every
`D`: tier `D` holds every intermediate for widths up to `64 D - 3`. The
largest width of a finite input is 2098 (highest set bit at most 2^1023,
lowest at least 2^-1074, `scale()`). `wide_tier = 33` gives
`64 * 33 - 3 = 2109 >= 2098`; 32 would give 2045. `static_assert`s check
both, and `tier_holds_every_intermediate(33, 2109)`.

The wide tier works on fixed-size integers on the stack; nothing is
allocated on the heap anywhere in the library. Its largest value is the
`insphere` determinant, `5 * 33` limbs (1320 bytes). The stack frames of the
functions that evaluate the wide tier's determinants, as the compilers report
them (`-fstack-usage`), in bytes:

| wide tier of | GCC 16 `-O3` | Apple clang `-O3` | GCC 16 `-O0` | Apple clang `-O0` |
|---|--:|--:|--:|--:|
| `orient2d` | 3,872 | 5,376 | 10,816 | 14,176 |
| `orient3d` | 8,112 | 9,968 | 34,704 | 42,944 |
| `incircle` | 11,776 | 11,840 | 42,496 | 48,192 |
| `insphere` | 21,840 | 25,296 | 113,968 | 130,448 |

They are fixed: the code has no recursion and no array whose size depends on
the input. Below each of them there is one frame of `mul_out_of_line`, at
most 464 bytes (its result is written to the caller's frame); above them the
frames of the stage's entry functions (at most 2,100 bytes optimized) and of
the predicate.

What that does to the library's worst-case stack use, which `insphere` sets:
the largest frame the library had before, and still has, holds the arrays of
`insphereexact()`: about 214,000 bytes optimized, 181,000 unoptimized.

- GCC keeps `insphereadapt()` (with `insphereexact()` inlined) a function of
  its own, which the wide tier never runs together with: the worst case stays
  at about 214 KB.
- clang inlines it into `shewchuk_insphere()`, whose frame is therefore
  214 KB on every call; the wide tier's 25 KB and the entry functions' 2 KB
  come on top: about 242 KB, up from about 216 KB.
- Unoptimized, the deepest chain is still the exact stage (about 215 KB);
  the wide tier's is about 135 KB.

These numbers are measurements of three compilers on one target, not a
guarantee for every compiler; the wide tier's functions are never inlined
into the fast tiers (`SHEWCHUK_INTEGER_STAGE_NOINLINE`), so its frames are
paid only by calls that reach it.

The wide tier is slow: its integers have up to 10560 bits and the
multiplication is the schoolbook one. On an Apple M4 (Apple clang, wall
clock, coordinates with exponents from the whole range and one repeated
point, so that no filter decides) a call takes about 1.5 µs (`orient2d`),
8.5 µs (`orient3d`), 16 µs (`incircle`) and 51 µs (`insphere`). Tiers
between 3 and 33 limbs would make moderately wide inputs faster; there are
none yet.

It is reached only by inputs that are wider than 189 bits, outside the
floating-point domain, and not decided by the first filter under its guard.

## 8. Inputs inside the range of the 1996 paper

For inputs whose coordinates are zero or have exponents in [-142, 201], the
library takes the decisions it took before this change and returns the same
values, with these exceptions, none of which changes a sign:

- **Route only, same value.** A call whose first filter certifies but whose
  guard fails goes through `*_guard_failed()`, which finds the input in the
  floating-point domain and returns the filter's `det` (case 2 of Theorem
  4). On the test corpus this happens only for `insphere` with every
  coordinate below 2^-120 in magnitude, where `errbound` is below the
  guard's floor 2^-683. `orient2d` with two zero products leaves the first
  test by its own branch and returns the same zero.
- **Different value, same sign.** `insphere` with a coordinate whose lowest
  set bit is below 2^-182 (possible in the paper's range, which goes down to
  2^-194): outside the floating-point domain, so a call the first filter
  does not return under its guard is decided by the integer stage, which
  returns -1, 0 or +1 where a 1996 stage would have returned an estimate of
  the determinant.

The test corpus is inside the paper's range and its recorded value hashes are
unchanged.

## 9. What is not claimed

- Nothing about inputs with a coordinate that is not finite beyond "the
  result is a NaN" (and, for the sign-returning functions,
  `SHEWCHUK_SIGN_NOT_FINITE`: ledger, "The two return-value contracts").
- Nothing about the magnitude of a result. Outside the floating-point domain
  the integer stage decides more calls than before, and those return -1, 0
  or +1.
- The claim needs R1: the library's translation unit compiled as the project
  compiles it, round-to-nearest, no flush-to-zero. The guard itself does not
  depend on any of that (it compares integers), but the values it guards do.
- The guard is sufficient, not necessary: it sends calls to the slower exact
  path that the first filter would have decided correctly.

## 10. Proved, tested, and what mutation showed

| Statement | Status |
|---|---|
| Lemmas of section 2; Theorems 1, 2, 4 | proved here, from R1 to R3 |
| The control exponents, floors and powers of the guards | derived by `constexpr` code that mirrors the lemmas, tied to the constants by `static_assert` |
| Theorem 3, underflow | proved from R4; R4 by inspection |
| Theorem 3, overflow | the paper's statement, with the bound it rests on |
| The wide tier | [integer-stage.md](integer-stage.md), generic in the number of limbs |

What the tests add (`library.input_domain`, see [testing.md](../testing.md)):

- the exact sign, against the oracle, on a few hundred thousand finite
  inputs from outside the 1996 domain in six families (rescaled corpus
  slices at every exponent up to both ends of the format, per-axis
  rescaling, exact cancellation at extreme exponents, subnormal coordinates,
  the whole exponent range mixed), on all of which the pristine 1996 code
  and the library without guard and integer stage are caught failing;
- every route of section 6 is taken, and on every call the route is one the
  proof allows;
- at the corners of the floating-point domain, no call decided by a
  floating-point stage raises the underflow or overflow exception flag
  (evidence for R4 and for the overflow half of Theorem 3), while the same
  check does fire far outside the domain.

**A sign test cannot validate a threshold.** Mutants of the guard and of the
routing, each built and run against the tests that could notice (the mutants
are not in the tree). "Wrong results" means that `library.input_domain` or
`library.orient2d_first_test` saw the mutant return a wrong sign, or a NaN
or an infinity for a finite input.

| Mutant | Wrong results seen | What else fails |
|---|---|---|
| guard removed from the first filter of `orient3d`, `incircle`, `insphere` (1996 test, finite `det`) | **yes** | |
| the coordinate / lift test dropped from the guard (floor kept, i.e. `D = 1`) | **yes** (spikes) | |
| `orient3d`'s guard reads the x coordinates instead of z | **yes** (spikes) | |
| `det` not required to be finite in the first filter | **yes** (NaN for finite input) | |
| `orient2d`: window removed (any finite error bound) | **yes** | |
| `orient2d`: two zero products trusted without a zero factor | **yes** | route check |
| `orient2d` stage B: estimate not tested for NaN | **yes** (NaN for finite input) | |
| floating-point domain: every input counts as inside | **yes** | route check |
| floating-point domain: largest exponent 210 or 260 instead of 201 | **yes** (not finite) | route check |
| floating-point domain: `d L >= -1200` instead of -914 | **yes** | route check |
| wide tier replaced by tier 3; wide tier with 32 limbs | **yes** | |
| floors of the three guards lowered by 100 (2^-953, 2^-952, 2^-783) | **no** | only a count: fewer than 100 "rechecked" calls per predicate |
| floors lowered to 2^-1021, and to 2^-1022 | **no** | the same count |
| `orient2d` window floor 2^-800, 2^-1000 or 2^-1022 instead of 2^-600 | **no** | the route check of `library.orient2d_first_test`, which restates the window; for the last two also a count |
| `orient2d` window ceiling 2^1024 instead of 2^950 | **no** | the same route check |
| floating-point domain: `d L >= -1074` instead of -914 (no margin for the error-bound constants) | **no** | the route check against the independent statement of the domain in `tests/width.h` |

So the tests have teeth where a mechanism is missing or misdirected: every
part of the guard and of the routing is needed for them to pass. They have
none for the *values* of the thresholds. Guard floors more than 160 binary
orders of magnitude lower than the proved ones, down to the smallest floor
the bit arithmetic allows, still return the exact sign on every case
tried; so do a window floor at 2^-1022, a window without a ceiling, and a
domain without the margin for the error-bound constants. Those mutants fail
only because the tests count how often each route is taken, or compare the
route with their own copy of the constant: that pins the constants, it does
not justify them.

That is expected. The derivation is deliberately pessimistic in one place:
Lemma S charges a factor 2^56 for every addition that follows an unclean
value, as if every such addition had a clean operand just too small to
absorb it. An input that realizes that worst case at several additions at
once is hard to construct, and neither random nor structured testing found
one. The thresholds are therefore justified by the proof alone.

The `static_assert`s make the proof and the constants move together: a floor
below the derived control, a window that is too wide, a wide tier that is
too narrow or a domain that the tests' independent statement does not match
does not compile; the mutants above had those assertions removed.
