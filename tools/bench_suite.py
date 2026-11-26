#!/usr/bin/env python3
"""Run the benchmark suite in several fresh processes and aggregate.

One bench_predicates process is one pass over never-repeated inputs.
Repetitions therefore have to be separate processes: this tool runs the same
binary --runs times, takes the median over the runs for every benchmark, and
reports the run-to-run coefficient of variation, which is the harness's own
noise floor on this machine at this moment.

Example (the canonical baseline of the pristine reference):

    cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
    cmake --build build-bench -j 10 --target bench_predicates
    tools/bench_suite.py --build-dir build-bench --runs 10 --overhead \\
        --out bench/results/baseline.json

Exit status: 0 success; 1 a benchmark had too few valid runs (machine too
busy; nothing is written unless --allow-invalid); 2 error.
"""
import argparse
import json
import sys

import benchlib


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--build-dir", help="CMake build directory containing bench_predicates")
    where.add_argument("--binary", help="path of a bench_predicates executable")
    parser.add_argument("--impl", default="reference", help="implementation (default: reference)")
    parser.add_argument("--overhead", action="store_true",
                        help="also measure the harness overhead (implementation 'overhead' on uniform)")
    parser.add_argument("--runs", type=int, default=10, help="fresh processes per implementation (default 10)")
    parser.add_argument("--pred", help="comma-separated predicates (default: all)")
    parser.add_argument("--workload", help="comma-separated workloads (default: all)")
    parser.add_argument("--scale", type=float, default=1.0, help="multiply every workload's calls")
    parser.add_argument("--seed", type=int, default=0, help="seed of the input streams")
    parser.add_argument("--latency", action="store_true", help="latency mode instead of throughput")
    parser.add_argument("--allow-invalid", action="store_true",
                        help="report benchmarks with too few valid runs instead of failing")
    parser.add_argument("--out", help="write the aggregated JSON here")
    return parser.parse_args()


def aggregate(name, samples, runs, metric):
    """One aggregated benchmark entry from the per-run entries `samples`."""
    valid = [sample for sample in samples if sample["valid"]]
    first = samples[0]
    entry = {
        "name": name,
        "run_name": name,
        "run_type": "aggregate",
        "aggregate_name": "median",
        "repetitions": runs,
        "threads": 1,
        "time_unit": "ns",
        "implementation": first["implementation"],
        "predicate": first["predicate"],
        "workload": first["workload"],
        "calls": first["calls"],
        "runs": runs,
        "runs_valid": len(valid),
        "checksum": first["checksum"],
        "input_hash": first["input_hash"],
    }
    if valid:
        ns = [sample["real_time"] for sample in valid]
        entry["iterations"] = valid[0]["iterations"]
        entry["real_time"] = entry["cpu_time"] = benchlib.median(ns)
        entry["ns_min"] = min(ns)
        entry["ns_cv_percent"] = benchlib.cv_percent(ns)
        if metric == "cycles":
            cycles = [sample["cycles"] for sample in valid]
            instructions = [sample["instructions"] for sample in valid]
            entry["cycles"] = benchlib.median(cycles)
            entry["cycles_min"] = min(cycles)
            entry["cycles_max"] = max(cycles)
            entry["cycles_cv_percent"] = benchlib.cv_percent(cycles)
            entry["instructions"] = benchlib.median(instructions)
            entry["instructions_cv_percent"] = benchlib.cv_percent(instructions)
    return entry


