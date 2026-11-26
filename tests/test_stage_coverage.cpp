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
//
// "library" uses the library's test-only build. The library has one stage the
// 1996 code lacks: the exact integer stage (I). It sits directly behind the
// first filter (for orient2d: behind stage C; see integer_stage_precedes() in
// width.h) and takes every case that reaches it whose coordinates are narrow
// enough (width.h); wider cases fall through to the 1996 stages. So for the
// library the gate additionally requires, for every single case:
//   * decided in the reference by a stage before the integer stage: the same
//     stage in the library;
//   * otherwise, width at most INTEGER_STAGE_MAX_WIDTH: stage I;
//   * otherwise (the fall-through path): the same stage as in the reference.
// The stages of the library behind the integer stage (B, Bx, C and D; for
// orient2d, D) are therefore reached only by cases too wide for it, and the
// gate requires enough of those for each of them: the fall-through path must
// stay covered.
//
// The corpus is inside the exponent range of the 1996 paper, so the library's
// input-domain guard (src/filter_guard.h) must never be in its way: the gate
// also requires, for every single case, that
//   * a case the reference decides in its first test (A0, A) is decided by
//     the library's first test with its guard holding, without a detour
//     through the domain check. Two exceptions, both with the reference's
//     value: orient2d with two zero products, which leaves by a branch of
//     its own; and insphere when every coordinate is below 2^-120 in
//     magnitude, where the error bound is below the guard's floor (2^-683)
//     and the first filter's value is returned after the domain check
//     ("rechecked");
//   * the wide tier of the integer stage decides none.
//
// Also checks that the stage-recording build returns exactly the values of
// the build it instruments (the pristine reference, or the shipped library),
// i.e. that the instrumentation is behavior-neutral.
#include <cstdio>
#include <cstring>
#include <string>

#include "check.h"
#include "corpus.h"
#include "generators.h"
#include "implementations.h"
#include "stages.h"
#include "test_hooks.h"
#include "width.h"

using namespace shewchuk_test;

