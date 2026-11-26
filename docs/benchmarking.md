# Benchmarking

How speed is measured in this project, and the rule a change must pass to be
called faster. Exactness comes first and is covered in [testing.md](testing.md).

## Run it

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 10 --target bench_predicates

build/bench/bench_predicates --out result.json   # one pass, about 6 s, prints a table
build/bench/bench_predicates --list              # implementations, predicates, workloads
tools/bench_suite.py --build-dir build --runs 10 # repeat in fresh processes, report the noise
tools/compare_revs.py main my-branch --target 'orient2d/*'   # A/B two revisions
```

Useful options of `bench_predicates`: `--impl NAME`, `--pred LIST`,
`--workload LIST`, `--scale F` (fewer or more calls), `--latency`,
`--wall-clock`, `--manifest`. No process of the benchmark uses more than about
40 MB of memory: inputs are generated chunk by chunk into one reused buffer.

## What is measured

Per predicate call, for one implementation on one workload:

- **CPU cycles** (the metric every decision uses) and **retired instructions**;
- **nanoseconds**, for orientation only.

Cycles are read without privileges from `proc_pid_rusage(RUSAGE_INFO_V6)` on
macOS and from `perf_event_open` on Linux. Where no counter is available
(Windows, most virtual machines including hosted CI runners, Linux with a
restrictive `perf_event_paranoid`) the harness says so, measures wall-clock
nanoseconds only, and labels every output `wall-clock`. Wall-clock numbers are
never evidence for or against a change. The Linux counter path has so far only
been exercised on machines that refuse it; treat its first real numbers with
care.

## The protocol, and why

Each rule exists because breaking it gave a wrong number on the development
machine (a fanless Apple M4 laptop). Rules 1 and 2 come from exploratory
measurements made before this harness existed; the others were measured with
it.

1. **Cycles, not time.** The clock frequency follows temperature and load: the
   same code ran at very different nanoseconds per call (a factor of two was
   seen) while its cycle count stayed put.
2. **No input is ever repeated.** The branch predictor memorizes benchmark
   inputs: a loop over a small set of inputs reports `orient2d` several times
   cheaper than it is on inputs the processor has not seen. So a process makes
   a single pass over a stream of fresh inputs, and repetitions are new
   processes. (The warm-up runs on a separate stream.)
3. **A separate classification pass.** The stage that decides each input is
   found with the stage-instrumented reference before the timed pass, and the
   timed pass regenerates the same inputs without calling it. Classifying
   right before timing slowed the timed calls by 0.5% to 1.5%.
4. **Chunks, and only undisturbed ones.** A pass is split into 64 chunks that
   are measured separately. A chunk is kept only if it ran on one performance
   core from start to end (at least 99.9% of its cycles on performance cores,
   same CPU number before and after) and the same workload ran undisturbed on
   that CPU just before it for at least 2048 calls or 2 million cycles. An
   efficiency core needs 1.5 times the cycles for the same straight-line code,
   and after a move to another core the expansion stages read 3% to 10% high
   until its branch predictor has settled.
5. **Warm, on other inputs.** A warm-up stream, separate from the timed one,
   feeds 2048 calls before the pass and a short slice (a quarter of a chunk,
   at most 512 calls) right before every chunk. Producing a chunk takes longer
   than timing it, and on a busy machine other work uses the core meanwhile;
   without the slice the run-to-run variation of the rarest stages was 3.6%
   instead of under 1%.
6. **Whole-pass totals.** The result is total cycles over total calls of the
   kept chunks, not a trimmed statistic of the chunks: inputs differ in cost,
   and trimming would remove real work. A result with fewer than a quarter of
   its chunks kept is flagged invalid (exit status 3), not reported as good.
7. **A library call.** The timed loop is in its own translation unit and
   calls the implementation through a function pointer from the table in
   `tests/implementations.cpp`, so every call is a real call into separately
   compiled code. The loop's own cost (argument loads, call, return, sign
   accumulation) is about 3 cycles per call and is included in every number;
   `--impl overhead` measures it. Do not build the harness with link-time
   optimization.
8. **The checksum.** Every run sums the signs it gets and compares the sum
   with the reference's on the same inputs. A mismatch is an error (exit
   status 1): a benchmark of a wrong implementation is not a result.
9. **Everything is recorded.** The JSON (laid out like Google Benchmark's:
   `context` and `benchmarks`, plus `workloads`) carries the machine,
   compiler, flags, git revision, counter backend, load average, the workload
   manifest and the raw per-chunk counts.

`--latency` makes the address of each input depend on the previous result, so
calls cannot overlap; the default measures throughput.

## Workloads

Uniform random input never leaves the first filter, and the later stages cost
up to several hundred times more, so a single "average" number says little. Workloads are
therefore tagged by the stage that decides each input in the reference
(A0: sign test of `orient2d`; A, B, C: the three filters; Bx: the exit taken
when all difference tails are zero; D: the exact stage). The inputs come from
the generator families of the test corpus (`tests/generators.cpp`), looked up
by name.

The tags name the reference's stages. The library decides most inputs tagged
B, Bx, C or D (for `orient2d`: those tagged D) in its exact integer stage
instead, in the tier their width selects (`docs/proofs/integer-stage.md`):
the `near_degenerate`, `degenerate_*`, `stage_B`, `stage_Bx` and `stage_C`
inputs are at most 64 bits wide (tier 1, a few in tier 2) and the `stage_D`
inputs 88 to 126 bits (tier 2, a few in tier 3). Of the `wide_exponents`
inputs that the first filter leaves open, all of `insphere`'s fit tier 3,
15% of `incircle`'s do, and almost none of `orient2d`'s and `orient3d`'s:
those are too wide and fall through to the 1996 stages.

| Workload | Definition |
|---|---|
| `uniform` | coordinates uniform in [-1, 1) |
| `wide_exponents` | every coordinate has its own binary exponent |
| `near_degenerate` | an exactly degenerate real configuration (collinear, coplanar, cocircular, cospherical) rounded to doubles |
| `degenerate_lattice` | exactly degenerate, coordinate differences exact (zero tails) |
| `degenerate_53bit` | exactly degenerate, differences not representable (nonzero tails) |
| `stage_A0`, `stage_A` | `orient2d` only: the `uniform` cases decided by that stage (for the other predicates `uniform` is entirely stage A) |
| `stage_B`, `stage_Bx`, `stage_C` | the `near_degenerate` cases decided by that stage |
| `stage_D` | the cases of the `mixed_magnitudes` family (large and tiny coordinates, a tiny displacement away from degenerate) decided by the exact stage |
| `mix_easy` | 98% `uniform`, 2% `near_degenerate`, drawn independently per call |
| `mix_grid` | 93% `uniform`, 7% `degenerate_lattice` |
| `mix_surface` | 40% `uniform`, 60% `near_degenerate` |

The measured stage distribution of every workload (the manifest) is printed by
`bench_predicates --manifest` and stored in every result file under
`workloads`. It depends only on the generators and is the same for every
compiler tried. Percent of calls decided by each stage:

| predicate | workload | calls | candidates per call | A0 | A | B | Bx | C | D |
| --- | --- | --: | --: | --: | --: | --: | --: | --: | --: |
| orient2d | `uniform` | 2097152 | 1.00 | 44.5 | 55.5 | 0.0 | 0.0 | 0.0 | 0.0 |
| orient2d | `wide_exponents` | 2097152 | 1.00 | 44.4 | 52.2 | 0.1 | 0.0 | 2.6 | 0.8 |
| orient2d | `near_degenerate` | 524288 | 1.00 | 0.0 | 23.3 | 7.4 | 52.8 | 16.5 | 0.0 |
| orient2d | `degenerate_lattice` | 524288 | 1.00 | 11.9 | 0.0 | 0.0 | 88.1 | 0.0 | 0.0 |
| orient2d | `degenerate_53bit` | 524288 | 1.00 | 11.0 | 0.0 | 0.0 | 8.9 | 0.0 | 80.2 |
| orient2d | `stage_A0` | 2097152 | 2.25 | 100.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| orient2d | `stage_A` | 2097152 | 1.80 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| orient2d | `stage_B` | 524288 | 13.63 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 |
| orient2d | `stage_Bx` | 524288 | 1.89 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 |
| orient2d | `stage_C` | 524288 | 6.05 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 |
| orient2d | `stage_D` | 524288 | 5.15 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 |
| orient2d | `mix_easy` | 1048576 | 1.00 | 43.6 | 54.9 | 0.1 | 1.1 | 0.3 | 0.0 |
| orient2d | `mix_grid` | 1048576 | 1.00 | 42.1 | 51.7 | 0.0 | 6.2 | 0.0 | 0.0 |
| orient2d | `mix_surface` | 524288 | 1.00 | 17.8 | 36.2 | 4.4 | 31.6 | 10.0 | 0.0 |
| orient3d | `uniform` | 2097152 | 1.00 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| orient3d | `wide_exponents` | 1048576 | 1.00 | 0.0 | 94.8 | 0.2 | 0.0 | 4.2 | 0.8 |
| orient3d | `near_degenerate` | 65536 | 1.00 | 0.0 | 0.5 | 1.5 | 28.8 | 69.2 | 0.0 |
| orient3d | `degenerate_lattice` | 65536 | 1.00 | 0.0 | 0.0 | 0.9 | 99.1 | 0.0 | 0.0 |
| orient3d | `degenerate_53bit` | 32768 | 1.00 | 0.0 | 0.0 | 11.1 | 0.4 | 0.0 | 88.5 |
| orient3d | `stage_B` | 32768 | 65.83 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 |
| orient3d | `stage_Bx` | 65536 | 3.48 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 |
| orient3d | `stage_C` | 65536 | 1.44 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 |
| orient3d | `stage_D` | 32768 | 4.62 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 |
| orient3d | `mix_easy` | 1048576 | 1.00 | 0.0 | 98.0 | 0.0 | 0.6 | 1.4 | 0.0 |
| orient3d | `mix_grid` | 1048576 | 1.00 | 0.0 | 93.0 | 0.1 | 6.9 | 0.0 | 0.0 |
| orient3d | `mix_surface` | 131072 | 1.00 | 0.0 | 40.4 | 0.9 | 17.3 | 41.4 | 0.0 |
| incircle | `uniform` | 2097152 | 1.00 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| incircle | `wide_exponents` | 262144 | 1.00 | 0.0 | 84.6 | 0.5 | 0.0 | 10.9 | 4.1 |
| incircle | `near_degenerate` | 32768 | 1.00 | 0.0 | 8.8 | 8.5 | 33.3 | 49.3 | 0.0 |
| incircle | `degenerate_lattice` | 65536 | 1.00 | 0.0 | 0.0 | 23.2 | 76.8 | 0.0 | 0.0 |
| incircle | `degenerate_53bit` | 16384 | 1.00 | 0.0 | 0.0 | 33.2 | 17.9 | 0.0 | 48.9 |
| incircle | `stage_B` | 32768 | 11.87 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 |
| incircle | `stage_Bx` | 65536 | 2.99 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 |
| incircle | `stage_C` | 32768 | 2.04 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 |
| incircle | `stage_D` | 16384 | 1.02 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 |
| incircle | `mix_easy` | 524288 | 1.00 | 0.0 | 98.2 | 0.2 | 0.7 | 1.0 | 0.0 |
| incircle | `mix_grid` | 524288 | 1.00 | 0.0 | 93.0 | 1.6 | 5.4 | 0.0 | 0.0 |
| incircle | `mix_surface` | 32768 | 1.00 | 0.0 | 46.0 | 5.0 | 19.8 | 29.2 | 0.0 |
| insphere | `uniform` | 2097152 | 1.00 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 | 0.0 |
| insphere | `wide_exponents` | 131072 | 1.00 | 0.0 | 93.5 | 0.5 | 0.0 | 5.6 | 0.4 |
| insphere | `near_degenerate` | 8192 | 1.00 | 0.0 | 0.4 | 2.0 | 30.7 | 66.8 | 0.0 |
| insphere | `degenerate_lattice` | 32768 | 1.00 | 0.0 | 0.0 | 19.4 | 80.6 | 0.0 | 0.0 |
| insphere | `degenerate_53bit` | 8192 | 1.00 | 0.0 | 0.0 | 8.4 | 3.4 | 0.0 | 88.3 |
| insphere | `stage_B` | 8192 | 55.76 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 | 0.0 |
| insphere | `stage_Bx` | 32768 | 3.12 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 | 0.0 |
| insphere | `stage_C` | 8192 | 1.52 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 | 0.0 |
| insphere | `stage_D` | 8192 | 1.43 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 100.0 |
| insphere | `mix_easy` | 262144 | 1.00 | 0.0 | 98.0 | 0.0 | 0.6 | 1.4 | 0.0 |
| insphere | `mix_grid` | 262144 | 1.00 | 0.0 | 93.0 | 1.3 | 5.7 | 0.0 | 0.0 |
| insphere | `mix_surface` | 16384 | 1.00 | 0.0 | 40.1 | 1.1 | 19.8 | 39.0 | 0.0 |

"Candidates per call" is how many inputs had to be drawn for each one kept by
the stage filter.

## Comparing two revisions

```sh
tools/compare_revs.py REV_A REV_B --cc CC --cxx CXX --target 'orient2d/*'
```

exports both revisions, builds each in Release with the same compiler, runs
them alternately in fresh processes (A B B A A B ...), and prints a Markdown
table for the pull request with, per workload, the ratio B/A of cycles per
call and a bootstrap 95% confidence interval. Because both sides generate
identical inputs, the ratio of a round is taken over the chunks that were kept
in both runs; the estimate is the median over rounds and the interval is the
percentile bootstrap of that median. If the two builds do not produce the same
inputs (the generators or the harness changed between the revisions), the tool
refuses to compare.

Two implementations in one build are compared with
`--build-dir-a build --build-dir-b build --impl-a reference --impl-b NAME`.

## The acceptance rule

Per workload, with r the ratio B/A and [lo, hi] its 95% interval:

| Verdict | Condition |
|---|---|
| faster | r <= 0.98 and hi < 0.99 |
| slower | hi >= 1.01 and lo > 1.00 |
| inconclusive | hi >= 1.01 and lo <= 1.00 (more rounds needed) |
| no difference | otherwise |

A pull request may claim a speedup when

1. the exactness suite passes and every benchmark checksum matches;
2. every workload it declares as its target (`--target`) is **faster**;
3. no other workload is slower or inconclusive, that is, none has an upper
   bound at or above 1.01. A slowdown it wants to accept anyway must be named
   and argued in the pull request;
4. the direction is confirmed with a second compiler (run the tool again with
   another `--cc`/`--cxx`).

Effects below these thresholds are neutral, however small their interval: on
branchy code a change of code layout alone moves the result by up to about
1.5%. Instruction counts are reported but are never the criterion.

The tool's own check: comparing a revision with itself must report no
difference on every workload.

## The baseline

`bench/results/` holds the canonical measurement of the pristine reference
(built by `baseline/` with the locked floating-point flags), one file per
compiler. Regenerate with:

```sh
# Apple clang
cmake -S . -B build-bench-appleclang -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build-bench-appleclang -j 10 --target bench_predicates
tools/bench_suite.py --build-dir build-bench-appleclang --runs 10 --overhead \
  --out bench/results/baseline-apple-m4-appleclang-21.json

