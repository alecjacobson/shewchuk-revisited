# Proof: the exact integer stage

Code: [`src/integer_stage.h`](../../src/integer_stage.h). Hook:
`*_after_filter()` and `orient2dadapt()` in
[`src/predicates.cpp`](../../src/predicates.cpp). Ledger:
[`docs/exactness.md`](../exactness.md).

## 1. Claim

For each of `orient2d`, `orient3d`, `incircle`, `insphere`, the function of
that name in `shewchuk::detail::integer_stage` returns, for **any** input
coordinates (any bit patterns),

- `refused`, if a coordinate is an infinity or a NaN, or if the width `W` of
  the coordinates (section 2) exceeds 189; or
- otherwise the exact sign (-1, 0, +1) of the predicate's determinant, with
  the sign convention of the public functions.

It never returns a sign that is not the exact one, it has no undefined
behavior, and it does not depend on the floating-point environment (it
performs no floating-point operation at all; it only copies the bits of the
doubles). In particular it is exact for subnormal coordinates and outside the
exponent domain of the 1996 predicates, whenever it does not refuse.

The claim is about the stage, not about the whole predicate: the stage runs
only on calls the floating-point stages before it do not decide (stage A;
for `orient2d` stages A, B and C), and those are unchanged, with their 1996
domain (section 9).

There is no error bound to get wrong in this stage. What could make it wrong
is: a wrong decomposition of a double, a wrong `W`, an intermediate that does
not fit its fixed number of limbs (silent wrap-around), a wrong multi-limb
operation, or a shift by an out-of-range count. Sections 2 to 7 rule these
out one by one.

Notation: `[c]` is 1 if the condition `c` holds and 0 otherwise. A *limb* is
an unsigned 64-bit integer. `Int<N>` is `N` limbs `w[0..N-1]`, least
significant first, read as a two's-complement integer: its value is
`sum w[i] 2^(64 i) - 2^(64 N) [w[N-1] >= 2^63]`, in `[-2^(64N-1), 2^(64N-1))`.

## 2. Scaling: `scale()`

**Decomposition.** An IEEE 754 binary64 with sign bit `s`, biased exponent
field `b` (11 bits) and fraction field `f` (52 bits) is

| `b` | value |
|---|---|
| 0 | `(-1)^s f 2^-1074` (zero if `f = 0`, else subnormal) |
| 1 ... 2046 | `(-1)^s (2^52 + f) 2^(b - 1075)` |
| 2047 | infinity or NaN |

The code computes `m = f | ([b != 0] << 52)`, which equals `f + 2^52 [b != 0]`
because `f < 2^52`, and `E = b + [b == 0]`. So for `b < 2047` the value is
exactly `(-1)^s m 2^(E - 1075)`, with `0 <= m < 2^53` and `1 <= E <= 2046`.
Both zeros give `m = 0` (negative zero has `s = 1`). All exponents in
`scale()` carry this bias of 1075; only their differences are used.

**Odd part.** For `m != 0`, `zeros = trailing_zeros(m)` is in `[0, 52]`,
`odd = m >> zeros` is odd, and `m = odd 2^zeros`. (For `m = 0`,
`trailing_zeros` is 64; the code shifts by `zeros & 63 = 0` and gets `odd = 0`.
For `m != 0` the mask changes nothing.) The code sets

    lowest  = E + zeros + ((zeros >> 6) << 20)
    highest = E + bit_length(m)

For a nonzero finite coordinate `zeros >> 6 = 0`, so

    |x| = odd * 2^(lowest - 1075),   2^(highest - 1076) <= |x| < 2^(highest - 1075),
    1 <= lowest < highest <= 2046 + 53 = 2099:

`lowest` is the (biased) exponent of the lowest set bit of `x`, `highest` one
more than that of its highest set bit.

**Zeros and non-finite values need no test of their own.**