namespace {

// Every stage must decide at least this share of its predicate's cases...
const double MIN_SHARE = 0.02;
// ...and at least this many cases in absolute terms.
const long MIN_COUNT = 200;
// The stages behind the library's integer stage only see cases too wide for
// it, which only the wide_span generator families (and a few others)
// produce. Each must still decide at least this share of its predicate's
// cases, and MIN_COUNT cases.
const double MIN_FALL_THROUGH_SHARE = 0.005;

bool stage_exists(int pred, int stage) { return stage != STAGE_A0 || pred == ORIENT2D; }

void print_row(const char* label, long cases, const long* counts, int num_stages) {
  std::printf("%-42s %8ld |", label, cases);
  for (int s = 0; s < num_stages; s++) std::printf(" %6.2f", 100.0 * counts[s] / cases);
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
  // The build whose values the stage-recording build must reproduce.
  const Implementation& uninstrumented =
      library ? *find_implementation("library") : reference_implementation();
  const int num_stages = library ? NUM_LIBRARY_STAGES : NUM_STAGES;

  long totals[NUM_PREDS][NUM_LIBRARY_STAGES] = {};
  long cases[NUM_PREDS] = {};
  long unrecorded = 0, value_differences = 0, stage_differences = 0;
  long path_differences = 0, zero_product_cases = 0, rechecked_cases = 0;

  std::printf("Share of cases decided by each stage of the %s, in percent (tier %s)\n", subject,
              tier->name);
  std::printf("%-42s %8s |", "generator", "cases");
  for (int s = 0; s < num_stages; s++) std::printf(" %6s", STAGE_NAME[s]);
  std::printf("\n");
  const Generator* current = nullptr;
  long current_counts[NUM_LIBRARY_STAGES] = {}, current_cases = 0;
  auto flush = [&] {
    if (current) print_row(current->name.c_str(), current_cases, current_counts, num_stages);
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
      shewchuk_hooked_last_path = -1;
      shewchuk_hooked_last_integer_tier = -1;
      staged_value = staged_library_call(generator.pred, c, &stage);
      const int path = shewchuk_hooked_last_path;
      int reference_stage = -1;
      staged_reference_call(generator.pred, c, &reference_stage);
      // The path the library must take (see the top).
      bool expected_path;
      if (reference_stage == STAGE_A0 || reference_stage == STAGE_A) {
        expected_path = path == SHEWCHUK_PATH_LEAF;
        if (generator.pred == ORIENT2D && reference_stage == STAGE_A0 &&
            path == SHEWCHUK_PATH_ZERO_PRODUCTS) {
          expected_path = staged_value == 0.0;
          zero_product_cases++;
        }
        if (generator.pred == INSPHERE && path == SHEWCHUK_PATH_RECHECKED) {
          expected_path = extent_of(c, NUM_COORDS[INSPHERE]).highest <= -120;
          rechecked_cases++;
        }
      } else if (generator.pred == ORIENT2D) {
        expected_path = path == SHEWCHUK_PATH_LEAF;
      } else {
        expected_path = path == SHEWCHUK_PATH_AFTER;
      }
      if (stage == STAGE_I && shewchuk_hooked_last_integer_tier == INTEGER_STAGE_WIDE_TIER) {
        expected_path = false;
      }
      if (!in_floating_point_domain(generator.pred, c)) expected_path = false;
      if (!expected_path) {
        if (path_differences < 5) {
          std::printf("WRONG PATH in %s: path %d, stage %d, reference stage %d\n",
                      generator.name.c_str(), path, stage, reference_stage);
        }
        path_differences++;
      }
      // The stage the library must decide this case in (see the top).
      const int width = width_of(c, NUM_COORDS[generator.pred]);
      const bool narrow = width >= 0 && width <= INTEGER_STAGE_MAX_WIDTH;
      const int expected_stage = integer_stage_precedes(generator.pred, reference_stage) && narrow
                                     ? static_cast<int>(STAGE_I)
                                     : reference_stage;
      if (stage != expected_stage) {
        if (stage_differences < 5) {
          std::printf("WRONG STAGE in %s: decided by stage %d, expected %d (width %d)\n",
                      generator.name.c_str(), stage, expected_stage, width);
        }
        stage_differences++;
      }
    } else {
      staged_value = staged_reference_call(generator.pred, c, &stage);
    }
    const double value = uninstrumented.call(generator.pred, c);
    if (std::memcmp(&staged_value, &value, sizeof value) != 0) value_differences++;
    if (stage < 0 || stage >= num_stages) {
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
  for (int s = 0; s < num_stages; s++) std::printf(" %15s", STAGE_NAME[s]);
  std::printf("\n");
  for (int p = 0; p < NUM_PREDS; p++) {
    std::printf("%-9s %8ld", PRED_NAME[p], cases[p]);
    for (int s = 0; s < num_stages; s++) {
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
    for (int s = 0; s < num_stages; s++) {
      if (!stage_exists(p, s)) {
        CHECK(totals[p][s] == 0);
        continue;
      }
      // In the library, the stages behind stage I are the fall-through path.
      const bool fall_through = library && integer_stage_precedes(static_cast<Pred>(p), s);
      const double min_share = fall_through ? MIN_FALL_THROUGH_SHARE : MIN_SHARE;
      const bool enough = totals[p][s] >= MIN_COUNT && totals[p][s] >= min_share * cases[p];
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
    std::printf("%ld values differ between the stage-recording build and the build it "
                "instruments\n",
                value_differences);
  }
  CHECK(value_differences == 0);
  if (stage_differences) {
    std::printf("%ld cases are not decided by the stage the library must decide them in\n",
                stage_differences);
  }
  CHECK(stage_differences == 0);
  if (library) {
    std::printf("%ld cases took a path the input-domain guard must not send a corpus case on "
                "(%ld orient2d cases with two zero products left by their own branch; %ld tiny "
                "insphere cases were rechecked)\n",
                path_differences, zero_product_cases, rechecked_cases);
    // The recheck must stay the rare exception it is on in-range input.
    CHECK(rechecked_cases * 1000 < cases[INSPHERE]);
  }
  CHECK(path_differences == 0);
  return finish("stage coverage");
}
