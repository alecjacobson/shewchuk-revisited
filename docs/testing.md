# Testing

The exactness suite. Its rule: every sign an implementation returns equals the
sign computed by an exact oracle, on a corpus that provably reaches every stage
of every predicate.

## Run it

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 10
ctest --test-dir build --output-on-failure -j 4
```

The whole quick tier takes under a minute in Release. No test process uses
more than a few MB of memory: the corpus is generated as a stream and never
stored.

Options:

| CMake option | Effect |
|---|---|
| `-DSHEWCHUK_DEEP_TESTS=ON` | also registers the deep tier (`deep.*` tests, up to 20x the cases, a few minutes) |
| `-DSHEWCHUK_REQUIRE_GMP=ON` | configuration fails if GMP is missing, instead of skipping the GMP cross-check |
| `-DSHEWCHUK_BUILD_COMPARATORS=ON` | downloads and builds third-party exact predicates and registers the `comparator.*` tests ([comparators.md](comparators.md)) |
| `-DSHEWCHUK_SANITIZE=address,undefined` | builds the library and the tests (not the pristine reference) with these sanitizers; `thread` for ThreadSanitizer |

GMP (`libgmp-dev`, `brew install gmp`) and Python 3 are optional; they enable
two extra cross-checks of the oracle.

## What the tests are

| Test | What it proves |
|---|---|
| `reference.sha256` | `reference/predicates.c` is the pinned 1996 file (also checked at configure time) |
| `reference.symbol_prefix` | every external symbol of the reference library has the `shewchuk_ref_` prefix |
| `oracle.unit` | the big-integer arithmetic and the oracle's signs on hand-computed cases; sign convention equals the reference's; two determinant formulations agree on the corpus |
| `oracle.gmp_crosscheck` | the oracle agrees with an independent GMP rational oracle on the corpus (skipped without GMP) |
| `oracle.python_crosscheck` | the oracle agrees with Python `fractions.Fraction` Gaussian elimination on a sample |
| `stages.coverage` | every stage (A0, A, B, Bx, C, D) of every predicate decides at least 2% of its cases; prints the stage table |
| `differential.reference` | the reference has zero sign mismatches against the oracle, and its value hash equals the recorded one |
| `differential.reference_staged` | the instrumented build returns bit-identical values |
| `negative_control.fp_contract` | the same file built with `-O2 -ffp-contract=fast` IS caught returning wrong signs (skipped where the compiler has no such mode). On x86-64 it is built with `-mfma -mno-avx2`: when clang may use AVX2 its contracted code returns different values but no wrong sign on the corpus, which only the value hash sees |
| `negative_control.default_flags` | the same file built with `-O2` and no floating-point flag at all IS caught returning wrong signs. GCC only (its GNU dialects imply `-ffp-contract=fast`); skipped for other compilers, whose defaults have not been seen to change a sign. On x86-64 the file is built with `-mfma`, as `-march=x86-64-v3` or `-march=native` would, and the test is skipped on a CPU without FMA |

Tests of the library (`src/predicates.cpp`):

| Test | What it proves |
|---|---|
| `differential.library_expansion_only` | the library built without its integer stage and its input-domain guard is a refactoring of the 1996 code: zero sign mismatches, every value equals the reference's bit for bit, the value hash equals the recorded reference hash |
| `differential.library` | the shipped library: zero sign mismatches against the oracle, and every value is the one the integer-stage contract fixes (below); the value hash equals the recorded library hash |
| `differential.library_hooked` | the same for the library's test-only build |
| `differential.library_portable` | the same, with the same hash, for the library built with the integer stage's portable 64x64->128 multiplication (the one compilers without a 128-bit integer use) |
| `library.stages` | every stage of the library decides enough cases: at least 2% for the stages before the integer stage and for the integer stage, at least 0.5% (and 200 cases) for the stages behind it, which only inputs too wide for the integer stage reach. Every single case is decided by the stage it must be decided in: the reference's stage, or the integer stage if the reference's stage comes after it and the case's width is at most 317. For orient2d the library has one first test where the reference has two (A0 and A); a call that test decides is counted as A0 if its two products have opposite signs or one is zero, the class the reference decides in A0, and as A otherwise. **The input-domain guard is never in the way of the corpus**, which is inside the range of the 1996 paper: every case the reference decides in its first test is decided by the library's first test with its guard holding (exceptions, with the reference's value: `orient2d` with two zero products, which leaves by a branch of its own, and `insphere` with every coordinate below 2^-100, or with coordinates that span more than 253 bits, whose first filter is confirmed by the domain check; these must stay below 0.1% of the cases), every case is inside the floating-point domain, and the wide tier decides none |
| `integer_stage.unit`, `.portable`, `.checked`, `.checked_portable` | the integer stage on its own (`tests/test_integer_stage.cpp`), in four builds: default and portable limb arithmetic, each with and without run-time verification of the proof's bounds. See below |
| `integer_stage.widths` | the bit widths of `docs/proofs/integer-stage.md`, re-derived with exact integers by `tools/integer_stage_widths.py`, agree with the formulas in `src/integer_stage.h` and fit the limbs of every tier, and for every width a finite input can have the number of limbs the wide tier works with holds every intermediate |
| `library.orient2d_first_test` | the library's first test of orient2d on 331776 combinations of special products the corpus does not contain (zeros of both signs, equal and opposite products, subnormal, huge, overflowed to infinity): the exact sign on every one; inside the window of the test's guard, and for two zero products, the reference's value in the reference's stage; outside the window the first test decides nothing by itself. The 1996 code is wrong on thousands of these |
| `library.input_domain` | **the exact sign for every finite input.** See below |
| `library.expansions` | `fast_expansion_sum_zeroelim`, `scale_expansion_zeroelim` and `estimate` return the same components as the reference's routines on random and adversarial expansions, up to the largest lengths any predicate uses |
| `library.constants` | the compile-time constants equal the globals `exactinit()` computes, their formulas and their documented bit patterns |
| `library.api`, `library.api_c` | the public headers from C++ and C, including the opt-in 1996 names; both forms of every predicate (the double with the exact sign, and the sign as -1, 0, +1), and `SHEWCHUK_SIGN_NOT_FINITE` for a NaN or infinite coordinate |
| `library.threads` | several threads calling the predicates get the single-threaded values |
| `library.symbols` | the library exports the four `shewchuk_` predicates, each in its two forms, and nothing else (8 symbols) |
| `library.lto_fast_math_caller` | a caller compiled with `-O3 -ffast-math` (MSVC: `/fp:fast`) and link-time optimization, linked with those flags against a link-time-optimized build of the library that has the same flags in front of the project's strict ones, gets the recorded values bit for bit on the whole corpus (skipped if the toolchain has no link-time optimization, or on an x86-64 CPU without FMA: the test compiles with `-mfma`) |
| `negative_control.lto_fast_math_caller` | the same test IS failed by a library copy built with `-ffp-contract=fast` instead of the strict flags. The criterion is the value hash: with clang this copy returns different values but no wrong sign on the corpus, with GCC it returns wrong signs |
| `library.no_fma` | the library's object code contains no fused multiply-add and no call to `fma()` (skipped if no disassembler is found or the architecture is not arm64 / x86-64) |
| `negative_control.no_fma` | that check IS triggered by the contracted reference build |
| `library.refuses_x87` | compiled for x87 extended-precision arithmetic (the build's flags plus `-mfpmath=387`), `src/predicates.cpp` stops at its `#error` on `FLT_EVAL_METHOD` (skipped where the compiler has no such mode: not x86, or clang on x86-64) |

