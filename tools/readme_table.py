#!/usr/bin/env python3
"""Print the performance table of README.md from committed result files.

    tools/readme_table.py            # print the table
    tools/readme_table.py --check    # fail if README.md does not contain it

The table compares bench/results/baseline-<machine>-<compiler>.json (the
pristine 1996 reference) with bench/results/library-<machine>-<compiler>.json
(this library), both written by tools/bench_suite.py. Cells are median CPU
cycles per call, "reference -> library (speedup)".
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MACHINE = "apple-m4"
COMPILERS = [("appleclang-21", "Apple clang 21"), ("gcc-16", "GCC 16")]
WORKLOADS = [
    ("uniform", "random points"),
    ("mix_easy", "2% near-degenerate"),
    ("mix_surface", "60% near-degenerate"),
    ("stage_D", "hardest cases"),
]
PREDICATES = ["orient2d", "orient3d", "incircle", "insphere"]
BEGIN, END = "<!-- performance-table:begin -->", "<!-- performance-table:end -->"


def cycles(kind, compiler):
    path = os.path.join(ROOT, "bench", "results", f"{kind}-{MACHINE}-{compiler}.json")
    with open(path) as file:
        data = json.load(file)
    return {(b["predicate"], b["workload"]): b["cycles"] for b in data["benchmarks"]}


def number(value):
    return f"{value:,.0f}" if value >= 100 else f"{value:.1f}"


def table():
    lines = []
    for compiler, label in COMPILERS:
        reference, library = cycles("baseline", compiler), cycles("library", compiler)
        lines.append(f"**{label}**, CPU cycles per call, 1996 code → this library:")
        lines.append("")
        lines.append("| | " + " | ".join(text for _, text in WORKLOADS) + " |")
        lines.append("|---|" + "--:|" * len(WORKLOADS))
        for predicate in PREDICATES:
            cells = []
            for workload, _ in WORKLOADS:
                old, new = reference[(predicate, workload)], library[(predicate, workload)]
                cells.append(f"{number(old)} → {number(new)} (**{old / new:.1f}x**)")
            lines.append(f"| `{predicate}` | " + " | ".join(cells) + " |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main():
    text = table()
    if "--check" not in sys.argv[1:]:
        sys.stdout.write(text)
        return
    with open(os.path.join(ROOT, "README.md")) as file:
        readme = file.read()
    start, end = readme.find(BEGIN), readme.find(END)
    if start < 0 or end < 0 or readme[start + len(BEGIN):end].strip() != text.strip():
        sys.exit("README.md performance table is out of date: run tools/readme_table.py")


if __name__ == "__main__":
    main()