- A zero has `E = 1`, `zeros = 64`, `bit_length(m) = 0`: `lowest = 65 + 2^20`
  and `highest = 1`. That `lowest` exceeds every nonzero coordinate's and that
  `highest` is below every nonzero coordinate's (which is at least 2), so
  when a nonzero coordinate exists, zeros affect neither the minimum of
  `lowest` nor the maximum of `highest`.
- An infinity or NaN has `b = 2047`, hence `m >= 2^52`, `bit_length(m) = 53`
  and `highest = 2100`, above every finite coordinate's (at most 2099). So
  `max highest >= 2100` exactly if some coordinate is not finite; `scale()`
  then returns false and the predicate returns `refused` before anything
  else is used.

No `int` expression in `scale()` exceeds `2^20 + 2200` in magnitude.

**Width.** Let `low` be the minimum of `lowest` and `high` the maximum of
`highest` over all coordinates; by the above these are the minimum and
maximum over the nonzero coordinates if there is one. The code defines

    W = high - low  if high > low,   else W = 0.

If every coordinate is zero, `high = 1 < low` and `W = 0`. Otherwise
`high > low` (already for a single nonzero coordinate `highest > lowest`),
and for a nonzero coordinate, `shift = lowest - low >= 0`, so the *scaled
coordinate* `X = x / 2^(low - 1075) = (-1)^s odd 2^shift` is an integer, and

    |X| = odd * 2^shift < 2^(highest - low) <= 2^W,   i.e.  |X| <= 2^W - 1,
    shift + bit_length(odd) = highest - low <= W,   hence   0 <= shift <= W - 1.

So **`W` is the bit length of the largest scaled magnitude** (the smallest
`W` with every `|X| < 2^W`): one past the highest set bit of any coordinate
minus the lowest set bit of any coordinate. It is *not* "highest set bit
minus lowest set bit", which is one less; every bound below uses the code's
`W`. `1 <= W <= 2098` unless all coordinates are zero.

A zero coordinate has `X = 0` and `odd = 0`. The code stores
`shift = lowest - low` for it as for any other coordinate; that is a
meaningless nonnegative number (0 if all coordinates are zero, more than
`2^20 - 2100` otherwise). Section 5 shows that it is harmless.

**The sign is unchanged.** Each determinant is a homogeneous polynomial in
the coordinates, of degree `d = 2, 3, 4, 5` for `orient2d`, `orient3d`,
`incircle`, `insphere`. With `x_i = X_i 2^(low - 1075)` for all `i`,
`det(x) = 2^(d (low - 1075)) det(X)`, and that factor is positive. So
`sign det(x) = sign det(X)`: the stage may evaluate the determinant on the
integers `X_i`. `low` is not needed after `scale()` and is not stored.

If all coordinates are zero then `W = 0`, tier 1 runs on integers that are
all zero, and the result is 0, which is the determinant's sign.

## 3. Bit widths of the intermediates

Let `M = 2^W - 1`, so `|X_i| <= M`. The table lists every kind of value the
stage computes, with the expression in `struct Tier`, a bound on its
magnitude, and `k` such that the magnitude is `< 2^k`. Sums are bounded by
the triangle inequality, products by the product of the bounds; a partial sum
of some of the terms of a sum obeys the same bound as the whole sum, so the
order in which the code adds does not matter.

| kind (`Kind` in the code) | expression | magnitude at most | `< 2^k`, `k =` |
|---|---|---|---|
| `COORDINATE` | `X` | `M` | `W` |
| `DIFFERENCE` | `X - X'` | `2M` | `W + 1` |
| `PRODUCT2` | difference × difference | `4M²` | `2W + 2` |
| `MINOR2` | `PRODUCT2 - PRODUCT2` | `8M²` | `2W + 3` |
| `LIFT2` | `PRODUCT2 + PRODUCT2` | `8M²` | `2W + 3` |
| `LIFT3` | sum of three `PRODUCT2` (and of two) | `12M²` | `2W + 4` |
| `PRODUCT3` | difference × `MINOR2` | `16M³` | `3W + 4` |
| `MINOR3` | sum/difference of three `PRODUCT3` (and of two) | `48M³` | `3W + 6` |
| `INCIRCLE_TERM` | `LIFT2` × `MINOR2` | `64M⁴` | `4W + 6` |
| `INCIRCLE_DET` | sum of three `INCIRCLE_TERM` (and of two) | `192M⁴` | `4W + 8` |
| `INSPHERE_TERM` | `LIFT3` × `MINOR3` | `576M⁵` | `5W + 10` |
| `INSPHERE_DET` | sum/difference of four `INSPHERE_TERM` (and of two) | `2304M⁵` | `5W + 12` |