Tests of third-party code ([comparators.md](comparators.md)), registered with
`-DSHEWCHUK_BUILD_COMPARATORS=ON` (`comparator.gmp_rational` whenever GMP is
found):

| Test | What it proves |
|---|---|
| `comparator.geogram`, `.cgal`, `.indirect`, `.gmp_rational` | the comparator, through its wrapper, has zero wrong signs against the oracle on the quick corpus (`gmp_rational`: on a tenth of it), which also shows that the wrapper adapts the argument order and the sign convention correctly. What a comparator declares about itself is honoured: Geogram's symbolically perturbed zeros are counted separately, its orientation-free in-circle test is compared with the exact sign times the exact orientation, and its in-sphere test is checked on the inputs that satisfy its precondition |
| `comparator.geometric_predicates` | GeometricPredicates is recorded as returning wrong signs (its `orient3d`). The test prints them by kind and by generator family and fails if there are none any more, or if there is one on uniform random input, where a wrong sign would mean a wrong wrapper |

A skipped test is reported as skipped, never as passed.

## The whole finite range

`test_input_domain` compares the library's sign with the oracle's on inputs
from everywhere outside the range of the 1996 paper, in six families:

| family | inputs |
|---|---|
| upper sweep | a slice of the corpus (25 cases per generator, every stage) rescaled by a power of two so that its largest coordinate has binary exponent T, for every T from 201 to 1023 |
| lower sweep | the same with the lowest set bit of any coordinate at 2^T, for every T from -150 to -1074; and every case of the slice at the four exponents just inside the lower edge of the floating-point domain |
| axes | `orient2d`, `orient3d`: each axis rescaled by its own power of two (the sign is unchanged), so that products underflow next to products that do not and next to factors that amplify them |
| spikes | small integers times powers of two from a list of exponents around the thresholds where products of 2 to 5 coordinates overflow or underflow, zeros of both signs: exact cancellation everywhere |
| subnormal | every coordinate a subnormal number or zero |
| full range | every coordinate with its own exponent from the whole format, repeated coordinates |

