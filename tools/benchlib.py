"""Helpers shared by bench_suite.py and compare_revs.py (standard library only)."""
import json
import os
import subprocess
import tempfile

# Exit statuses of bench_predicates (see bench/bench_predicates.cpp).
EXIT_OK = 0
EXIT_INVALID = 3  # measured, but some results are flagged invalid


class BenchError(Exception):
    """A benchmark process failed or produced unusable output."""


def find_binary(build_dir):
    """Return the bench_predicates executable inside a CMake build directory."""
    for relative in ("bench", os.path.join("bench", "Release"), os.path.join("bench", "RelWithDebInfo")):
        for name in ("bench_predicates", "bench_predicates.exe"):
            path = os.path.join(build_dir, relative, name)
            if os.path.isfile(path):
                return path
    raise BenchError(f"no bench_predicates executable under {build_dir} (was it built?)")


def run_bench(binary, arguments):
    """Run one fresh bench_predicates process and return its parsed JSON.

    Results flagged invalid by the harness (too few chunks on performance
    cores) are kept in the returned data with "valid": false; callers decide.
    Any other failure (checksum mismatch, unknown name, crash) raises.
    """
    handle, path = tempfile.mkstemp(prefix="bench-", suffix=".json")
    os.close(handle)
    try:
        process = subprocess.run(
            [binary, *arguments, "--out", path],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if process.returncode not in (EXIT_OK, EXIT_INVALID):
            raise BenchError(
                f"{binary} {' '.join(arguments)} exited with status {process.returncode}:\n"
                f"{process.stderr.strip()}")
        with open(path) as file:
            return json.load(file)
    finally:
        os.unlink(path)


def selection_arguments(options):
    """bench_predicates arguments for the options shared by both tools."""
    arguments = ["--scale", repr(options.scale), "--seed", str(options.seed)]
    if options.pred:
        arguments += ["--pred", options.pred]
    if options.workload:
        arguments += ["--workload", options.workload]
    if getattr(options, "latency", False):
        arguments += ["--latency"]
    return arguments


def metric_of(context):
    """("cycles" | "ns", label): what a run measured as its primary metric."""
    if context["primary_metric"] == "cycles":
        return "cycles", "CPU cycles"
    return "ns", "WALL-CLOCK ns (no cycle counter: " + context["counters_fallback_reason"] + ")"


def median(values):
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return 0.5 * (ordered[middle - 1] + ordered[middle])


def cv_percent(values):
    """Sample coefficient of variation in percent (0 for fewer than 2 values)."""
    if len(values) < 2:
        return 0.0
    mean = sum(values) / len(values)
    if mean == 0:
        return 0.0
    variance = sum((value - mean) ** 2 for value in values) / (len(values) - 1)
    return 100.0 * variance ** 0.5 / mean


def load_average():
    """1-minute load average, or None where the platform has none."""
    try:
        return os.getloadavg()[0]
    except (AttributeError, OSError):
        return None


def format_number(value):
    """Three or four significant digits, readable in a table."""
    if value >= 1000:
        return f"{value:,.0f}"
    if value >= 100:
        return f"{value:.1f}"
    return f"{value:.2f}"