def main():
    options = parse_arguments()
    if options.runs < 1:
        sys.exit("--runs must be at least 1")
    try:
        binary = options.binary or benchlib.find_binary(options.build_dir)
        jobs = [(options.impl, benchlib.selection_arguments(options))]
        if options.overhead:
            overhead_options = argparse.Namespace(**vars(options))
            overhead_options.workload = "uniform"
            jobs.append(("overhead", benchlib.selection_arguments(overhead_options)))

        load_before = benchlib.load_average()
        runs = []  # (implementation, parsed JSON)
        for run in range(options.runs):
            for implementation, arguments in jobs:
                print(f"run {run + 1}/{options.runs}: {implementation}", file=sys.stderr, flush=True)
                runs.append((implementation, benchlib.run_bench(binary, ["--impl", implementation, *arguments])))
        load_after = benchlib.load_average()
    except benchlib.BenchError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    context = dict(runs[0][1]["context"])
    metric, metric_label = benchlib.metric_of(context)
    # Inputs are deterministic: every run must have seen the same ones.
    samples, order, workloads = {}, [], {}
    for _, data in runs:
        for benchmark in data["benchmarks"]:
            name = benchmark["name"]
            if name not in samples:
                samples[name] = []
                order.append(name)
            elif samples[name][0]["input_hash"] != benchmark["input_hash"]:
                print(f"ERROR: {name}: the inputs differ between runs", file=sys.stderr)
                return 2
            samples[name].append(benchmark)
        for workload in data["workloads"]:
            workloads[(workload["predicate"], workload["workload"])] = workload

    benchmarks = [aggregate(name, samples[name], options.runs, metric) for name in order]

    for key in ("date", "command", "implementation", "implementation_description", "load_avg"):
        context.pop(key, None)
    context["date"] = runs[0][1]["context"]["date"]
    context["suite_command"] = "tools/bench_suite.py " + " ".join(sys.argv[1:])
    context["runs"] = options.runs
    context["aggregate"] = "median over fresh processes; *_cv_percent is the run-to-run variation"
    context["load_avg_1min_before"] = load_before
    context["load_avg_1min_after"] = load_after
    result = {"context": context, "workloads": list(workloads.values()), "benchmarks": benchmarks}

    # --- Report
    key = "cycles" if metric == "cycles" else "real_time"
    print(f"Metric: {metric_label}. {options.runs} fresh processes per implementation, "
          f"{context['cxx_compiler']}, {context['cpu']}.")
    print()
    print(f"| benchmark | {metric}/call | run-to-run CV | ns/call (median) | ns/call (min) | valid runs |")
    print("|---|--:|--:|--:|--:|--:|")
    needed = options.runs // 2 + 1
    too_few, noise = [], []
    for entry in benchmarks:
        if entry["runs_valid"] < needed:
            too_few.append(entry["name"])
        if entry["runs_valid"] == 0:
            print(f"| `{entry['name']}` | - | - | - | - | 0/{options.runs} |")
            continue
        cv = entry["cycles_cv_percent"] if metric == "cycles" else entry["ns_cv_percent"]
        if entry["implementation"] != "overhead":
            noise.append((cv, entry["name"]))
        print(f"| `{entry['name']}` | {benchlib.format_number(entry[key])} | {cv:.2f}% | "
              f"{benchlib.format_number(entry['real_time'])} | {benchlib.format_number(entry['ns_min'])} | "
              f"{entry['runs_valid']}/{options.runs} |")
    print()
    if noise and options.runs > 1:
        noise.sort()
        worst_cv, worst_name = noise[-1]
        median_cv = noise[len(noise) // 2][0]
        result["context"]["noise_cv_percent_median"] = median_cv
        result["context"]["noise_cv_percent_max"] = worst_cv
        result["context"]["noise_cv_percent_max_benchmark"] = worst_name
        print(f"Noise floor (same binary, run-to-run CV of {metric}/call): "
              f"median {median_cv:.2f}%, max {worst_cv:.2f}% ({worst_name}).")
    if load_before is not None:
        print(f"Load average (1 min): {load_before:.2f} before, {load_after:.2f} after.")

    if too_few and not options.allow_invalid:
        print(f"ERROR: fewer than {needed} of {options.runs} runs were valid for: {', '.join(too_few)}.\n"
              "The machine is too busy (chunks were scheduled off the performance cores). "
              "Nothing was written.", file=sys.stderr)
        return 1
    if options.out:
        with open(options.out, "w") as file:
            json.dump(result, file, indent=1)
            file.write("\n")
        print(f"Wrote {options.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