The last column follows from `M < 2^W` and `12 < 2^4`, `48 < 2^6`,
`192 < 2^8`, `576 < 2^10`, `2304 < 2^12` (the other constants are powers of
two). A value of magnitude `< 2^k` is representable in `k + 1` bits of two's
complement.

Tier `D` (`D = 1, 2, 3`) stores each kind in `D` limbs per polynomial degree:
`D` for coordinates and differences, `2D` for the degree-2 kinds, `3D`, `4D`,
`5D` for degrees 3, 4, 5 (`limbs_of` in the code). It accepts
`W <= 64 D - 3`. Taking, for each degree `j`, the kind with the largest `k`:

| degree `j` | largest `k + 1` | at `W = 64D - 3` | available `64 j D` |
|---|---|---|---|
| 1 | `W + 2` | `64D - 1` | `64D` |
| 2 | `2W + 5` | `128D - 1` | `128D` |
| 3 | `3W + 7` | `192D - 2` | `192D` |
| 4 | `4W + 9` | `256D - 3` | `256D` |
| 5 | `5W + 13` | `320D - 2` | `320D` |

and since `k + 1` increases with `W`, every value fits for every
`W <= 64D - 3`. The limit cannot be raised with these bounds: at
`W = 64D - 2` the bound of `LIFT3` needs `128D + 1` bits. (That is a
statement about the proof. The bounds are not attained: the test suite sees
the largest `INSPHERE_DET` 5 bits below its bound.)

Per predicate and tier, bits needed (magnitude plus sign) at the tier's
widest `W` against bits available:

| predicate | intermediate | tier 1, `W = 61` | tier 2, `W = 125` | tier 3, `W = 189` |
|---|---|--:|--:|--:|
| all | scaled coordinates | 62 / 64 | 126 / 128 | 190 / 192 |
| all | differences | 63 / 64 | 127 / 128 | 191 / 192 |
| all | products of two differences | 125 / 128 | 253 / 256 | 381 / 384 |
| all | 2×2 minors; the `orient2d` determinant | 126 / 128 | 254 / 256 | 382 / 384 |
| `incircle` | lifted terms `x² + y²` | 126 / 128 | 254 / 256 | 382 / 384 |
| `insphere` | lifted terms `x² + y² + z²` | 127 / 128 | 255 / 256 | 383 / 384 |
| `orient3d`, `insphere` | difference × 2×2 minor | 188 / 192 | 380 / 384 | 572 / 576 |
| `orient3d`, `insphere` | 3×3 minors; the `orient3d` determinant and its partial sum | 190 / 192 | 382 / 384 | 574 / 576 |
| `incircle` | lift × 2×2 minor | 251 / 256 | 507 / 512 | 763 / 768 |
| `incircle` | the determinant and its partial sum | 253 / 256 | 509 / 512 | 765 / 768 |
| `insphere` | lift × 3×3 minor | 316 / 320 | 636 / 640 | 956 / 960 |
| `insphere` | the determinant and its partial sums | 318 / 320 | 638 / 640 | 958 / 960 |

This table is generated: `tools/integer_stage_widths.py src/integer_stage.h
--table`. The script propagates the largest magnitudes above as exact Python
integers through the expressions, for every `W`, and checks them against
`magnitude_bits()` and `limbs_of()` parsed from the header (test
`integer_stage.widths`). The header also checks itself at compile time:
`static_assert(tier_holds_every_intermediate(D, max_width(D)))` for each tier.

