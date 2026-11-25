// Stage-coverage gate.
//
//   test_stage_coverage [tier] [reference|library]
//
// Runs the corpus through a stage-recording build, prints which stage decided
// how many cases, and fails unless every stage of every predicate is reached
// often enough. A corpus that never leaves the first filter would make every
// other exactness test meaningless.
//
// "reference" (the default) uses the instrumented copy of the pristine file.
// "library" uses the library's test-only build, and additionally requires
// that every single case is decided by the same stage as in the reference.
//
// Also checks that the stage-recording build returns exactly the values of
// the pristine build, i.e. that the instrumentation is behavior-neutral.
#include <cstdio>
#include <cstring>
#include <string>

#include "check.h"
#include "corpus.h"
#include "generators.h"
#include "implementations.h"
#include "stages.h"

using namespace shewchuk_test;

namespace {

// Every stage must decide at least this share of its predicate's cases...
const double MIN_SHARE = 0.02;
// ...and at least this many cases in absolute terms.
const long MIN_COUNT = 200;

bool stage_exists(int pred, int stage) { return stage != STAGE_A0 || pred == ORIENT2D; }

void print_row(const char* label, long cases, const long* counts) {
  std::printf("%-42s %8ld |", label, cases);
  for (int s = 0; s < NUM_STAGES; s++) std::printf(" %6.2f", 100.0 * counts[s] / cases);
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  const Tier* tier = find_tier(argc > 1 ? argv[1] : "quick");
  if (!tier) {
    std::printf("unknown tier '%s'\n", argv[1]);
    return 2;
  }
  const char* subject = argc > 2 ? argv[2] : "reference";
  const bool library = std::strcmp(subject, "library") == 0;
  if (!library && std::strcmp(subject, "reference") != 0) {
    std::printf("unknown subject '%s' (reference or library)\n", subject);
    return 2;
  }
  initialize_implementations();
  const Implementation& reference = reference_implementation();

  long totals[NUM_PREDS][NUM_STAGES] = {};
  long cases[NUM_PREDS] = {};
  long unrecorded = 0, value_differences = 0, stage_differences = 0;

  std::printf("Share of cases decided by each stage of the %s, in percent (tier %s)\n", subject,
              tier->name);
  std::printf("%-42s %8s | %6s %6s %6s %6s %6s %6s\n", "generator", "cases", "A0", "A", "B", "Bx",
              "C", "D");
  const Generator* current = nullptr;
  long current_counts[NUM_STAGES] = {}, current_cases = 0;
  auto flush = [&] {
    if (current) print_row(current->name.c_str(), current_cases, current_counts);
    std::memset(current_counts, 0, sizeof current_counts);
    current_cases = 0;
  };

  for_each_case(tier->cases_per_generator, [&](const Generator& generator, const double* c) {
    if (&generator != current) {
      flush();
      current = &generator;
    }
    int stage = -1;
    double staged_value;
    if (library) {
      staged_value = staged_library_call(generator.pred, c, &stage);
      int reference_stage = -1;
      staged_reference_call(generator.pred, c, &reference_stage);
      if (stage != reference_stage) stage_differences++;
    } else {
      staged_value = staged_reference_call(generator.pred, c, &stage);
    }
    const double value = reference.call(generator.pred, c);
    if (std::memcmp(&staged_value, &value, sizeof value) != 0) value_differences++;
    if (stage < 0 || stage >= NUM_STAGES) {
      unrecorded++;
      return;
    }
    totals[generator.pred][stage]++;
    cases[generator.pred]++;
    current_counts[stage]++;
    current_cases++;
  });
  flush();

  std::printf("\nStage distribution per predicate: cases (percent)\n");
  std::printf("%-9s %8s", "predicate", "cases");
  for (int s = 0; s < NUM_STAGES; s++) std::printf(" %15s", STAGE_NAME[s]);
  std::printf("\n");
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s %8ld", PRED_NAME[p], cases[p]);
    for (int s = 0; s < NUM_STAGES; s++) {
      if (stage_exists(p, s)) {
        std::printf(" %7ld (%4.1f%%)", totals[p][s], 100.0 * totals[p][s] / cases[p]);
      } else {
        std::printf(" %15s", "-");
      }
    }
    std::printf("\n");
  }

  // The gate.
  for (int p = 0; p < NUM_PREDS; p++) {
    for (int s = 0; s < NUM_STAGES; s++) {
      if (!stage_exists(p, s)) {
        CHECK(totals[p][s] == 0);
        continue;
      }
      const bool enough = totals[p][s] >= MIN_COUNT && totals[p][s] >= MIN_SHARE * cases[p];
      if (!enough) {
        std::printf("STAGE UNDER-COVERED: %s stage %s decided only %ld of %ld cases\n",
                    PRED_NAME[p], STAGE_NAME[s], totals[p][s], cases[p]);
      }
      CHECK(enough);
    }
  }
  if (unrecorded) std::printf("%ld calls returned without recording a stage\n", unrecorded);
  CHECK(unrecorded == 0);
  if (value_differences) {
    std::printf("%ld values differ between the stage-recording and the pristine build\n",
                value_differences);
  }
  CHECK(value_differences == 0);
  if (stage_differences) {
    std::printf("%ld cases are decided by a different stage than in the reference\n",
                stage_differences);
  }
  CHECK(stage_differences == 0);
  return finish("stage coverage");
}
