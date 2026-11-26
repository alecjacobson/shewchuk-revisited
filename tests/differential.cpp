// Differential test driver.
//
// Runs one implementation over the generated corpus and checks, per case:
//   1. its sign equals the exact oracle's (the contract: zero mismatches);
//   2. its sign equals the pristine reference's (control);
//   3. its value equals the reference's bit for bit (informational, and
//      mandatory for changes declared refactor-only: --require-identical);
//   4. with --integer-stage, the value contract of the library's exact integer
//      stage (see integer_stage_value() below).
// With --comparator (third-party code, docs/comparators.md) the wrong signs are
// also broken down by kind and by generator family, and a nonzero sign for an
// exactly degenerate input is counted separately, not as wrong, for the
// predicates the comparator declares as symbolically perturbed. A comparator
// whose in-circle test does not depend on the orientation of the triangle is
// compared with the exact sign times the exact orientation, and one that
// requires an orientation is checked on the inputs that have it.
// It also hashes the corpus and the returned values and can compare them with
// the hashes recorded in corpus.h (--check-golden).
//
// Exit status: 0 pass, 1 fail, 2 usage error, 77 skipped.
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "corpus.h"
#include "generators.h"
#include "implementations.h"
#include "judge.h"
#include "oracle.h"
#include "stages.h"
#include "width.h"

using namespace shewchuk_test;

namespace {

const int EXIT_SKIPPED = 77;

void usage() {
  std::printf(
      "usage: differential [options]\n"
      "  --impl NAME              implementation to test (default: reference)\n"
      "  --tier NAME              corpus tier: quick (default) or deep\n"
      "  --cases-per-generator N  custom corpus size instead of a tier\n"
      "  --check-golden           fail unless the corpus hash matches corpus.h and,\n"
      "                           for the reference and with --require-identical,\n"
      "                           the value hash does too\n"
      "  --require-identical      fail unless every value equals the reference's\n"
      "  --integer-stage          the library with its exact integer stage: fail unless\n"
      "                           every value decided before the integer stage equals\n"
      "                           the reference's, every other value of a case narrow\n"
      "                           enough for the integer stage is exactly -1, 0 or +1,\n"
      "                           and every remaining value equals the reference's; with\n"
      "                           --check-golden the value hash must be the recorded one\n"
      "  --expect-mismatch        negative control: pass only if the implementation\n"
      "                           DISAGREES with the oracle at least once\n"
      "  --comparator             third-party code: break the wrong signs down by kind and\n"
      "                           by generator family; a nonzero sign for an exact zero is\n"
      "                           not wrong where the implementation declares a symbolic\n"
      "                           perturbation; fail on any wrong sign\n"
      "  --known-wrong            with --comparator: this comparator is recorded as\n"
      "                           returning wrong signs. Pass only if it still does, and\n"
      "                           returns none on the uniform random families (which\n"
      "                           shows that the wrapper's sign convention is right)\n"
      "  --dump-sample            print the corpus with oracle signs and exit\n"
      "  --list                   list implementations and tiers\n");
}

void print_case(Pred pred, const double* c) {
  std::printf("    %s", PRED_NAME[pred]);
  for (int i = 0; i < NUM_COORDS[pred]; i++) std::printf(" %a", c[i]);
  std::printf("\n");
}

std::uint64_t bits_of(double x) {
  std::uint64_t bits;
  std::memcpy(&bits, &x, sizeof bits);
  return bits;
}

struct Totals {
  long cases = 0;
  long oracle_mismatches = 0;     // wrong or non-finite result
  long reference_mismatches = 0;  // sign differs from the reference
  long value_differences = 0;     // value bits differ from the reference
  long contract_violations = 0;   // --integer-stage: not the value the contract fixes
  long integer_stage_cases = 0;   // --integer-stage: cases the integer stage must decide
  // --comparator: the wrong results by kind, and the perturbed zeros.
  long opposite_sign = 0;      // exact sign nonzero, the opposite returned
  long zero_for_nonzero = 0;   // exact sign nonzero, zero returned
  long nonzero_for_zero = 0;   // exact sign zero, a nonzero sign returned
  long non_finite = 0;         // NaN or an infinity returned
  long perturbed_zeros = 0;    // exact sign zero, nonzero by declared perturbation
  long undefined = 0;          // unoriented predicate on a collinear or coplanar base
  long precondition = 0;       // outside the implementation's stated precondition
};

// --comparator: what one generator family saw.
struct FamilyTotals {
  std::string name;
  long cases = 0, wrong = 0, perturbed_zeros = 0;
};

// The value the library must return now that it has the exact integer stage
// (--integer-stage). The stage knows only the sign, so the library is no
// longer bit-identical to the 1996 code, and this is the whole difference:
//   * a call decided by a stage that comes before the integer stage (the
//     first filter; for orient2d also stages B, Bx and C: see
//     integer_stage_precedes() in width.h) returns the reference's value, bit
//     for bit. Which stage decides is found with the stage-instrumented
//     reference;
//   * any other call whose coordinates are narrow enough (width.h) is decided
//     by the integer stage and returns exactly -1.0, +0.0 or +1.0, the exact
//     sign;
//   * any other call falls through to the 1996 stages and returns the
//     reference's value, bit for bit.
// `*by_integer_stage` reports whether the second rule applied.
double integer_stage_value(Pred pred, const double* c, double reference_value, int exact_sign,
                           bool* by_integer_stage) {
  int reference_stage = -1;
  staged_reference_call(pred, c, &reference_stage);
  const int width = width_of(c, NUM_COORDS[pred]);
  *by_integer_stage = integer_stage_precedes(pred, reference_stage) && width >= 0 &&
                      width <= INTEGER_STAGE_MAX_WIDTH;
  return *by_integer_stage ? static_cast<double>(exact_sign) : reference_value;
}

}  // namespace