plus the insphere call for which the 1996 code was reported to return the
opposite nonzero sign. In the quick tier one case of the slice in 48 is used
per exponent (rotating); `deep.library.input_domain` uses six times as many
and larger random families.

What it requires:

- **the library**: zero wrong signs and zero non-finite results;
- **negative control**: the pristine 1996 code and the library without guard
  and integer stage, on the same inputs, are wrong (wrong sign, NaN or
  infinity) more than 20 times in every family and for every predicate; in
  the lower sweep with finite wrong signs, in the upper sweep with NaN or
  infinity; and they are exact as long as the slice stays within the limits
  recorded in docs/exactness.md;
- **routes**: the test-only build reports the stage and the path of every
  call (`shewchuk_hooked_last_stage`, `shewchuk_hooked_last_path`,
  `shewchuk_hooked_last_integer_tier`). On every case the route must be one
  the proof allows: a floating-point stage behind the first filter only
  inside the floating-point domain (`orient2d`: stage D; its stages B and C
  also under the window), the integer tier that the width and the domain
  select (computed independently in `tests/width.h`), a recheck only inside
  the domain. Over the run every route must be taken at least 100 times per
  predicate: the first test with its guard holding, the recheck, the adaptive
  stages, a fast integer tier inside and outside the domain, the wide tier;
  and the guard must have stopped the first test at least 300 times where
  the 1996 code returned from its own;
- **the test-only build** returns the shipped library's value on every case;
- **no exception flags**: at the corners of the floating-point domain, a call
  that a floating-point stage decides does not raise the underflow or the
  overflow flag; far outside, the unguarded stages do (so the check can
  fail). This is measured on the test-only build. The shipped library built
  by clang can raise a spurious overflow or underflow flag in insphere's
  first filter inside the domain (its returned values are unaffected and
  bit-identical to GCC's; seen in an independent review, also before the
  guard existed, and attributed, unconfirmed, to an unused vector lane), so
  callers must not rely on the flags;
- **non-finite input**: NaN for a NaN or an infinity at every coordinate
  position, alone and in combinations, at the case's own scale and at both
  ends of the format; and `SHEWCHUK_SIGN_NOT_FINITE` from the sign-returning
  form;
- **the sign-returning form** returns the oracle's sign on every finite case
  of every family.

What these tests cannot do is validate the guard's thresholds; see the
mutation table in [proofs/input-domain.md](proofs/input-domain.md).

## The integer stage and the value contract

The library is no longer bit-identical to the 1996 code: its exact integer
stage (`src/integer_stage.h`) knows only the sign and returns -1.0, 0.0 or
+1.0. What the suite requires instead, with `differential --integer-stage`:

- zero sign mismatches against the oracle, as for every implementation;
- per case, exactly this value: the reference's, bit for bit, if the
  reference decides the case in a stage that comes before the integer stage
  (A0, A; for `orient2d` also B, Bx, C) or if the case's width exceeds 317
  (it falls through to the 1996 stages); otherwise the oracle's sign as
  -1.0, +0.0 or +1.0. The deciding stage comes from the stage-instrumented
  reference and the width from `tests/width.h`, which shares no code with the
  library;
- the hash of all these values equals the one recorded in `tests/corpus.h`.

The recorded reference hash still binds the reference itself and the library
built without the integer stage (`differential.library_expansion_only`).

`test_integer_stage` calls the stage directly (the header is compiled into
the test):

- limb arithmetic against the oracle's big integers, for the fixed sizes of
  the fast tiers and for the wide tier's run-time sizes (every number of
  limbs from 1 to 66 for sums, every product size of every number of limbs
  per difference from 1 to 33);
- the scaling of doubles to integers against an independent decomposition:
  zeros, negative zero, subnormals, the largest doubles, random bit patterns
  from the whole format, infinities and NaNs;
