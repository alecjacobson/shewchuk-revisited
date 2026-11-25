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

The whole quick tier takes a few seconds in Release (under 20 s in Debug). No
test process uses more than a few MB of memory: the corpus is generated as a
stream and never stored.

Options:

| CMake option | Effect |
|---|---|
| `-DSHEWCHUK_DEEP_TESTS=ON` | also registers the deep tier (`deep.*` tests, 20x the cases, about a minute) |
| `-DSHEWCHUK_REQUIRE_GMP=ON` | configuration fails if GMP is missing, instead of skipping the GMP cross-check |
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
| `negative_control.fp_contract` | the same file built with `-O2 -ffp-contract=fast` IS caught returning wrong signs (skipped where the compiler has no such mode) |

Tests of the library (`src/predicates.cpp`):

| Test | What it proves |
|---|---|
| `differential.library` | zero sign mismatches against the oracle; every value equals the reference's bit for bit; the value hash equals the recorded reference hash |
| `differential.library_hooked` | the same for the library's test-only build |
| `library.stages` | every stage of the library decides at least 2% of its cases, and every single case is decided by the same stage as in the reference |
| `library.expansions` | `fast_expansion_sum_zeroelim`, `scale_expansion_zeroelim` and `estimate` return the same components as the reference's routines on random and adversarial expansions, up to the largest lengths any predicate uses |
| `library.constants` | the compile-time constants equal the globals `exactinit()` computes, their formulas and their documented bit patterns |
| `library.api`, `library.api_c` | the public headers from C++ and C, including the opt-in 1996 names |
| `library.threads` | several threads calling the predicates get the single-threaded values |
| `library.symbols` | the library exports the four `shewchuk_` predicates and nothing else |
| `library.no_fma` | the library's object code contains no fused multiply-add and no call to `fma()` (skipped if no disassembler is found or the architecture is not arm64 / x86-64) |
| `negative_control.no_fma` | that check IS triggered by the contracted reference build |

A skipped test is reported as skipped, never as passed.

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
- `src/test_hooks.h`: the library's test-only build. The same source and
  flags as the shipped library, compiled a second time with
  `SHEWCHUK_PREDICATES_TEST_HOOKS`: different symbol names, a record of the
  stage that decided each call, and exported expansion routines. None of it
  exists in the shipped library.

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
for bit).

## The recorded hashes

`tests/corpus.h` records, per tier, a hash of the generated inputs and a hash
of the values the reference returns. With contraction off they are the same
for every compiler, optimization level and platform tested. If
`differential.reference` reports a different hash on a new platform, that is a
finding to report, not a number to update. Update the hashes only when the
generators change on purpose.
