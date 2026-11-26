#!/usr/bin/env python3
"""Draw the performance chart of README.md from committed result files.

    tools/readme_chart.py            # rewrite the chart and the table
    tools/readme_chart.py --check    # fail if either is out of date

The chart compares bench/results/baseline-<machine>-<compiler>.json (the
pristine 1996 reference) with bench/results/library-<machine>-<compiler>.json
(this library), both written by tools/bench_suite.py. A bar is the ratio of
median CPU cycles per call, reference / library, on a logarithmic axis that
starts at 1 (no difference); its label repeats the ratio and both medians.

Written: docs/img/performance-light.svg and performance-dark.svg (the README
shows one or the other, by the reader's colour scheme), and the same numbers
as a table in docs/benchmarking.md between the performance-table markers.
Only the standard library is used and the output is deterministic.
"""
import json
import math
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
TABLE_FILE = os.path.join("docs", "benchmarking.md")
CHART_FILES = {
    "light": os.path.join("docs", "img", "performance-light.svg"),
    "dark": os.path.join("docs", "img", "performance-dark.svg"),
}
# One colour per compiler, then the inks. The backgrounds are transparent.
THEMES = {
    "light": {"series": ["#257de0", "#f47230"], "text": "#191919",
              "muted": "#666666", "grid": "#e7e7e7", "axis": "#c2c2c2"},
    "dark": {"series": ["#338ced", "#e76624"], "text": "#efefef",
             "muted": "#9c9c9c", "grid": "#303030", "axis": "#474747"},
}
SANS = "-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif"
MONO = "ui-monospace,SFMono-Regular,Menlo,Consolas,monospace"

# Geometry, in SVG user units.
WIDTH, PANEL_WIDTH, PANEL_HEIGHT, TOP = 840, 420, 204, 46
GUTTER, DECADE, DECADES = 126, 76, 2          # label column; axis is 1x..100x
BAR, BAR_GAP, ROW = 10, 2, 36


def cycles(kind, compiler):
    path = os.path.join(ROOT, "bench", "results", f"{kind}-{MACHINE}-{compiler}.json")
    with open(path) as file:
        data = json.load(file)
    return {(b["predicate"], b["workload"]): b["cycles"] for b in data["benchmarks"]}


def number(value):
    return f"{value:,.0f}" if value >= 100 else f"{value:.1f}"


def measurements():
    return [(cycles("baseline", compiler), cycles("library", compiler))
            for compiler, _ in COMPILERS]


def table(data):
    lines = []
    for (compiler, label), (reference, library) in zip(COMPILERS, data):
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


def coordinate(value):
    text = f"{value:.1f}"
    return text[:-2] if text.endswith(".0") else text


def bar_path(x, y, length, thickness):
    """A horizontal bar: square at the baseline, rounded at the data end."""
    radius = min(4.0, length, thickness / 2)
    c = coordinate
    return (f"M{c(x)} {c(y)}h{c(length - radius)}"
            f"a{c(radius)} {c(radius)} 0 0 1 {c(radius)} {c(radius)}"
            f"v{c(thickness - 2 * radius)}"
            f"a{c(radius)} {c(radius)} 0 0 1 -{c(radius)} {c(radius)}"
            f"h-{c(length - radius)}z")


