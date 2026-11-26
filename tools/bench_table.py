#!/usr/bin/env python3
"""Print result files of tools/bench_suite.py side by side as a Markdown table.

    tools/bench_table.py bench/results/baseline-*.json

One row per predicate and workload, and per file one column of cycles per
call and one of nanoseconds per call (medians over the file's runs). The
column heading is the file's compiler. Files without a cycle counter show
"-" for cycles.
"""
import json
import sys

import benchlib


def main():
    paths = sys.argv[1:]
    if not paths:
        sys.exit(__doc__)
    files = []
    for path in paths:
        with open(path) as file:
            files.append(json.load(file))
    rows, order = {}, []
    for index, data in enumerate(files):
        for benchmark in data["benchmarks"]:
            key = (benchmark["implementation"], benchmark["predicate"], benchmark["workload"])
            if key not in rows:
                rows[key] = [None] * len(files)
                order.append(key)
            rows[key][index] = benchmark
    labels = [data["context"]["c_compiler"] for data in files]
    print("| implementation | predicate | workload | "
          + " | ".join(f"{label} cycles | {label} ns" for label in labels) + " |")
    print("|---|---|---|" + "--:|--:|" * len(files))
    for key in order:
        cells = []
        for benchmark in rows[key]:
            if benchmark is None or "real_time" not in benchmark:
                cells += ["-", "-"]
            else:
                cells.append(benchlib.format_number(benchmark["cycles"]) if "cycles" in benchmark else "-")
                cells.append(benchlib.format_number(benchmark["real_time"]))
        print(f"| {key[0]} | {key[1]} | `{key[2]}` | " + " | ".join(cells) + " |")
    print()
    for label, data in zip(labels, files):
        context = data["context"]
        noise = ""
        if "noise_cv_percent_median" in context:
            noise = (f"; run-to-run CV median {context['noise_cv_percent_median']:.2f}%, "
                     f"max {context['noise_cv_percent_max']:.2f}%")
        print(f"- {label}: `{(context['c_flags'] + ' ' + context['strict_fp_flags']).strip()}`, "
              f"{context['runs']} runs, {context['cpu']}, revision {context['git_revision'][:12]}"
              f"{' (modified)' if context['git_dirty'] else ''}{noise}")


if __name__ == "__main__":
    main()