## 4. The multi-limb operations

All limb arithmetic is on `std::uint64_t` (and, in one of the three
implementations of the primitives, on `unsigned __int128`), so no operation
can overflow in the C++ sense: unsigned arithmetic is modular by definition.

**Bit counting.** `bit_length(x)` is the smallest `k` with `x < 2^k` and
`trailing_zeros(x)` the number of trailing zero bits, 64 for `x = 0`. Both
are a compiler builtin or intrinsic guarded by `x != 0`, or portable code:
`bit_length` adds 32, 16, ..., 1 to a count whenever the remaining value has
a set bit at or above that position, and shifts it down; what remains is 0
or 1 and is added. `trailing_zeros(x)` for `x != 0` is
`bit_length(x & -x) - 1`, since `x & -x` (unsigned) is the lowest set bit of
`x` alone.

**Primitives** (three interchangeable implementations, section 8):

- `add_carry(a, b, carry)` returns the low limb of `a + b + carry` and sets
  `carry` to the high part, for `carry` in `{0, 1}`. The sum is at most
  `2^65 - 1`, so the high part is 0 or 1. In the portable form,
  `partial = a + b` wraps iff `partial < a`, and `partial + carry` wraps iff
  the result is `< partial`; both cannot wrap (if the first does,
  `partial <= 2^64 - 2`), so the carry out is their OR.
- `sub_borrow(a, b, borrow)`: the same for `a - b - borrow`. `a - b` wraps
  iff `a < b`, and `partial - borrow` wraps iff `partial < borrow`; both
  cannot wrap (if the first does, `partial >= 1`).
- `multiply_add(a, b, c, d, high)` returns the low limb of `a b + c + d` and
  sets `high` to the high limb. `a b + c + d <= (2^64 - 1)² + 2(2^64 - 1) =
  2^128 - 1`, so it fits two limbs, and in the portable form the two
  increments of the high limb cannot wrap.
- The portable `multiply(a, b)`: with `a = a1 2^32 + a0`, `b = b1 2^32 + b0`,
  the four products `p00, p01, p10, p11` are below `2^64`.
  `middle = (p00 >> 32) + (p01 mod 2^32) + (p10 mod 2^32) < 3 * 2^32`; the low
  limb is `(middle mod 2^32) 2^32 + (p00 mod 2^32)`, and the high limb
  `p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32)` equals
  `floor(a b / 2^64) < 2^64`, so that sum does not wrap.

**`add`, `sub`** propagate the carry or borrow through the `N` limbs and drop
the last one: the result is `a + b` (or `a - b`) modulo `2^(64N)`. If the
true result lies in `[-2^(64N-1), 2^(64N-1))`, the `N`-limb result is that
value. Section 3 shows that it does for every addition and subtraction the
stage performs.

**`mul`** of `Int<N>` by `Int<M>` gives `Int<N + M>`, and is exact for all
operands: `|a| <= 2^(64N-1)` and `|b| <= 2^(64M-1)` give
`|a b| <= 2^(64(N+M)-2)`. It computes the schoolbook product of the bit
patterns `A = a + 2^(64N) [a < 0]` and `B = b + 2^(64M) [b < 0]` (row by row
with `multiply_add`, each row's final carry stored in the next limb, which no
earlier row has written), then subtracts `B` from limbs `N ... N+M-1` if
`a < 0` and `A` from limbs `M ... M+N-1` if `b < 0`, dropping the borrows:

    A B = a b + 2^(64N) [a<0] b + 2^(64M) [b<0] a + 2^(64(N+M)) [a<0][b<0]
    a b = A B - 2^(64N) [a<0] B - 2^(64M) [b<0] A      (mod 2^(64(N+M)))

(using `b = B` modulo `2^(64M)` in the term multiplied by `2^(64N)`, and
likewise for `a`). The `N + M` limbs therefore hold `a b` modulo
`2^(64(N+M))`, which is `a b` since it is in range.

