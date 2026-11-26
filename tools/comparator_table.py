#!/usr/bin/env python3
"""Print the comparator table of docs/benchmarking.md from committed result files.

    tools/comparator_table.py            # print the tables
    tools/comparator_table.py --check    # fail if docs/benchmarking.md does not contain them
    tools/comparator_table.py --losses   # list the benchmarks where a comparator beats the library

One table per compiler: for each predicate and workload, median CPU cycles
per call of the 1996 reference, of this library and of every comparator
(docs/comparators.md), from bench/results/<name>-<machine>-<compiler>.json as
written by tools/bench_suite.py.

Marks:
  bold   the fastest in the row among the entries whose signs were checked
  †      the comparator's sign checksum differs from the reference's on this
         workload: it returned wrong signs
  ‡      the comparator answers a different question by design (symbolic
         perturbation, another orientation convention): no checksum is compared
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MACHINE = "apple-m4"
COMPILERS = [("appleclang-21", "Apple clang 21"), ("gcc-16", "GCC 16")]
# (file name prefix, column heading)
COLUMNS = [
    ("baseline", "1996"),
    ("library", "this library"),
    ("indirect", "Indirect_Predicates"),
    ("geogram", "Geogram PCK"),
    ("cgal", "CGAL EPICK"),
    ("lenthe", "GeometricPredicates"),
    ("gmp_rational", "GMP rationals"),
]
WORKLOADS = ["uniform", "mix_easy", "mix_surface", "stage_D", "wide_exponents"]
PREDICATES = ["orient2d", "orient3d", "incircle", "insphere"]
BEGIN, END = "<!-- comparator-table:begin -->", "<!-- comparator-table:end -->"
# A comparator is reported as faster than the library only beyond this margin
# (the acceptance rule of docs/benchmarking.md calls anything closer neutral).
MARGIN = 0.98


def load(kind, compiler):
    """{(predicate, workload): benchmark entry} of one result file, or None."""
    path = os.path.join(ROOT, "bench", "results", f"{kind}-{MACHINE}-{compiler}.json")
    if not os.path.isfile(path):
        return None
    with open(path) as file:
        data = json.load(file)
    return {(b["predicate"], b["workload"]): b for b in data["benchmarks"]
            if b["implementation"] != "overhead"}


def number(value):
    if value >= 100:
        return f"{value:,.0f}"
    return f"{value:.1f}"


def mark(entry):
    if not entry.get("checksum_checked", True):
        return "‡"
    if not entry.get("checksum_ok", True):
        return "†"
    return ""


def table():
    lines = []
    for compiler, label in COMPILERS:
        columns = [(heading, load(kind, compiler)) for kind, heading in COLUMNS]
        columns = [(heading, data) for heading, data in columns if data is not None]
        lines.append(f"**{label}**, CPU cycles per call:")
        lines.append("")
        lines.append("| predicate | workload | " + " | ".join(heading for heading, _ in columns) + " |")
        lines.append("|---|---|" + "--:|" * len(columns))
        for predicate in PREDICATES:
            for workload in WORKLOADS:
                entries = [data.get((predicate, workload)) for _, data in columns]
                checked = [e["cycles"] for e in entries if e is not None and not mark(e)]
                best = min(checked)
                cells = []
                for entry in entries:
                    if entry is None:
                        cells.append("-")
                        continue
                    text = number(entry["cycles"])
                    if not mark(entry) and entry["cycles"] == best:
                        text = f"**{text}**"
                    cells.append(text + mark(entry))
                lines.append(f"| `{predicate}` | `{workload}` | " + " | ".join(cells) + " |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def losses():
    """Every benchmark, of all workloads, where a comparator is faster than the library."""
    lines = []
    for compiler, label in COMPILERS:
        library = load("library", compiler)
        for kind, heading in COLUMNS[2:]:
            data = load(kind, compiler)
            if data is None:
                continue
            for key, entry in data.items():
                ours = library.get(key)
                if ours is None or entry["cycles"] >= MARGIN * ours["cycles"]:
                    continue
                lines.append(
                    f"| {label} | `{key[0]}` | `{key[1]}` | {heading}{mark(entry)} | "
                    f"{number(entry['cycles'])} | {number(ours['cycles'])} | "
                    f"{ours['cycles'] / entry['cycles']:.2f}x |")
    header = ["| compiler | predicate | workload | comparator | its cycles | this library | library / comparator |",
              "|---|---|---|---|--:|--:|--:|"]
    return "\n".join(header + lines) + "\n"


def main():
    arguments = sys.argv[1:]
    if "--losses" in arguments:
        sys.stdout.write(losses())
        return
    text = table()
    if "--check" not in arguments:
        sys.stdout.write(text)
        return
    with open(os.path.join(ROOT, "docs", "benchmarking.md")) as file:
        document = file.read()
    start, end = document.find(BEGIN), document.find(END)
    if start < 0 or end < 0 or document[start + len(BEGIN):end].strip() != text.strip():
        sys.exit("The comparator table of docs/benchmarking.md is out of date: "
                 "run tools/comparator_table.py")


if __name__ == "__main__":
    main()