# Homebrew LLVM
cmake -S . -B build-bench-llvm22 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build-bench-llvm22 -j 10 --target bench_predicates
tools/bench_suite.py --build-dir build-bench-llvm22 --runs 10 --overhead \
  --out bench/results/baseline-apple-m4-llvm-22.json

# GCC
cmake -S . -B build-bench-gcc16 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/bin/gcc-16 -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++-16
cmake --build build-bench-gcc16 -j 10 --target bench_predicates
tools/bench_suite.py --build-dir build-bench-gcc16 --runs 10 --overhead \
  --out bench/results/baseline-apple-m4-gcc-16.json

# All three side by side, as a Markdown table
tools/bench_table.py bench/results/baseline-apple-m4-*.json
```

The library is measured the same way with `--impl library` into
`bench/results/library-apple-m4-<compiler>.json` (same build directories, same
commands with `--impl library` added). The performance table of the README is
generated from the two sets of files by `tools/readme_table.py`; the test
`bench.readme_table` fails if the README and the files disagree. Cycles per
call include the harness's loop (about 2 cycles, see `--impl overhead`), so
ratios on the cheapest workloads understate the difference.

Each number is the median of 10 fresh processes; `cycles_cv_percent` in the
files is the run-to-run variation of the same binary, the harness's noise
floor, and the `context` of each file records the load average during the
run. `bench_suite.py` refuses to write a file if, for some benchmark, fewer
than 6 of the 10 runs were valid. Regenerate the files on a quiet machine
whenever the harness, the generators, the compilers or the machine change;
every earlier number is then retired.

The committed files were taken at a load average of about 3 (the machine was
shared). Their run-to-run variation of cycles per call is 0.2% to 0.3% in the
median over the 50 benchmarks and at most 1.1% to 1.4%, depending on the
compiler. On a busier machine expect more discarded chunks and a wider
spread; `bench_suite.py` prints both.

## Adding an implementation

Add one entry to the table in `tests/implementations.cpp`. The same entry
makes it available to the exactness drivers and to `bench_predicates --impl`.

## In CI

CI builds the harness and runs `bench.smoke`, `bench.smoke_wall_clock`,
`bench.suite_smoke` and `bench.compare_smoke` at a tiny scale. They show that
the code runs on every platform, including the wall-clock fallback. Nothing in
CI judges performance.
