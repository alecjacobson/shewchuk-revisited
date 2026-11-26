# Comparators

Other exact implementations of the four predicates, plugged into the same
harness as the 1996 reference and this library: the differential test checks
their signs against the exact oracle, and `bench_predicates --impl NAME`
measures them on the same workloads. The speed table is in
[benchmarking.md](benchmarking.md#comparators).

Nothing of the comparators is in this repository. They are downloaded when
the build is configured with the option below, at the pinned versions of the
table, and each archive is verified by its SHA-256
(`comparators/CMakeLists.txt`). The licenses in the table are those of the
downloaded code and apply to whoever builds and runs it.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSHEWCHUK_BUILD_COMPARATORS=ON
cmake --build build -j 10
ctest --test-dir build -R '^comparator[.]' --output-on-failure
build/bench/bench_predicates --impl indirect
```

The option is off by default: the library, the default build and its tests
need no network and no third-party code. With it on, the configuration
downloads about 6 MB (13 MB on arm64) and prints one line per comparator: its
version and flags, or why it was skipped. CGAL needs the Boost headers, GMP
and MPFR on the machine (`brew install boost gmp mpfr`,
`apt install libboost-dev libgmp-dev libmpfr-dev`); without them it is
skipped. A comparator can be left out with `-DSHEWCHUK_COMPARATOR_<NAME>=OFF`.
`gmp_rational` is not downloaded code: it is built whenever GMP is found.

## What is compared

| `--impl` | Upstream | Pinned version | License | Entry points |
|---|---|---|---|---|
| `indirect` | [Indirect_Predicates](https://github.com/MarcoAttene/Indirect_Predicates) with its numeric kernel [NFG](https://github.com/MarcoAttene/nfg); on arm64 also [SIMDe](https://github.com/simd-everywhere/simde), which NFG includes there | commits `f8f243d18a6851fe3128b7e09af5b3574259d700` and `aeec97547a67dc6904ab84b39b15d20c6d62f65a`; SIMDe 0.8.2 (`71fd833d9666141edcd1d3c109a80e228303d8d7`) | LGPL 3.0 or later; SIMDe: MIT | `orient2d`, `orient3d`, `IPs::incircle`, `IPs::inSphere` on double coordinates |
| `geogram` | Geogram's PCK predicates, standalone distribution [geogram.psm.Predicates](https://github.com/BrunoLevy/geogram.psm.Predicates) | commit `8468ac11314dd4356a31dbf8b5f59ae29980e7e6` | BSD 3-Clause | `GEO::PCK::orient_2d`, `orient_3d`, `in_circle_2d_SOS`, `in_sphere_3d_SOS` |
| `cgal` | [CGAL](https://github.com/CGAL/cgal), `Exact_predicates_inexact_constructions_kernel` | release 6.2.1 (`CGAL-6.2.1-library.tar.xz`) | LGPL 3.0 or later (the kernel packages) | `CGAL::orientation` (2D, 3D), `side_of_oriented_circle`, `side_of_oriented_sphere` |
| `geometric_predicates` | [GeometricPredicates](https://github.com/wlenthe/GeometricPredicates), a C++ template port of the 1996 code | commit `ebc50200a90684e045419ce1bb634524eb63704c` | BSD 3-Clause | `predicates::adaptive::orient2d`, `orient3d`, `incircle`, `insphere` |
| `gmp_rational` | the suite's second oracle (`tests/oracle_gmp.cpp`): the determinant evaluated with GMP rationals | the GMP installed on the machine (6.3.0 for the committed results) | this repository's code; GMP: LGPL 3.0 or later / GPL 2.0 or later | - |

How each one works, as far as it matters for reading the numbers:

- **Indirect_Predicates**: a semi-static floating-point filter; then
  expansion arithmetic (for `inSphere`: none); then a big-float number type
  that allocates.
- **Geogram PCK**: a generated floating-point filter that scales its error
  bound by the largest input difference, found with comparisons and
  branches; then expansion arithmetic on the full formula.
- **CGAL EPICK**: a static filter, then interval arithmetic, then exact
  rational arithmetic on GMP numbers.
- **GeometricPredicates**: the stages of the 1996 code as C++ templates over
  fixed-size expansion types. Where the compiler says that `std::fma` is fast
  (arm64, and x86-64 when FMA is enabled) the product's rounding error comes
  from one fused multiply-add instead of Dekker's splitting.
- **GMP rationals**: no filter. A reference point for "exact, and no effort
  spent on speed".

## Flags

Each comparator is one translation unit, `comparators/<name>.cpp`, compiled
with the build type's optimization flags (`-O3 -DNDEBUG` in Release, as
everything else) followed by the floating-point flags its upstream
prescribes. The harness prints them (`bench_predicates --list`) and records
them in every result file (`implementation_description`).

| `--impl` | Floating-point flags | Where they come from |
|---|---|---|
| `indirect` | `-frounding-math -ffp-contract=off` (MSVC: `/fp:strict`); on x86-64 also `-mavx2 -mfma` | the `README.md` and `CMakeLists.txt` of the pinned commit. AVX2 with FMA is upstream's default for x86-64; the harness reports the comparator as unavailable on a CPU without them |
| `geogram` | `-ffp-contract=off` (MSVC: `/fp:strict`), `GEO_STATIC_LIBS` | the `CMakeLists.txt` of the pinned commit |
| `cgal` | GCC: `-frounding-math`; clang: none; MSVC: `/fp:strict /fp:except-` | `cmake/modules/CGAL_SetupCGALDependencies.cmake` of the release; linked with GMP and MPFR, as CGAL's own configuration does. `NDEBUG` turns CGAL's assertions off |
| `geometric_predicates` | `-ffp-contract=off` (MSVC: `/fp:strict`) | upstream names no flags. It uses the error-free transformations of the 1996 code, which a contracted multiply-add breaks, so it gets the flag this project gives the reference |

## Sign conventions and contracts

The wrappers adapt the argument order and the sign to those of the 1996
functions and do nothing else. That the adaptation is right is not argued
but tested: the `comparator.*` tests run every wrapper against the exact
oracle on the whole quick corpus (760,000 cases).

- CGAL's 3D orientation and oriented-sphere test, Geogram's `orient_3d` and
  Indirect_Predicates' `orient3d` and `inSphere` have the opposite sign of
  the 1996 functions. The wrappers negate.
- Geogram has no in-circle or in-sphere test that reports a degenerate input:
  `in_circle_2d_SOS` and `in_sphere_3d_SOS` break ties with a symbolic
  perturbation and never return zero.
- Geogram's `in_circle_2d_SOS` answers "is the last point inside the circle
  through the other three" whatever the orientation of the three: the 1996
  sign times the sign of `orient2d` of the triangle.
- Geogram's `in_sphere_3d_SOS` requires a tetrahedron that is positive for
  its own `orient_3d`. On other tetrahedra its filter and its exact stage use
  different conventions and its answers mean nothing. The cost of a call does
  not depend on the orientation.

The last three are not hidden behind extra work in the wrapper, which would
make Geogram look slower than it is. The table of implementations declares
them instead (`perturbs_zero`, `unoriented`, `required_base_orientation` in
`tests/implementations.h`), and the drivers act on the declaration:
`tests/judge.h` compares an unoriented answer with the exact sign times the
exact orientation, skips inputs outside a declared precondition, and counts
a nonzero answer for an exact zero separately where a perturbation is
declared; `bench_predicates` does not compare the sign checksum for those
predicates (`"checksum_checked": false`).

## Exactness

Wrong signs against the exact oracle on the quick corpus, 760,000 cases, all
inside the input domain the 1996 paper states (every coordinate zero or with
a binary exponent in [-142, 201]). The counts are the same with Apple clang
21 and GCC 16 on arm64. Reproduce with
`build/tests/differential --impl NAME --tier quick --comparator`, which also
prints the first wrong cases as hexadecimal floats.

| `--impl` | `orient2d` (195,000) | `orient3d` (185,000) | `incircle` (195,000) | `insphere` (185,000) |
|---|--:|--:|--:|--:|
| `indirect` | 0 | 0 | 0 | 0 |
| `cgal` | 0 | 0 | 0 | 0 |
| `geogram` | 0 | 0 | 0 of the 168,295 comparable | 0 of the 82,580 inside its precondition |
| `geometric_predicates` | 0 | **17,506 wrong** | 0 | 0 |
| `gmp_rational` | 0 | 0 | 0 | 0 |

**Geogram** is exact on this corpus within its own contracts (previous
section). For `incircle`, 26,705 cases have a collinear triangle, for which
an orientation-free in-circle test has no defined answer; of the others,
49,709 are exactly cocircular and get a nonzero sign from the symbolic
perturbation. For `insphere`, 102,420 cases have a tetrahedron that is flat
or of the orientation `in_sphere_3d_SOS` does not accept; of the others,
25,197 are exactly cospherical and get a perturbed sign. A caller that needs
to know that four points are cocircular cannot get that from Geogram.

**GeometricPredicates returns wrong signs from `orient3d`**, on inputs that
are inside the 1996 domain: 16,339 times a nonzero sign for exactly coplanar
points and 1,167 times the opposite of a nonzero sign. Its other three
predicates agree with the oracle everywhere. The wrong results are confined
to these generator families of `orient3d` (none in the uniform,
near-degenerate or lattice families), with both compilers:

| generator family | wrong | of |
|---|--:|--:|
| `shared_coordinates` | 595 | 10,000 |
| `degenerate_wide` | 4,506 | 10,000 |
| `degenerate_wide_ulps` | 74 | 10,000 |
| `degenerate_wide_one_coordinate` | 1,898 | 10,000 |
| `degenerate_53bit` | 4,656 | 10,000 |
| `mixed_magnitudes` | 886 | 10,000 |
| `threshold_mixed_magnitudes` | 13 | 1,250 |
| `threshold_degenerate_wide` | 328 | 1,250 |
| `wide_span_degenerate` | 2,821 | 10,000 |
| `wide_span_degenerate_ulps` | 144 | 10,000 |
| `wide_span_degenerate_one_coordinate` | 1,300 | 10,000 |
| `wide_span_box_ulps` | 84 | 10,000 |
| `threshold_wide_span` | 201 | 1,250 |

(Counted before the exponent bands of the `wide_span` families were moved
further apart and the `span_tier_4` and `span_tier_5` families were added;
the rows of those families have not been counted again.)

One reproducer (coordinates of a, b, c, d as hexadecimal floats; the points
are exactly coplanar and `predicates::adaptive::orient3d` returns a negative
value):

```
orient3d 0x1p+1 0x1p+1 0x1.f7c883ec0b782p+27  0x1p+1 -0x1.48550a13c39cap-1 0x1.f7c883ec0b782p+27  0x1p+1 0x1p+1 0x1.f7c883ec0b782p+27  -0x1.48550a13c39cap-1 0x1.f7c883ec0b782p+27 0x1p+1
```

The benchmark marks every workload on which this shows in the sign checksum
(†). The cause was not investigated, and no upstream report has been filed
from this project.

### Beyond the corpus: the exponent range

`build/tests/test_input_domain --sweep NAME [STEP]` rescales a slice of the
corpus (about 460 cases per predicate) by powers of two, so that the largest
coordinate has binary exponent T, or the lowest set bit of any coordinate is
2^T, and compares with the oracle. The first T at which a wrong result
appears, going up from 150 and down from -150:

| | `orient2d` | `orient3d` | `incircle` | `insphere` |
|---|---|---|---|---|
| this library | none / none | none / none | none / none | none / none |
| the 1996 code (`reference`) | 511 / -538 | 340 / -359 | 255 / -269 | 204 / -215 |
| `indirect` | 511 / -538 | 341 / -359 | none up to 1023 / -270 | 222 / -215 |
| `geogram` (every second T) | 512 / -538 | 342 / -360 | **170 / -180** | 204 / -216 |
| `cgal` (every fifth T) | none / none | none / none | none / none | none / none |
| `geometric_predicates` | 512 / -538 | wrong everywhere (above) | 255 / -269 | 204 / -215 |

The 1996 domain corresponds to T <= 201 and T >= -194. Two things stand out:

- **This library and CGAL had no wrong result anywhere in the sweep**, up to
  the largest and down to the smallest doubles. For this library that is the
  contract, proved in [proofs/input-domain.md](proofs/input-domain.md) and
  tested far beyond this sweep (`library.input_domain`): the exact sign for
  every finite input. Before it had its input-domain guard it had the limits
  of the 1996 code, whose floating-point stages it uses. For CGAL it is an
  observation on this sample.
- **Geogram's `in_circle_2d_SOS` returns wrong signs inside the 1996
  domain**, for coordinates of magnitude 2^170 and above (exact at 2^168)
  and for coordinates whose lowest set bit is 2^-180 or below (exact at
  2^-178), silently: the results are finite. The limits are those of a
  formula of degree 6 in the coordinates (1024 / 6 and 1074 / 6), where the
  1996 formulation has degree 4. Whether Geogram documents an input domain
  was not checked: this is an observed difference in domain, not a claim
  that Geogram breaks a promise. One reproducer
  (a, b, c, d; d is inside the circle through the counterclockwise triangle
  a, b, c and Geogram answers "outside"):

  ```
  incircle 0x1.e9f3db6dd845cp+170 0x1.6dff5e5e1e554p+170  0x1.e9ac65e39eb1p+167 0x1.7c9c388772f98p+170  -0x1.bbcae50862e2p+169 0x1.f8171bdc2adfp+169  -0x1.8eabe31701058p+168 -0x1.c03c7c2e15d1ap+170
  ```

Outside its limits the 1996 code returns NaN or an infinity when an
intermediate overflows (never a finite wrong sign on this sample) and a
silently wrong sign when one underflows; Indirect_Predicates, Geogram and
GeometricPredicates return silently wrong signs in both directions.

These are observations on a sample, not statements about what any of these
libraries guarantees.

## Speed

The table for the main workloads is in
[benchmarking.md](benchmarking.md#comparators); the result files in
`bench/results/` hold every workload. In short, on an Apple M4:

- On mixtures of random and near-degenerate inputs (`mix_easy`,
  `mix_surface`) and on the hardest cases (`stage_D`) this library is the
  fastest of the exact implementations for every predicate, except
  `orient2d` on `mix_easy` with Apple clang, where Indirect_Predicates is 6%
  ahead. On `stage_D` the margin over the best comparator is 1.3x to 1.9x for
  `orient2d` and 6x to 10x for the other three predicates.
- **On uniform random input it is not the fastest everywhere.** With Apple
  clang, Indirect_Predicates' first filter is 19% faster for `orient2d` and
  6% faster for `insphere`, and GeometricPredicates is 2% faster for
  `incircle`. With GCC the library is level with Indirect_Predicates for
  `orient2d` and `orient3d` and ahead elsewhere.
- **On inputs whose coordinates have very different exponents
  (`wide_exponents`) it loses clearly for three predicates**: these inputs
  are too wide for the integer stage, so the library falls back to the 1996
  stages.

Every benchmark, of all workloads, on which a comparator beats the library by
more than 2% (`tools/comparator_table.py --losses`, medians of separate
sessions), with the paired same-session ratio where one was measured
(`tools/compare_revs.py --impl-a library --impl-b NAME`, 10 alternating
rounds; a ratio below 1 means the comparator is faster):

| compiler | predicate | workload | comparator | its cycles | this library | library / comparator | paired comparator / library (95% CI) |
|---|---|---|---|--:|--:|--:|---|
| Apple clang 21 | `orient2d` | `uniform` | Indirect_Predicates | 4.1 | 5.0 | 1.23x | 0.814 [0.814, 0.815], faster |
| Apple clang 21 | `orient2d` | `wide_exponents` | Indirect_Predicates | 6.4 | 10.4 | 1.62x | 0.616 [0.615, 0.618], faster |
| Apple clang 21 | `orient2d` | `degenerate_lattice` | Indirect_Predicates | 21.3 | 27.7 | 1.30x | 0.770 [0.768, 0.770], faster |
| Apple clang 21 | `orient2d` | `stage_A0` | Indirect_Predicates | 4.1 | 5.0 | 1.23x | 0.813 [0.812, 0.816], faster |
| Apple clang 21 | `orient2d` | `stage_A` | Indirect_Predicates | 4.1 | 5.0 | 1.23x | 0.812 [0.812, 0.814], faster |
| Apple clang 21 | `orient2d` | `stage_B` | Indirect_Predicates | 20.7 | 21.8 | 1.05x | 0.951 [0.950, 0.952], faster |
| Apple clang 21 | `orient2d` | `stage_Bx` | Indirect_Predicates | 21.4 | 25.6 | 1.20x | 0.837 [0.836, 0.837], faster |
| Apple clang 21 | `orient2d` | `mix_easy` | Indirect_Predicates | 5.8 | 6.2 | 1.06x | 0.946 [0.944, 0.950], faster |
| Apple clang 21 | `orient3d` | `wide_exponents` | Indirect_Predicates | 77.2 | 108 | 1.40x | 0.719 [0.713, 0.722], faster |
| Apple clang 21 | `insphere` | `uniform` | Indirect_Predicates | 28.7 | 30.7 | 1.07x | 0.937 [0.935, 0.938], faster |
| Apple clang 21 | `incircle` | `wide_exponents` | CGAL EPICK | 333 | 1,625 | 4.87x | 0.206 [0.205, 0.207], faster |
| Apple clang 21 | `orient2d` | `degenerate_lattice` | GeometricPredicates | 22.9 | 27.7 | 1.21x | 0.827 [0.825, 0.829], faster |
| Apple clang 21 | `orient2d` | `degenerate_53bit` | GeometricPredicates | 55.3 | 57.1 | 1.03x | 0.960 [0.956, 0.967], faster |
| Apple clang 21 | `orient2d` | `stage_B` | GeometricPredicates | 17.2 | 21.8 | 1.27x | 0.788 [0.787, 0.788], faster |
| Apple clang 21 | `incircle` | `uniform` | GeometricPredicates | 11.5 | 11.8 | 1.02x | 0.975 [0.974, 0.977], faster |
| Apple clang 21 | `incircle` | `wide_exponents` | GeometricPredicates | 722 | 1,625 | 2.25x | 0.448 [0.447, 0.449], faster |
| GCC 16 | `orient2d` | `wide_exponents` | Indirect_Predicates | 6.9 | 10.5 | 1.53x | 0.655 [0.654, 0.655], faster |
| GCC 16 | `orient2d` | `degenerate_lattice` | Indirect_Predicates | 21.2 | 29.8 | 1.41x | 0.710 [0.710, 0.711], faster |
| GCC 16 | `orient2d` | `stage_B` | Indirect_Predicates | 20.7 | 22.1 | 1.07x | 0.936 [0.936, 0.937], faster |
| GCC 16 | `orient2d` | `stage_Bx` | Indirect_Predicates | 21.3 | 27.8 | 1.31x | 0.766 [0.766, 0.767], faster |
| GCC 16 | `orient2d` | `stage_C` | Indirect_Predicates | 25.2 | 33.1 | 1.31x | 0.763 [0.763, 0.764], faster |
| GCC 16 | `orient3d` | `wide_exponents` | Indirect_Predicates | 88.4 | 108 | 1.23x | 0.818 [0.813, 0.823], faster |
| GCC 16 | `incircle` | `wide_exponents` | CGAL EPICK | 290 | 1,621 | 5.59x | 0.179 [0.179, 0.180], faster |
| GCC 16 | `orient2d` | `degenerate_lattice` | GeometricPredicates | 23.2 | 29.8 | 1.28x | 0.780 [0.779, 0.780], faster |
| GCC 16 | `orient2d` | `degenerate_53bit` | GeometricPredicates | 52.6 | 62.3 | 1.18x | 0.847 [0.834, 0.849], faster |
| GCC 16 | `orient2d` | `stage_B` | GeometricPredicates | 15.3 | 22.1 | 1.44x | 0.691 [0.691, 0.693], faster |
| GCC 16 | `orient3d` | `wide_exponents` | GeometricPredicates† | 102 | 108 | 1.06x | not measured |
| GCC 16 | `orient3d` | `degenerate_lattice` | GeometricPredicates | 92.9 | 101 | 1.09x | not measured |
| GCC 16 | `incircle` | `wide_exponents` | GeometricPredicates | 652 | 1,621 | 2.48x | 0.402 [0.401, 0.403], faster |

What the comparators do differently there, as far as their code was read
(none of this was confirmed by profiling):

- `orient2d`, first filter (Indirect_Predicates): one branch-free
  expression, `(det >= eb) - (-det >= eb)` with `eb` a constant times
  `|l| + |r|`, and the result is the sign, not the determinant.
- `orient2d`, `orient3d` on `wide_exponents` (Indirect_Predicates): after
  the filter it goes straight to one exact expansion evaluation. The library
  walks through the 1996 stages B, C and D for inputs its integer stage
  declines.
- `incircle` on `wide_exponents` (CGAL): behind the static filter comes
  interval arithmetic, whose bound follows the actual magnitudes of every
  intermediate, where the 1996 filter has one bound, a constant times the
  permanent; and behind that, rational arithmetic. GeometricPredicates runs
  the stages of the 1996 code there at less than half the cost of the 1996
  code and of the library, with `fma`-based products and fixed-size
  expansions.
- `orient2d` on exactly degenerate lattice inputs and on the inputs of
  stages B and Bx (Indirect_Predicates, GeometricPredicates): their
  expansion evaluation behind the filter costs 15 to 23 cycles, less than
  the library's path through its own stages (22 to 30).

The slow sides of the comparators, for balance: Geogram's filters cost 40 to
100 cycles on random input (their error bound needs the largest input
difference, found with data-dependent branches, which never-repeated inputs
make unpredictable); CGAL's static filter costs 28 to 96 cycles; and
everything behind the first filter costs each comparator hundreds to tens of
thousands of cycles where the library's integer stage takes 80 to 720.

## Fairness

- Same compiler, same `-O3`, same machine, same never-repeated inputs, same
  call scenario for everyone: a function pointer into separately compiled
  code, no link-time optimization.
- That scenario is the one this library and Geogram are made for. CGAL,
  Indirect_Predicates and GeometricPredicates are header-only and are
  normally inlined into the caller. Here they are inlined into their wrapper
  and the wrapper is called, so they pay the call that a compiled library
  pays: about 2 to 3 cycles per call, the harness's own `--impl overhead`
  figure, which is included in every number of every column. On the
  cheapest workloads (5 to 15 cycles) that is a large share. Inlined into a
  loop over independent queries they would also overlap more with the
  caller's work. This was not measured.
- CGAL's wrapper builds `Point_2` / `Point_3` objects from the coordinates on
  every call. A program that stores CGAL points does not.
- Geogram's two "simulation of simplicity" predicates do more than the
  others on an exactly degenerate input: they also resolve the tie.
- The comparators were built with the flags above and nothing was tuned
  further, for or against. In particular Indirect_Predicates on arm64 runs
  its SSE/AVX intrinsics through SIMDe, as its upstream sets it up.
- GMP rationals are measured at one twentieth of the calls
  (`--scale 0.05`): a full pass takes seven minutes.
- One machine, one architecture (Apple M4). x86-64, where
  Indirect_Predicates uses AVX2 and FMA directly and GeometricPredicates has
  no fast `fma` by default, is not measured.

## Not included

- `libigl/predicates` and the `predicates.c` copies in TetGen, Triangle and
  others: the same 1996 file, which the reference column already is.
- CDT's `predicates.h`: derived from GeometricPredicates, 2D only.
- Boost.Geometry's side strategies, dengwirda/robust-predicate and others
  from the list of the tracking issue: not evaluated.
- `long double` or `__float128` evaluation: not exact, so not a comparator.

## Adding a comparator

1. `comparators/<name>.cpp`: the four functions of `comparators/comparators.h`
   around the third-party entry points. Return the sign as -1.0, 0.0 or +1.0.
2. `comparators/CMakeLists.txt`: download it at a pinned version with its
   SHA-256 and call `shewchuk_add_comparator` with upstream's flags.
3. One entry in `tests/implementations.cpp` with `comparator = true`, and a
   `comparator.<name>` test in `tests/CMakeLists.txt`.
4. Run the test. A sign convention adapted the wrong way shows up as wrong
   signs on the uniform random families.