**`sign`** returns -1 if the top bit is set, else 1 if any limb is nonzero,
else 0: the sign of the two's-complement value.

## 5. Conversion: `coordinate()`

Tier `D` runs only if `W <= 64D - 3`. Take a nonzero coordinate with odd part
`m < 2^53` and `shift <= W - 1 <= 64D - 4` (section 2), and let
`bit = shift & 63`, `low_part = m << bit` (modulo `2^64`).

- `D = 1`: `shift <= 60`, so `bit = shift`, and `m 2^shift = |X| < 2^61`: no
  bit is shifted out, the single limb `low_part` is `|X|`.
- `D >= 2`: `word = shift >> 6 <= D - 1`, and
  `high_part = (m >> 1) >> (63 - bit)`, which is `floor(m / 2^(64 - bit))`
  for `bit >= 1` and 0 for `bit = 0`; so `m 2^bit = low_part + 2^64
  high_part`. `low_part` goes to limb `word` and `high_part` to limb
  `word + 1` if that limb exists. If it does not (`word = D - 1`), then
  `high_part = 0`, because otherwise `|X| = m 2^shift >= 2^(64D)`,
  contradicting `|X| < 2^W`. Nothing is dropped: the `D` limbs hold `|X|`
  exactly. The limbs are selected by comparing the loop index with `word`;
  no array is indexed with a computed index.

A zero coordinate has `m = 0`, so `low_part = 0` and `high_part = 0` whatever
its (meaningless, nonnegative) `shift` is: all limbs are zero, which is `X`.
The shift counts `bit` and `63 - bit` are in `[0, 63]` for any nonnegative
`shift`.

The conditional negation `(v XOR mask) - mask` with `mask` all ones is
`-v` modulo `2^(64D)`, and `|X| < 2^(64D-3)` is in range. Negative zero has
`m = 0` and `mask` all ones: `(0 XOR mask) - mask = 0`.

## 6. The determinants

`Tier<D>::orient2d`, `orient3d`, `incircle`, `insphere` evaluate, on the
scaled coordinates, exactly the expressions the first floating-point stage of
each predicate evaluates on the doubles (and `predicates.c` before it):

    orient2d   (ax-cx)(by-cy) - (ay-cy)(bx-cx)
    orient3d   adz (bdx cdy - cdx bdy) + bdz (cdx ady - adx cdy) + cdz (adx bdy - bdx ady)
    incircle   alift (bdx cdy - cdx bdy) + blift (cdx ady - adx cdy) + clift (adx bdy - bdx ady)
    insphere   (dlift abc - clift dab) + (blift cda - alift bcd)

with `adx = ax - dx` etc., `alift = adx² + ady²` (plus `aez²` for
`insphere`), and for `insphere`: `ab = aex bey - bex aey`, `bc`, `cd`, `da`,
`ac`, `bd` likewise, `abc = aez bc - bez ac + cez ab`,
`bcd = bez cd - cez bd + dez bc`, `cda = cez da + dez ac + aez cd`,
`dab = dez ab + aez bd + bez da`. So the sign convention is that of the
public functions by construction; `oracle.unit` checks the oracle's
convention against the reference, and every test of the stage compares with
the oracle.

Every value in these functions is one of the kinds of section 3 (the code
labels each with `is(kind, ...)`), is stored in the limbs section 3 assumes
(the template arguments: a product of `Int<N>` and `Int<M>` is `Int<N+M>`),
and by sections 3 to 5 equals the exact integer it stands for. Hence the
final value is `det(X)` exactly and `sign()` of it is the claimed sign.

## 7. The stage refuses; it never truncates

The entry functions call `scale()`, then try `W <= 61`, `W <= 125`,
`W <= 189` in this order and run the first tier whose limit holds; if none
does they return `refused`. A tier therefore never runs outside the range
section 3 covers. No function narrows a value: the only width-changing
operation is `mul`, which widens. `refused` (2) is distinct from every sign.

