#!/usr/bin/env python3
"""Third exact oracle, in another language: Python fractions.Fraction.

Usage: oracle_check.py <differential executable> [cases per generator]

Runs `differential --dump-sample`, which prints one case per line as
"<predicate> <hex float coordinates> <sign from the C++ oracle>", and
recomputes every sign as the sign of the homogeneous determinant by exact
Gaussian elimination over Fractions. That is a different algorithm (the C++
oracles expand cofactors), a different number representation and a different
language. Exits with status 1 on any disagreement.
"""
import subprocess
import sys
from fractions import Fraction

DIMENSION = {"orient2d": 2, "orient3d": 3, "incircle": 2, "insphere": 3}
LIFTED = {"orient2d": False, "orient3d": False, "incircle": True, "insphere": True}


def determinant_sign(matrix):
    """Return the sign of det(matrix) by exact Gaussian elimination."""
    n = len(matrix)
    m = [row[:] for row in matrix]
    sign = 1
    for k in range(n):
        pivot = next((r for r in range(k, n) if m[r][k] != 0), None)
        if pivot is None:
            return 0
        if pivot != k:
            m[k], m[pivot] = m[pivot], m[k]
            sign = -sign
        if m[k][k] < 0:
            sign = -sign
        for r in range(k + 1, n):
            factor = m[r][k] / m[k][k]
            if factor:
                for c in range(k, n):
                    m[r][c] -= factor * m[k][c]
    return sign


def oracle(predicate, coordinates):
    """Return the exact sign of the predicate with the reference's convention."""
    d = DIMENSION[predicate]
    rows = []
    for i in range(0, len(coordinates), d):
        point = coordinates[i:i + d]
        row = list(point)
        if LIFTED[predicate]:
            row.append(sum(x * x for x in point))
        row.append(Fraction(1))
        rows.append(row)
    return determinant_sign(rows)


def main():
    driver = sys.argv[1]
    cases_per_generator = sys.argv[2] if len(sys.argv) > 2 else "100"
    dump = subprocess.run(
        [driver, "--dump-sample", "--cases-per-generator", cases_per_generator],
        capture_output=True, text=True, timeout=300, check=True).stdout
    cases = disagreements = 0
    counts = {-1: 0, 0: 0, 1: 0}
    for line in dump.splitlines():
        fields = line.split()
        if not fields:
            continue
        predicate = fields[0]
        coordinates = [Fraction(float.fromhex(x)) for x in fields[1:-1]]
        expected = int(fields[-1])
        actual = oracle(predicate, coordinates)
        cases += 1
        counts[expected] += 1
        if actual != expected:
            disagreements += 1
            if disagreements <= 5:
                print("DISAGREE", line.strip(), "python:", actual)
    print(f"checked {cases} cases ({counts[-1]} negative, {counts[0]} zero, "
          f"{counts[1]} positive), {disagreements} disagreements")
    if cases < 1000 or min(counts.values()) < cases // 10:
        print("FAIL: the sample is too small or too one-sided to mean anything")
        sys.exit(1)
    sys.exit(1 if disagreements else 0)


if __name__ == "__main__":
    main()
