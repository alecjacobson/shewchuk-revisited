#!/usr/bin/env python3
"""A/B comparison of two revisions (or two implementations) of the predicates.

    tools/compare_revs.py REV_A REV_B [--cc CC --cxx CXX] [--target PATTERN ...]

Builds both git revisions in Release with the same compiler, runs the
benchmark of each in fresh processes interleaved A B B A A B ..., and prints,
per workload, the ratio B/A of CPU cycles per call with a bootstrap 95%
confidence interval, as a Markdown table for the pull request, followed by
the verdict of the acceptance rule below.

Other ways to choose what is compared:
    --impl-a NAME --impl-b NAME      two implementations (default: reference)
    --build-dir-a DIR --build-dir-b DIR   two existing build directories
    --binary-a PATH --binary-b PATH       two existing bench_predicates

THE ACCEPTANCE RULE (docs/benchmarking.md)

Per workload, with r the median over rounds of B/A and [lo, hi] its 95%
confidence interval:

    faster         r <= 0.98 and hi < 0.99
    slower         hi >= 1.01 and lo > 1.00
    inconclusive   hi >= 1.01 and lo <= 1.00   (interval too wide: more rounds)
    no difference  otherwise  (small effects are treated as neutral: code
                   layout alone moves branchy code by up to about 1.5%)
    invalid        fewer than 6 rounds, or fewer than half of them, were
                   usable (the scheduler disturbed the others)

A change is FASTER if every workload named with --target is `faster` and no
other workload is `slower` or `inconclusive`, i.e. no other workload has an
upper bound at or above 1.01. Confirm the direction with a second compiler
(run again with another --cc/--cxx) before claiming it.

Exit status: 0 the comparison ran (whatever the verdict), or 1 with
--fail-on-regression if a workload is slower; 2 error.
"""
import argparse
import fnmatch
import io
import json
import os
import random
import subprocess
import sys
import tarfile

import benchlib