def chart(data, theme):
    ink = THEMES[theme]
    c = coordinate
    height = TOP + 2 * PANEL_HEIGHT
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {height}" '
        f'width="{WIDTH}" height="{height}" role="img" font-family="{SANS}" font-size="12">',
        "<title>Speedup of this library over the 1996 predicates.c</title>",
        "<desc>Bars: how many times fewer CPU cycles per call than the 1996 code, "
        "logarithmic axis, for four predicates, four workloads and two compilers. "
        "The numbers are tabulated in docs/benchmarking.md.</desc>",
        f'<text x="0" y="14" font-size="13" font-weight="600" fill="{ink["text"]}">'
        "Times faster than the 1996 code</text>",
        f'<text x="0" y="31" font-size="11" fill="{ink["muted"]}">'
        "Labels: speedup, then CPU cycles per call, 1996 code → this library. "
        "Apple M4; logarithmic axis.</text>",
    ]
    for index, (_, label) in enumerate(COMPILERS):  # legend: fixed slots at the right
        x = WIDTH - (len(COMPILERS) - index) * 125
        out.append(f'<rect x="{x}" y="4" width="11" height="11" rx="2" '
                   f'fill="{ink["series"][index]}"/>')
        out.append(f'<text x="{x + 17}" y="14" fill="{ink["muted"]}">{label}</text>')
    for position, predicate in enumerate(PREDICATES):
        left = (position % 2) * PANEL_WIDTH
        top = TOP + (position // 2) * PANEL_HEIGHT
        x0, y0 = left + GUTTER, top + 28
        bottom = y0 + len(WORKLOADS) * ROW
        out.append(f'<text x="{left}" y="{top + 14}" font-family="{MONO}" font-size="13" '
                   f'font-weight="600" fill="{ink["text"]}">{predicate}</text>')
        for decade in range(1, DECADES + 1):
            gx = x0 + decade * DECADE
            out.append(f'<path d="M{gx} {y0}V{bottom}" stroke="{ink["grid"]}"/>')
        for decade in range(DECADES + 1):
            out.append(f'<text x="{x0 + decade * DECADE}" y="{bottom + 15}" font-size="11" '
                       f'text-anchor="middle" fill="{ink["muted"]}">{10 ** decade}×</text>')
        for row, (workload, text) in enumerate(WORKLOADS):
            y = y0 + row * ROW + (ROW - 2 * BAR - BAR_GAP) / 2
            out.append(f'<text x="{x0 - 8}" y="{c(y + BAR + BAR_GAP / 2 + 4)}" '
                       f'text-anchor="end" fill="{ink["text"]}">{text}</text>')
            for index, (reference, library) in enumerate(data):
                old, new = reference[(predicate, workload)], library[(predicate, workload)]
                ratio = old / new
                # A ratio below 1 (slower) would need a bar to the left: clamp
                # the bar, the label still states the ratio.
                length = min(max(math.log10(ratio), 0.0), DECADES) * DECADE
                by = y + index * (BAR + BAR_GAP)
                if length > 0:
                    out.append(f'<path d="{bar_path(x0, by, length, BAR)}" '
                               f'fill="{ink["series"][index]}"/>')
                speedup = f"{ratio:.1f}×"
                lx = x0 + length + 5
                out.append(
                    f'<text x="{c(lx)}" y="{c(by + BAR - 1)}" font-size="10.5">'
                    f'<tspan font-weight="600" fill="{ink["text"]}">{speedup}</tspan>'
                    f'<tspan dx="5" fill="{ink["muted"]}">{number(old)} → {number(new)}</tspan>'
                    "</text>")
        out.append(f'<path d="M{x0} {y0}V{bottom}" stroke="{ink["axis"]}"/>')
    out.append("</svg>")
    return "\n".join(out) + "\n"


def with_table(document, text):
    start, end = document.find(BEGIN), document.find(END)
    if start < 0 or end < 0:
        sys.exit(f"{TABLE_FILE} has no performance-table markers")
    return document[:start + len(BEGIN)] + "\n" + text + document[end:]


def main():
    data = measurements()
    wanted = {path: chart(data, theme) for theme, path in CHART_FILES.items()}
    with open(os.path.join(ROOT, TABLE_FILE), encoding="utf-8") as file:
        wanted[TABLE_FILE] = with_table(file.read(), table(data))
    if "--check" not in sys.argv[1:]:
        for path, text in wanted.items():
            os.makedirs(os.path.dirname(os.path.join(ROOT, path)), exist_ok=True)
            with open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="\n") as file:
                file.write(text)
        return
    stale = []
    for path, text in wanted.items():
        try:
            with open(os.path.join(ROOT, path), encoding="utf-8", newline="") as file:
                if file.read().replace("\r\n", "\n") != text:
                    stale.append(path)
        except FileNotFoundError:
            stale.append(path)
    with open(os.path.join(ROOT, "README.md"), encoding="utf-8") as file:
        readme = file.read()
    stale += [f"README.md (no reference to {path})" for path in CHART_FILES.values()
              if path.replace(os.sep, "/") not in readme]
    if stale:
        sys.exit("out of date: " + ", ".join(stale) + ": run tools/readme_chart.py")


if __name__ == "__main__":
    main()