In `predicates.cpp` the stage is called at one place per predicate: for
`orient3d`, `incircle` and `insphere` when stage A has not certified the sign
(`*_after_filter()`), for `orient2d` when stage C has not (inside
`orient2dadapt()`; stages B and C of `orient2d` are cheaper than the integer
stage). A sign is returned as it is; a refusal leads to the 1996 code
that would have run at that point, with the arguments it always received.
Non-finite inputs are refused, so the same floating-point operations run on
them as before the stage existed (their result is unspecified, as
documented).

The library built without the integer stage
(`SHEWCHUK_PREDICATES_NO_INTEGER_STAGE`, test only) still returns the 1996
values bit for bit on the whole corpus (`differential.library_expansion_only`).

## 8. Undefined behavior audit

- **Signed overflow.** Limb arithmetic is unsigned throughout. The `int`
  arithmetic is on exponents and bit counts, all of magnitude below `2^21`
  (section 2).
- **Shift counts.** `bits >> 52`, `bits >> 63`, `<< 52`, `>> 32`, `<< 32`,
  `>> 64` of a 128-bit value, `zeros >> 6`, `<< 20` of 0 or 1: constants in
  range. `m >> (zeros & 63)`: in `[0, 63]`. `m << bit`,
  `(m >> 1) >> (63 - bit)` with `bit = shift & 63` and `shift >= 0`: in
  `[0, 63]`. `shift >> 6` on a nonnegative `int`.
- **Array indices.** All loops run over compile-time bounds; `mul` writes
  `r.w[i + j]` and `r.w[i + M]` with `i < N`, `j < M`, into `N + M` limbs.
- **Uninitialized reads.** `mul` zeroes limbs `0 ... M-1` before row 0 reads
  them, and row `i` reads limb `i + M - 1` after row `i - 1` stored its carry
  there. `scale()` writes every field it returns when it returns true.
- **Type punning.** The bits of the doubles are read with `memcpy`.

The three implementations of the primitives (`unsigned __int128`; MSVC's
`_umul128` with portable carries; fully portable 32-bit halves) and of the
bit counting compute the same exact functions (section 4), so the stage's
result does not depend on which one is compiled. `SHEWCHUK_INTEGER_STAGE_PORTABLE`
forces the portable ones; the test suite builds the stage and the library
that way on every platform.

## 9. What is not claimed

- The stage sits behind stage A (for `orient2d`: behind stage C). A call
  that an earlier stage decides never reaches it, and those stages' guarantee
  is the 1996 one, including its exponent domain (ledger, assumption 3).
  Making out-of-domain inputs exact by routing them here is a separate change
  (issue #33).
- Nothing is claimed about speed here; see the pull request and
  `docs/benchmarking.md`.

## 10. Return value

The public functions return `double`. When the integer stage decides, they
return its sign converted to `double`: exactly `-1.0`, `+0.0` or `+1.0`. The
header has always said that only the sign of the result is meaningful; this
is the first stage for which the magnitude is not even an approximation of
the determinant.

## 11. How the tests relate to this proof

Tests cannot replace sections 2 to 7, but each failure mode has a test that
would see it (`docs/testing.md`):

- the checked build of the stage (`SHEWCHUK_INTEGER_STAGE_CHECKED`) verifies,
  on every operation of every test, that no addition or subtraction overflows
  and that every intermediate obeys its bound from section 3, and reports
  how close the largest values came to the bounds;
- inputs built to maximize every intermediate, at `W` exactly at each tier
  limit and one past it, anywhere in the exponent range of a double;
- a negative control: a tier forced onto inputs 5 bits too wide returns wrong
  signs and trips the checks;
- the scaling is compared with an independent decomposition (`frexp`) on
  zeros, subnormals, the extremes of the format and random bit patterns;
- the limb operations are compared with the oracle's big integers, in all
  three implementations of the primitives where the platform has them
  (the portable one everywhere).