int main(int argc, char** argv) {
  const char* impl_name = "reference";
  const Tier* tier = find_tier("quick");
  long custom_cases = 0;
  bool check_golden = false, require_identical = false, expect_mismatch = false;
  bool integer_stage = false;
  bool comparator = false, known_wrong = false;
  bool dump_sample = false;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--impl" && has_value) {
      impl_name = argv[++i];
    } else if (arg == "--tier" && has_value) {
      tier = find_tier(argv[++i]);
      if (!tier) {
        std::printf("unknown tier '%s'\n", argv[i]);
        return 2;
      }
    } else if (arg == "--cases-per-generator" && has_value) {
      custom_cases = std::atol(argv[++i]);
      if (custom_cases < 1) {
        std::printf("--cases-per-generator needs a positive number\n");
        return 2;
      }
    } else if (arg == "--check-golden") {
      check_golden = true;
    } else if (arg == "--require-identical") {
      require_identical = true;
    } else if (arg == "--integer-stage") {
      integer_stage = true;
    } else if (arg == "--expect-mismatch") {
      expect_mismatch = true;
    } else if (arg == "--comparator") {
      comparator = true;
    } else if (arg == "--known-wrong") {
      known_wrong = true;
    } else if (arg == "--dump-sample") {
      dump_sample = true;
    } else if (arg == "--list") {
      std::printf("implementations:\n");
      for (const Implementation& impl : all_implementations()) {
        std::printf("  %-22s %s\n", impl.name, impl.description);
        if (const char* reason = impl.unavailable_reason()) {
          std::printf("  %-22s unavailable here: %s\n", "", reason);
        }
      }
      std::printf("tiers:\n");
      for (const Tier& t : TIERS) std::printf("  %-6s %ld cases per generator\n", t.name, t.cases_per_generator);
      return 0;
    } else {
      usage();
      return 2;
    }
  }
  if (integer_stage && require_identical) {
    std::printf("--integer-stage and --require-identical exclude each other\n");
    return 2;
  }
  if (known_wrong && !comparator) {
    std::printf("--known-wrong needs --comparator\n");
    return 2;
  }
  if (comparator && (require_identical || integer_stage || expect_mismatch)) {
    std::printf("--comparator excludes --require-identical, --integer-stage and "
                "--expect-mismatch\n");
    return 2;
  }
  if (custom_cases && check_golden) {
    std::printf("--check-golden needs a tier, not --cases-per-generator\n");
    return 2;
  }
  const long cases_per_generator = custom_cases ? custom_cases : tier->cases_per_generator;

  initialize_implementations();

  if (dump_sample) {
    // One line per case: predicate, coordinates as hex floats, oracle sign.
    for_each_case(cases_per_generator, [](const Generator& generator, const double* c) {
      std::printf("%s", PRED_NAME[generator.pred]);
      for (int i = 0; i < NUM_COORDS[generator.pred]; i++) std::printf(" %a", c[i]);
      std::printf(" %d\n", oracle_sign(generator.pred, c));
    });
    return 0;
  }

  const Implementation* impl = find_implementation(impl_name);
  if (!impl) {
    std::printf("unknown implementation '%s' (see --list)\n", impl_name);
    return 2;
  }
  if (const char* reason = impl->unavailable_reason()) {
    std::printf("SKIPPED: implementation '%s' cannot run here: %s\n", impl->name, reason);
    return EXIT_SKIPPED;
  }
  const Implementation& reference = reference_implementation();
  const bool is_reference = impl == &reference;

  Totals totals[NUM_PREDS];
  std::vector<FamilyTotals> families;
  Hash64 corpus_hash, value_hash;
  const int max_reports = 5;
  int reports = 0;

  for_each_case(cases_per_generator, [&](const Generator& generator, const double* c) {
    const Pred pred = generator.pred;
    Totals& t = totals[pred];
    t.cases++;
    for (int i = 0; i < NUM_COORDS[pred]; i++) corpus_hash.add_double(c[i]);

    const int expected = oracle_sign(pred, c);
    const double value = impl->call(pred, c);
    const double reference_value = is_reference ? value : reference.call(pred, c);
    value_hash.add(bits_of(value));

    if (families.empty() || families.back().name != generator.name) {
      families.push_back({generator.name});
    }
    FamilyTotals& family = families.back();
    family.cases++;

    // What the implementation declares about itself (symbolic perturbation,
    // orientation conventions: comparators only) is taken into account here.
    const Judgement judgement = judge(*impl, pred, c, value, expected);
    const bool wrong = judgement.verdict == Verdict::WRONG;
    if (judgement.verdict == Verdict::PERTURBED_ZERO) {
      t.perturbed_zeros++;
      family.perturbed_zeros++;
    } else if (judgement.verdict == Verdict::UNDEFINED) {
      t.undefined++;
    } else if (judgement.verdict == Verdict::OUTSIDE_PRECONDITION) {
      t.precondition++;
    }
    if (wrong) {
      t.oracle_mismatches++;
      family.wrong++;
      if (!std::isfinite(value)) {
        t.non_finite++;
      } else if (judgement.asked == 0) {
        t.nonzero_for_zero++;
      } else if (value == 0.0) {
        t.zero_for_nonzero++;
      } else {
        t.opposite_sign++;
      }
      if (reports < max_reports) {
        reports++;
        std::printf("  WRONG SIGN in %s: returned %a, exact sign is %d", generator.name.c_str(),
                    value, expected);
        if (judgement.asked != expected) {
          std::printf(" (asked for %d: exact sign x orientation)", judgement.asked);
        }
        std::printf("\n");
        print_case(pred, c);
      }
    }
    if (sign_of(value) != sign_of(reference_value)) t.reference_mismatches++;
    if (bits_of(value) != bits_of(reference_value)) t.value_differences++;
    if (integer_stage) {
      bool by_integer_stage;
      const double contract =
          integer_stage_value(pred, c, reference_value, expected, &by_integer_stage);
      if (by_integer_stage) t.integer_stage_cases++;
      if (bits_of(value) != bits_of(contract)) {
        t.contract_violations++;
        if (reports < max_reports) {
          reports++;
          std::printf("  VALUE CONTRACT VIOLATED in %s: returned %a, the contract says %a (%s)\n",
                      generator.name.c_str(), value, contract,
                      by_integer_stage ? "the exact sign from the integer stage"
                                       : "the reference's value");
          print_case(pred, c);
        }
      }
    }
  });

  std::printf("implementation: %s (%s)\n", impl->name, impl->description);
  std::printf("corpus: %s, %ld cases per generator, %zu generators\n",
              custom_cases ? "custom" : tier->name, cases_per_generator, all_generators().size());
  std::printf("%-9s %10s %16s %18s %19s\n", "predicate", "cases", "wrong vs oracle",
              "sign != reference", "value != reference");
  Totals sum;
  for (int p = 0; p < NUM_PREDS; p++) {
    const Totals& t = totals[p];
    std::printf("%-9s %10ld %16ld %18ld %19ld\n", PRED_NAME[p], t.cases, t.oracle_mismatches,
                t.reference_mismatches, t.value_differences);
    sum.cases += t.cases;
    sum.oracle_mismatches += t.oracle_mismatches;
    sum.reference_mismatches += t.reference_mismatches;
    sum.value_differences += t.value_differences;
    sum.contract_violations += t.contract_violations;
    sum.integer_stage_cases += t.integer_stage_cases;
  }
  std::printf("%-9s %10ld %16ld %18ld %19ld\n", "total", sum.cases, sum.oracle_mismatches,
              sum.reference_mismatches, sum.value_differences);
  if (comparator) {
    std::printf("\nwrong results by kind, and exact zeros returned as a nonzero sign by the "
                "implementation's declared symbolic perturbation (not counted as wrong):\n");
    std::printf("%-9s %14s %17s %17s %11s %16s\n", "predicate", "opposite sign",
                "zero for nonzero", "nonzero for zero", "non-finite", "perturbed zeros");
    for (int p = 0; p < NUM_PREDS; p++) {
      const Totals& t = totals[p];
      std::printf("%-9s %14ld %17ld %17ld %11ld %16ld%s", PRED_NAME[p], t.opposite_sign,
                  t.zero_for_nonzero, t.nonzero_for_zero, t.non_finite, t.perturbed_zeros,
                  impl->perturbs_zero[p] ? "  (symbolically perturbed)" : "");
      if (impl->unoriented[p]) {
        std::printf("  (compared with exact sign x exact orientation of the other points; "
                    "%ld cases skipped, the others being collinear or coplanar)",
                    t.undefined);
      }
      if (impl->required_base_orientation[p] != 0) {
        std::printf("  (requires the other points to have orientation %+d in the 1996 "
                    "convention; %ld cases outside that precondition skipped)",
                    impl->required_base_orientation[p], t.precondition);
      }
      std::printf("\n");
    }
    std::printf("\ngenerator families with wrong results (the corpus stays inside the input "
                "domain of the 1996 paper):\n");
    bool any = false;
    for (const FamilyTotals& family : families) {
      if (family.wrong == 0) continue;
      any = true;
      std::printf("  %-48s %8ld of %8ld (%.2f%%)\n", family.name.c_str(), family.wrong,
                  family.cases,
                  100.0 * static_cast<double>(family.wrong) / static_cast<double>(family.cases));
    }
    if (!any) std::printf("  none\n");
    std::printf("\n");
  }
  std::printf("corpus hash: %016" PRIx64 "\n", corpus_hash.value());
  std::printf("value hash:  %016" PRIx64 "\n", value_hash.value());
  if (integer_stage) {
    std::printf("integer-stage value contract: %ld violations; the integer stage decides %ld "
                "of the %ld cases\n",
                sum.contract_violations, sum.integer_stage_cases, sum.cases);
  }

  bool ok = true;
  if (check_golden) {
    if (corpus_hash.value() != tier->corpus_hash) {
      std::printf("FAIL: corpus hash differs from the recorded %016" PRIx64
                  ": the generators produced different inputs on this build\n",
                  tier->corpus_hash);
      ok = false;
    }
    // The recorded value hash binds the reference and every implementation
    // declared refactor-only.
    if ((is_reference || require_identical) && value_hash.value() != tier->reference_value_hash) {
      std::printf("FAIL: value hash differs from the recorded reference value hash %016" PRIx64
                  ": this build does not return the recorded values\n",
                  tier->reference_value_hash);
      ok = false;
    }
    // The values of the library with its integer stage are fixed by the
    // contract above, so their hash is recorded too.
    if (integer_stage && value_hash.value() != tier->library_value_hash) {
      std::printf("FAIL: value hash differs from the recorded library value hash %016" PRIx64 "\n",
                  tier->library_value_hash);
      ok = false;
    }
  }
  if (integer_stage && sum.contract_violations != 0) {
    std::printf("FAIL: %ld values are not the ones the integer-stage contract fixes\n",
                sum.contract_violations);
    ok = false;
  }
  if (require_identical && sum.value_differences != 0) {
    std::printf("FAIL: %ld values differ from the reference\n", sum.value_differences);
    ok = false;
  }
  if (expect_mismatch) {
    if (sum.oracle_mismatches == 0) {
      std::printf("FAIL: negative control was NOT detected: a build that is known to be broken "
                  "agreed with the oracle on every case\n");
      ok = false;
    } else {
      std::printf("negative control detected: %ld wrong signs, as expected\n",
                  sum.oracle_mismatches);
    }
  } else if (known_wrong) {
    // The uniform random families are far from every filter threshold and
    // every exponent limit: a wrong sign there means the wrapper's argument
    // order or sign convention is wrong, not the comparator.
    long uniform_wrong = 0;
    for (const FamilyTotals& family : families) {
      const std::string suffix = "/uniform";
      if (family.name.size() >= suffix.size() &&
          family.name.compare(family.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        uniform_wrong += family.wrong;
      }
    }
    if (uniform_wrong != 0) {
      std::printf("FAIL: %ld wrong signs on uniform random inputs: the wrapper's sign convention "
                  "or argument order is wrong\n",
                  uniform_wrong);
      ok = false;
    }
    if (sum.oracle_mismatches == 0) {
      std::printf("FAIL: this comparator is recorded as returning wrong signs "
                  "(docs/comparators.md) and returned none: update the record\n");
      ok = false;
    } else {
      std::printf("KNOWN: this comparator returns %ld wrong signs on the corpus, as recorded in "
                  "docs/comparators.md\n",
                  sum.oracle_mismatches);
    }
  } else if (sum.oracle_mismatches != 0) {
    std::printf("FAIL: %ld results disagree with the exact oracle\n", sum.oracle_mismatches);
    ok = false;
  }
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
