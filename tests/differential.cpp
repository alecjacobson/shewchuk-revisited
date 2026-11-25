// Differential test driver.
//
// Runs one implementation over the generated corpus and checks, per case:
//   1. its sign equals the exact oracle's (the contract: zero mismatches);
//   2. its sign equals the pristine reference's (control);
//   3. its value equals the reference's bit for bit (informational, and
//      mandatory for changes declared refactor-only: --require-identical).
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

#include "corpus.h"
#include "generators.h"
#include "implementations.h"
#include "oracle.h"

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
      "                           for the reference, the value hash does too\n"
      "  --require-identical      fail unless every value equals the reference's\n"
      "  --expect-mismatch        negative control: pass only if the implementation\n"
      "                           DISAGREES with the oracle at least once\n"
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
};

}  // namespace

int main(int argc, char** argv) {
  const char* impl_name = "reference";
  const Tier* tier = find_tier("quick");
  long custom_cases = 0;
  bool check_golden = false, require_identical = false, expect_mismatch = false;
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
    } else if (arg == "--expect-mismatch") {
      expect_mismatch = true;
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

    // A NaN has no sign: never let it pass as "zero".
    const bool wrong = !std::isfinite(value) || sign_of(value) != expected;
    if (wrong) {
      t.oracle_mismatches++;
      if (reports < max_reports) {
        reports++;
        std::printf("  WRONG SIGN in %s: returned %a, exact sign is %d\n", generator.name.c_str(),
                    value, expected);
        print_case(pred, c);
      }
    }
    if (sign_of(value) != sign_of(reference_value)) t.reference_mismatches++;
    if (bits_of(value) != bits_of(reference_value)) t.value_differences++;
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
  }
  std::printf("%-9s %10ld %16ld %18ld %19ld\n", "total", sum.cases, sum.oracle_mismatches,
              sum.reference_mismatches, sum.value_differences);
  std::printf("corpus hash: %016" PRIx64 "\n", corpus_hash.value());
  std::printf("value hash:  %016" PRIx64 "\n", value_hash.value());

  bool ok = true;
  if (check_golden) {
    if (corpus_hash.value() != tier->corpus_hash) {
      std::printf("FAIL: corpus hash differs from the recorded %016" PRIx64
                  ": the generators produced different inputs on this build\n",
                  tier->corpus_hash);
      ok = false;
    }
    if (is_reference && value_hash.value() != tier->reference_value_hash) {
      std::printf("FAIL: reference value hash differs from the recorded %016" PRIx64
                  ": this build of predicates.c does not return the recorded values\n",
                  tier->reference_value_hash);
      ok = false;
    }
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
  } else if (sum.oracle_mismatches != 0) {
    std::printf("FAIL: %ld results disagree with the exact oracle\n", sum.oracle_mismatches);
    ok = false;
  }
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