- force-stage: on every corpus case the stage, as the sole decider, returns
  the oracle's sign or declines; it declines exactly the cases wider than
  317 (the corpus is inside the floating-point domain), uses the tier the
  width selects, and every fast tier and the refusal see at least 2% of each
  predicate's cases. Under the request `always_exact` it decides every case
  (the declined ones in the wide tier; one in 16 of them is run), and under
  `prefer_floating_point` it declines exactly the cases inside the
  floating-point domain;
- worst-case magnitudes: every coordinate at the largest value of the width,
  with every sign pattern, at widths exactly at each fast tier's limit and
  one past it (and around the limb boundaries), anywhere in the exponent
  range of a double; for the wide tier at widths from 318 to 2098, the
  largest a finite input can have; and the wide tier run directly with every
  number of limbs per difference from 1 to 33, at the narrowest and the
  widest width of that number;
- a negative control: a tier forced onto inputs 5 bits too wide returns wrong
  signs;
- non-finite coordinates are reported as such under every request, and the
  library returns NaN;
- in the checked builds (`SHEWCHUK_INTEGER_STAGE_CHECKED`): no addition or
  subtraction overflowed, every intermediate stayed within the bit width the
  proof states for it, the worst-case inputs came within 5 bits of every
  bound (for the wide tier: of the bound at the largest width there is), and
  the negative control tripped the checks.

The `wide_span` generator families exist for the fall-through path: their
coordinates come from two exponent bands at least 269 binades apart, so the
integer stage declines them and stages B, Bx, C and D of the library stay
covered. The `span_tier_4` and `span_tier_5` families are the same
near-degenerate configurations with the bands closer together (140 and 206
binades), for the two widest fast tiers of the integer stage.

## Layout

- `reference/predicates.c`: the pristine file. Never edited.
- `baseline/`: builds it unmodified, with locked floating-point flags
  (`cmake/StrictFloatingPoint.cmake`) and renamed symbols. Call
  `shewchuk_ref_exactinit()` once before any predicate.
- `tools/instrument_stages.cpp`: generates, at build time, the test-only copy
  that records which stage decided each call.
- `tests/oracle.*`, `tests/bigint.h`: the dependency-free exact oracle.
- `tests/generators.*`: the stage-targeted, deterministic input generators.
- `tests/differential.cpp`: the driver. `differential --list` shows the
  implementations and tiers; `differential --impl NAME` runs one.
- `comparators/`: wrappers around third-party predicates and the CMake code
  that downloads them (off by default). `tests/judge.h` holds the rule by
  which a returned value is judged, including what a comparator declares
  about its own conventions.
- `src/test_hooks.h`: the library's test-only build. The same source and
  flags as the shipped library, compiled a second time with
  `SHEWCHUK_PREDICATES_TEST_HOOKS`: different symbol names, a record of the
  stage that decided each call, of the path the call took past the
  input-domain guard and of the integer tier, and exported expansion
  routines. None of it exists in the shipped library.
- Two more test-only builds of the same source under other symbol names
  (`tests/CMakeLists.txt`): `library_expansion_only`
  (`SHEWCHUK_PREDICATES_NO_INTEGER_STAGE`) and `library_portable`
  (`SHEWCHUK_INTEGER_STAGE_PORTABLE`).
- `tests/width.h`: the width of a case, the position of the integer stage,
  the floating-point domain and the integer tier a case must be decided in,
  all independent of the library.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSHEWCHUK_SANITIZE=address,undefined
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSHEWCHUK_SANITIZE=thread
```

The option instruments the library and the tests. The pristine reference is
left alone on purpose: it reads one element past the end of an array in
`fast_expansion_sum_zeroelim` (the value is unused), AddressSanitizer reports
that, and the file is never edited. The library does not have that read, and
the sanitizer build of the whole suite is how that is checked. Do not put
`-fsanitize` into `CMAKE_C_FLAGS`. CTest sets the sanitizer run-time options
itself (a small AddressSanitizer quarantine keeps each test process around
100 MB).

## Adding an implementation

Add one entry to the table in `tests/implementations.cpp` and one
`differential.<name>` test in `tests/CMakeLists.txt`. Use `--require-identical`
if the change is declared refactor-only (values must equal the reference's bit
for bit), and `--integer-stage` if it must return the library's values.

## The recorded hashes

`tests/corpus.h` records, per tier, a hash of the generated inputs, a hash of
the values the reference returns, and a hash of the values the library
returns. With contraction off they are the same for every compiler,
optimization level and platform tested. If `differential.reference` reports a
different hash on a new platform, that is a finding to report, not a number
to update. Update the hashes only when the generators change on purpose (the
library's also when a change to the library is meant to change returned
values, which needs its own justification).