# The rule's constants.
FASTER_RATIO = 0.98        # a win needs the ratio at or below this ...
FASTER_UPPER = 0.99        # ... with the interval's upper bound below this
REGRESSION_UPPER = 1.01    # no other workload may have an upper bound at or above this
CONFIDENCE = 0.95
BOOTSTRAP_RESAMPLES = 4000
# A round counts for a workload only if at least this share of the chunks was
# kept (undisturbed, on a performance core) in both A and B.
MIN_COMMON_CHUNKS = 0.25
# No interval, and no verdict, from fewer valid rounds than this.
MIN_VALID_ROUNDS = 6


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rev_a", nargs="?", help="git revision A (the base)")
    parser.add_argument("rev_b", nargs="?", help="git revision B (the change)")
    parser.add_argument("--cc", help="C compiler for both builds")
    parser.add_argument("--cxx", help="C++ compiler for both builds")
    parser.add_argument("--cmake-arg", action="append", default=[],
                        help="extra argument for both CMake configurations (repeatable)")
    parser.add_argument("--work-dir", help="where revisions are exported and built "
                        "(default: build-compare in the repository)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="parallel build jobs")
    parser.add_argument("--build-dir-a"), parser.add_argument("--build-dir-b")
    parser.add_argument("--binary-a"), parser.add_argument("--binary-b")
    parser.add_argument("--impl-a", default="reference"), parser.add_argument("--impl-b", default="reference")
    parser.add_argument("--rounds", type=int, default=12, help="A/B rounds (default 12)")
    parser.add_argument("--pred", help="comma-separated predicates (default: all)")
    parser.add_argument("--workload", help="comma-separated workloads (default: all)")
    parser.add_argument("--scale", type=float, default=1.0, help="multiply every workload's calls")
    parser.add_argument("--seed", type=int, default=0, help="seed of the input streams")
    parser.add_argument("--target", action="append", default=[], metavar="PATTERN",
                        help="workload the change is meant to speed up, as predicate/workload; "
                        "shell wildcards allowed, e.g. 'orient2d/*' (repeatable)")
    parser.add_argument("--json", help="write every measurement and the verdicts here")
    parser.add_argument("--fail-on-regression", action="store_true")
    return parser.parse_args()


def run(command, **kwargs):
    process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    if process.returncode != 0:
        output = process.stdout.decode(errors="replace") if isinstance(process.stdout, bytes) else process.stdout
        raise benchlib.BenchError(f"command failed: {' '.join(command)}\n{output[-4000:]}")
    return process.stdout


def build_revision(options, repository, revision, side):
    """Export `revision`, build it in Release and return (sha, bench binary)."""
    sha = run(["git", "-C", repository, "rev-parse", "--verify", revision + "^{commit}"]).decode().strip()
    work = os.path.abspath(options.work_dir or os.path.join(repository, "build-compare"))
    source = os.path.join(work, "src-" + sha[:12])
    build = os.path.join(work, f"build-{side}-{sha[:12]}")
    if not os.path.isdir(source):
        # An export, not a worktree: nothing is registered in the repository.
        archive = run(["git", "-C", repository, "archive", "--format=tar", sha])
        os.makedirs(source + ".partial", exist_ok=True)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            try:
                tar.extractall(source + ".partial", filter="data")
            except TypeError:  # Python older than 3.12
                tar.extractall(source + ".partial")
        os.rename(source + ".partial", source)
    if not os.path.isfile(os.path.join(source, "bench", "CMakeLists.txt")):
        raise benchlib.BenchError(
            f"revision {revision} ({sha[:12]}) has no bench/ directory: it predates the benchmark "
            "harness and cannot be built by this tool")
    configure = ["cmake", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
                 "-DSHEWCHUK_BENCH_GIT_REVISION=" + sha]
    if options.cc:
        configure.append("-DCMAKE_C_COMPILER=" + options.cc)
    if options.cxx:
        configure.append("-DCMAKE_CXX_COMPILER=" + options.cxx)
    print(f"building {side}: {revision} ({sha[:12]}) in {build}", file=sys.stderr, flush=True)
    run(configure + options.cmake_arg)
    run(["cmake", "--build", build, "--config", "Release", "--target", "bench_predicates",
         "-j", str(options.jobs)])
    return sha, benchlib.find_binary(build)


def locate_sides(options):
    """Return [(label, binary), (label, binary)] for A and B."""
    explicit = [options.binary_a, options.binary_b, options.build_dir_a, options.build_dir_b]
    if any(explicit):
        if options.rev_a or options.rev_b:
            raise benchlib.BenchError("give revisions or --binary/--build-dir options, not both")
        sides = []
        for binary, build_dir, side in ((options.binary_a, options.build_dir_a, "a"),
                                        (options.binary_b, options.build_dir_b, "b")):
            if bool(binary) == bool(build_dir):
                raise benchlib.BenchError(f"give exactly one of --binary-{side} and --build-dir-{side}")
            path = binary or benchlib.find_binary(build_dir)
            sides.append((path, path))
        return sides
    repository = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if not options.rev_a:
        raise benchlib.BenchError("nothing to compare: give two revisions (see --help)")
    revisions = [options.rev_a, options.rev_b or options.rev_a]
    sides = []
    for revision, side in zip(revisions, "AB"):
        sha, binary = build_revision(options, repository, revision, side)
        sides.append((f"{revision} ({sha[:12]})", binary))
    return sides


def round_ratio(a, b, metric):
    """(B/A, A per call, B per call) over the chunks valid in both runs, or None."""
    if a["input_hash"] != b["input_hash"] or a["chunk_calls"] != b["chunk_calls"]:
        raise benchlib.BenchError(
            f"{a['predicate']}/{a['workload']}: A and B did not measure the same inputs "
            "(the harness or the generators differ between the two builds)")
    key = "chunk_cycles" if metric == "cycles" else "chunk_ns"
    common = [i for i, (ok_a, ok_b) in enumerate(zip(a["chunk_kept"],
                                                     b["chunk_kept"])) if ok_a and ok_b]
    if len(common) < MIN_COMMON_CHUNKS * len(a[key]) or not common:
        return None
    total_a = sum(a[key][i] for i in common)
    total_b = sum(b[key][i] for i in common)
    if total_a == 0 or total_b == 0:
        return None
    calls = len(common) * a["chunk_calls"]
    return total_b / total_a, total_a / calls, total_b / calls


def bootstrap_interval(ratios, generator):
    """Percentile bootstrap interval of the median of `ratios`."""
    count = len(ratios)
    medians = sorted(
        benchlib.median([ratios[generator.randrange(count)] for _ in range(count)])
        for _ in range(BOOTSTRAP_RESAMPLES))
    tail = (1.0 - CONFIDENCE) / 2.0
    return medians[int(tail * BOOTSTRAP_RESAMPLES)], medians[int((1.0 - tail) * BOOTSTRAP_RESAMPLES) - 1]


def classify(ratio, low, high):
    if ratio <= FASTER_RATIO and high < FASTER_UPPER:
        return "faster"
    if high >= REGRESSION_UPPER:
        return "slower" if low > 1.0 else "inconclusive"
    return "no difference"


def main():
    options = parse_arguments()
    if options.rounds < 2:
        sys.exit("--rounds must be at least 2")
    try:
        (label_a, binary_a), (label_b, binary_b) = locate_sides(options)
        arguments = benchlib.selection_arguments(options)
        commands = {"A": (binary_a, ["--impl", options.impl_a, *arguments]),
                    "B": (binary_b, ["--impl", options.impl_b, *arguments])}
        load_before = benchlib.load_average()
        rounds = []  # per round: {"A": json, "B": json}
        for index in range(options.rounds):
            order = "AB" if index % 2 == 0 else "BA"
            print(f"round {index + 1}/{options.rounds} ({order})", file=sys.stderr, flush=True)
            results = {}
            for side in order:
                results[side] = benchlib.run_bench(*commands[side])
            rounds.append(results)
        load_after = benchlib.load_average()

        context_a, context_b = rounds[0]["A"]["context"], rounds[0]["B"]["context"]
        metric, metric_label = benchlib.metric_of(context_a)
        if benchlib.metric_of(context_b)[0] != metric:
            raise benchlib.BenchError("A and B measured different metrics")

        generator = random.Random(20240229)
        needed = max(MIN_VALID_ROUNDS, options.rounds // 2)
        rows = []
        for position, benchmark in enumerate(rounds[0]["A"]["benchmarks"]):
            name = benchmark["predicate"] + "/" + benchmark["workload"]
            samples = []
            for results in rounds:
                a, b = results["A"]["benchmarks"][position], results["B"]["benchmarks"][position]
                sample = round_ratio(a, b, metric)
                if sample:
                    samples.append(sample)
            row = {"name": name, "predicate": benchmark["predicate"], "workload": benchmark["workload"],
                   "rounds_valid": len(samples),
                   "target": any(fnmatch.fnmatchcase(name, pattern) for pattern in options.target)}
            if len(samples) >= needed:
                ratios = [sample[0] for sample in samples]
                row["a"] = benchlib.median([sample[1] for sample in samples])
                row["b"] = benchlib.median([sample[2] for sample in samples])
                row["ratio"] = benchlib.median(ratios)
                row["low"], row["high"] = bootstrap_interval(ratios, generator)
                row["ratios"] = ratios
                row["verdict"] = classify(row["ratio"], row["low"], row["high"])
            else:
                row["verdict"] = "invalid"
            rows.append(row)
    except benchlib.BenchError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    for pattern in options.target:
        if not any(fnmatch.fnmatchcase(row["name"], pattern) for row in rows):
            print(f"ERROR: --target {pattern} matches no measured workload", file=sys.stderr)
            return 2

    # --- The table
    same_compiler = context_a["cxx_compiler"] == context_b["cxx_compiler"]
    print(f"**A** = {label_a}, implementation `{options.impl_a}`  ")
    print(f"**B** = {label_b}, implementation `{options.impl_b}`  ")
    print(f"Compiler: {context_a['c_compiler']}" + ("" if same_compiler else f" (A) / {context_b['c_compiler']} (B)")
          + f", flags `{(context_a['c_flags'] + ' ' + context_a['strict_fp_flags']).strip()}`  ")
    print(f"Metric: {metric_label} per call; {options.rounds} interleaved rounds of fresh processes, "
          f"{int(100 * CONFIDENCE)}% bootstrap interval of the median ratio  ")
    load = "" if load_before is None else f", load average {load_before:.1f} to {load_after:.1f}"
    print(f"Machine: {context_a['cpu']} ({context_a['hardware_model']}), {context_a['os']}{load}")
    print()
    print(f"| predicate | workload | A {metric}/call | B {metric}/call | B/A | 95% CI | rounds | verdict |")
    print("|---|---|--:|--:|--:|:-:|--:|---|")
    for row in rows:
        mark = " (target)" if row["target"] else ""
        if row["verdict"] == "invalid":
            print(f"| {row['predicate']} | `{row['workload']}`{mark} | - | - | - | - | "
                  f"{row['rounds_valid']}/{options.rounds} | invalid |")
            continue
        verdict = row["verdict"] if row["verdict"] == "no difference" else f"**{row['verdict']}**"
        print(f"| {row['predicate']} | `{row['workload']}`{mark} | {benchlib.format_number(row['a'])} | "
              f"{benchlib.format_number(row['b'])} | {row['ratio']:.4f} | [{row['low']:.4f}, {row['high']:.4f}] | "
              f"{row['rounds_valid']}/{options.rounds} | {verdict} |")
    print()

    # --- The verdict
    def names(verdict, targets=None):
        return [row["name"] for row in rows
                if row["verdict"] == verdict and (targets is None or row["target"] == targets)]

    slower, inconclusive, invalid = names("slower"), names("inconclusive"), names("invalid")
    targets = [row for row in rows if row["target"]]
    targets_faster = bool(targets) and all(row["verdict"] == "faster" for row in targets)
    if slower:
        overall = "REGRESSION: slower on " + ", ".join(slower)
    elif invalid or inconclusive:
        overall = ("INCONCLUSIVE: " + ", ".join(invalid + inconclusive) +
                   " could not be decided (busy machine or too few rounds); run again with more --rounds")
    elif targets_faster:
        overall = ("FASTER: every target workload is faster and no other workload has an upper bound "
                   f"at or above {REGRESSION_UPPER}")
    elif targets:
        overall = "NOT FASTER: target not met on " + ", ".join(
            row["name"] for row in targets if row["verdict"] != "faster")
    elif names("faster"):
        overall = "NO REGRESSION; faster on " + ", ".join(names("faster")) + " (no --target was declared)"
    else:
        overall = "NO DIFFERENCE on every workload"
    print(f"Verdict: **{overall}**")
    if metric != "cycles":
        print("\nThis comparison used wall-clock time. It is not evidence for or against a change.")

    if options.json:
        with open(options.json, "w") as file:
            json.dump({"a": {"label": label_a, "implementation": options.impl_a, "context": context_a},
                       "b": {"label": label_b, "implementation": options.impl_b, "context": context_b},
                       "rounds": options.rounds, "metric": metric, "verdict": overall, "rows": rows,
                       "rule": {"faster_ratio": FASTER_RATIO, "faster_upper": FASTER_UPPER,
                                "regression_upper": REGRESSION_UPPER, "confidence": CONFIDENCE},
                       "load_avg_1min": [load_before, load_after]}, file, indent=1)
            file.write("\n")
    return 1 if slower and options.fail_on_regression else 0


if __name__ == "__main__":
    sys.exit(main())
