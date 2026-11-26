#!/usr/bin/env python3
"""Re-derives the bit widths of the exact integer stage with exact integers.

    tools/integer_stage_widths.py src/integer_stage.h [--table]

docs/proofs/integer-stage.md bounds the magnitude of every intermediate of
the integer stage by a power of two, and src/integer_stage.h states those
bounds as code (magnitude_bits) next to the number of limbs each intermediate
gets (limbs_of) and the widest width each tier accepts (max_width).

This script does the derivation again, mechanically: for every width W it
propagates the largest possible magnitude, as an exact Python integer, through
the expressions the stage evaluates (the triangle inequality at every sum,
the product of the bounds at every product), and checks that

  1. the header's magnitude_bits(kind, W) really bounds it: bound < 2^k;
  2. for every tier D and every W <= max_width(D), that magnitude plus a sign
     bit fits the limbs: k + 1 <= 64 * limbs_of(kind, D).

The formulas are read from the header itself, so an edit to either side that
breaks the agreement fails here. With --table it prints the table of the
proof document. Exit status 0 if everything holds.
"""
import re
import sys

# The expressions of src/integer_stage.h (struct Tier), as bounds: the
# largest magnitude each kind of intermediate can have when every scaled
# coordinate has magnitude at most M = 2^W - 1.
def largest_magnitudes(W):
    M = (1 << W) - 1
    bound = {"COORDINATE": M}
    bound["DIFFERENCE"] = 2 * M                       # |a - b| <= |a| + |b|
    bound["PRODUCT2"] = bound["DIFFERENCE"] ** 2      # difference * difference
    bound["MINOR2"] = 2 * bound["PRODUCT2"]           # p q - r t
    bound["LIFT2"] = 2 * bound["PRODUCT2"]            # x^2 + y^2
    bound["LIFT3"] = 3 * bound["PRODUCT2"]            # x^2 + y^2 + z^2
    bound["PRODUCT3"] = bound["DIFFERENCE"] * bound["MINOR2"]
    bound["MINOR3"] = 3 * bound["PRODUCT3"]           # three such products
    bound["INCIRCLE_TERM"] = bound["LIFT2"] * bound["MINOR2"]
    bound["INCIRCLE_DET"] = 3 * bound["INCIRCLE_TERM"]
    bound["INSPHERE_TERM"] = bound["LIFT3"] * bound["MINOR3"]
    bound["INSPHERE_DET"] = 4 * bound["INSPHERE_TERM"]
    return bound


def parse_switch(source, function):
    """{kind: expression} from the `case KIND: return EXPRESSION;` lines of a function."""
    start = source.index("constexpr int " + function + "(")
    body = source[start:source.index("\n}\n", start)]
    table, pending = {}, []
    for label, expression in re.findall(r"case (\w+):(?:\s*return ([^;]+);)?", body):
        pending.append(label)
        if expression:
            for kind in pending:
                table[kind] = expression
            pending = []
    if pending:
        raise SystemExit(f"{function}: case labels without a return: {pending}")
    return table


def main():
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(arguments) != 1:
        raise SystemExit(__doc__)
    source = open(arguments[0]).read()
    magnitude_bits = parse_switch(source, "magnitude_bits")
    limbs_of = parse_switch(source, "limbs_of")
    fast_tiers = int(re.search(r"constexpr int num_tiers = (\d+);", source).group(1))
    wide_tier = int(re.search(r"constexpr int wide_tier = (\d+);", source).group(1))
    # The fast tiers 1 ... num_tiers, and the wide tier.
    tiers = list(range(1, fast_tiers + 1)) + [wide_tier]
    max_width_expression = re.search(
        r"constexpr int max_width\(int tier\)(?: noexcept)? \{ return ([^;]+); \}", source).group(1)
    max_width = {D: eval(max_width_expression, {"tier": D}) for D in tiers}
    # The largest width of a finite input: the wide tier must hold it.
    max_finite_width = eval(re.search(
        r"constexpr int max_finite_width = ([^;]+);", source).group(1))

    kinds = list(largest_magnitudes(1))
    if set(kinds) != set(magnitude_bits) or set(kinds) != set(limbs_of):
        raise SystemExit("the header's kinds of intermediates are not the ones this script derives")

    failures = 0
    # 1. The stated power-of-two bounds hold at every width (far beyond the
    # widest tier: the formulas are claimed for all W).
    for W in range(0, 2 * max_width[fast_tiers] + 1):
        derived = largest_magnitudes(W)
        for kind in kinds:
            k = eval(magnitude_bits[kind], {"W": W})
            if not derived[kind] < (1 << k):
                print(f"FAIL: {kind} at W = {W}: the largest magnitude needs "
                      f"{derived[kind].bit_length()} bits, the header says {k}")
                failures += 1
    # 2. Every tier holds every intermediate, sign bit included, up to its limit.
    for D in tiers:
        # (The bounds grow with W, so for the wide tier the widths near its
        # limit, the largest finite width and a sample of the others are
        # enough; the fast tiers are checked at every width.)
        widths = range(0, max_width[D] + 1)
        if D == wide_tier:
            widths = sorted(set(range(0, max_width[D] + 1, 37)) |
                            set(range(max_width[D] - 70, max_width[D] + 1)) | {max_finite_width})
        for W in widths:
            derived = largest_magnitudes(W)
            for kind in kinds:
                bits = 64 * eval(limbs_of[kind], {"D": D})
                if derived[kind].bit_length() + 1 > bits:
                    print(f"FAIL: tier {D}, {kind} at W = {W}: needs "
                          f"{derived[kind].bit_length() + 1} bits, has {bits}")
                    failures += 1
        # How far the derivation reaches: the first width whose derived bound
        # no longer fits (not necessarily a width at which the code overflows).
        W = max_width[D] + 1
        derived = largest_magnitudes(W)
        beyond = [kind for kind in kinds
                  if derived[kind].bit_length() + 1 > 64 * eval(limbs_of[kind], {"D": D})]
        print(f"tier {D}: every intermediate fits for W <= {max_width[D]}; at W = {W} the bound of "
              f"{', '.join(beyond) if beyond else 'nothing'} no longer fits")
    if max_width[wide_tier] < max_finite_width:
        print(f"FAIL: the wide tier holds widths up to {max_width[wide_tier]}, but a finite input "
              f"can have width {max_finite_width}")
        failures += 1
    else:
        narrower = eval(max_width_expression, {"tier": wide_tier - 1})
        print(f"the wide tier ({wide_tier} limbs per difference) holds every finite input "
              f"(largest width {max_finite_width}); {wide_tier - 1} limbs would hold widths up to "
              f"{narrower}")
        if narrower >= max_finite_width:
            print("FAIL: the wide tier is wider than it needs to be")
            failures += 1

    if "--table" in sys.argv:
        print()
        header = "| intermediate | magnitude below 2^k, k = | limbs |"
        rule = "|---|---|---|"
        for D in tiers:
            header += f" tier {D}: bits needed at W = {max_width[D]} / bits available |"
            rule += "--:|"
        print(header)
        print(rule)
        for kind in kinds:
            row = f"| `{kind}` | {magnitude_bits[kind].replace(' * ', '')} | {limbs_of[kind].replace(' * ', '')} |"
            for D in tiers:
                k = eval(magnitude_bits[kind], {"W": max_width[D]})
                row += f" {k + 1} / {64 * eval(limbs_of[kind], {'D': D})} |"
            print(row)

    if failures:
        raise SystemExit(f"{failures} width checks failed")
    print("PASS: the header's bit widths agree with the exact derivation")


if __name__ == "__main__":
    main()
