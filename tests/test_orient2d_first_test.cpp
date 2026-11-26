// The first test of the library's orient2d on edge cases.
//
// The 1996 orient2d first looks at the signs of its two products (stage A0)
// and only then applies the error-bound filter (stage A). The library does
// both with one comparison, and trusts that comparison only if the error
// bound lies in a window (see shewchuk_orient2d in src/predicates.cpp,
// docs/exactness.md and docs/proofs/input-domain.md, section 4). The corpus
// tests already require identical values and identical stages on in-domain
// inputs. This test adds the inputs the arguments have to survive and the
// corpus does not contain: products that are zero of either sign, equal,
// opposite, tiny, subnormal, huge, or overflowed to infinity.
//
// With pc at the origin the two products are ax*by and ay*bx, so every
// combination of special values is reached directly. For every combination:
//   * the library's sign is the exact one (the oracle's): every input here
//     is finite;
//   * if the error bound is inside the window, or both products have a zero
//     factor, the library does what the 1996 code does: a call the reference
//     decides in its first test (A0 or A) is decided by the library's first
//     test, in the same stage, with the same value bit for bit, and a call
//     the reference hands to orient2dadapt() is handed on by the library;
//   * if not, the first test must not be the one that decides (the test-only
//     build reports the path).
// The window is recomputed here from the products, independently of the
// library.
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "check.h"
#include "filter_guard.h"
#include "implementations.h"
#include "oracle.h"
#include "stages.h"
#include "test_hooks.h"

using namespace shewchuk_test;

namespace {

// The window of the library's first test, restated: the test is trusted if
// 2^-600 <= errbound < 2^950.
const double WINDOW_FLOOR = 0x1p-600, WINDOW_CEILING = 0x1p950;
static_assert(shewchuk::detail::guard::orient2d_floor_exponent == -600 &&
                  shewchuk::detail::guard::orient2d_ceiling_exponent == 950,
              "restate the window of src/filter_guard.h above");
// ccwerrboundA (tests/test_constants.cpp checks the library's constants).
const double CCW_ERRBOUND_A = 0x1.8000000000004p-52;

}  // namespace

int main() {
  initialize_implementations();
  const Implementation& reference = reference_implementation();
  const Implementation& library = *find_implementation("library");

  const double magnitudes[] = {
      0.0,
      std::numeric_limits<double>::denorm_min(),
      DBL_MIN,
      0x1p-600,
      0x1p-300,
      0x1p-142,
      1.0,
      1.0 + DBL_EPSILON,
      3.0,
      0x1p201,
      0x1p600,
      DBL_MAX,
  };
  const int num_magnitudes = sizeof magnitudes / sizeof magnitudes[0];
  const int num_values = 2 * num_magnitudes;
  auto value_at = [&](int i) {
    return i < num_magnitudes ? magnitudes[i] : -magnitudes[i - num_magnitudes];
  };

  long cases = 0, wrong_signs = 0, value_differences = 0, stage_differences = 0;
  long path_differences = 0, reference_wrong = 0;
  long in_window_by_stage[NUM_LIBRARY_STAGES] = {}, outside_by_stage[NUM_LIBRARY_STAGES] = {};
  long zero_products = 0;
  for (int i = 0; i < num_values; i++) {
    for (int j = 0; j < num_values; j++) {
      for (int k = 0; k < num_values; k++) {
        for (int l = 0; l < num_values; l++) {
          const double c[6] = {value_at(i), value_at(j), value_at(k), value_at(l), 0.0, 0.0};
          cases++;
          const int exact = oracle_sign(ORIENT2D, c);
          const double expected = reference.call(ORIENT2D, c);
          const double value = library.call(ORIENT2D, c);
          if (!std::isfinite(expected) || sign_of(expected) != exact) reference_wrong++;
          if (!std::isfinite(value) || sign_of(value) != exact) {
            if (wrong_signs++ < 5) {
              std::printf("wrong sign: a=(%a, %a) b=(%a, %a): library %a, exact sign %d\n", c[0],
                          c[1], c[2], c[3], value, exact);
            }
          }
          int reference_stage = -1, library_stage = -1;
          staged_reference_call(ORIENT2D, c, &reference_stage);
          shewchuk_hooked_last_path = -1;
          staged_library_call(ORIENT2D, c, &library_stage);
          const int path = shewchuk_hooked_last_path;
          if (library_stage < 0 || library_stage >= NUM_LIBRARY_STAGES) {
            stage_differences++;
            continue;
          }

          // What the first test computes, recomputed.
          const double detleft = c[0] * c[3], detright = c[1] * c[2];
          const double detsum = std::fabs(detleft) + std::fabs(detright);
          const double errbound = CCW_ERRBOUND_A * detsum;
          const bool in_window = errbound >= WINDOW_FLOOR && errbound < WINDOW_CEILING;
          const bool zero_factors =
              (c[0] == 0.0 || c[3] == 0.0) && (c[1] == 0.0 || c[2] == 0.0);
          if (in_window || zero_factors) {
            // As in 1996: the first test decides the same calls, in the same
            // stage, with the same value. (What follows a first test that
            // does not decide is the subject of the corpus tests.)
            const bool first_test = reference_stage == STAGE_A0 || reference_stage == STAGE_A;
            const bool library_first_test = library_stage == STAGE_A0 || library_stage == STAGE_A;
            const bool same = std::memcmp(&expected, &value, sizeof value) == 0;
            if (first_test && !same) {
              if (value_differences++ < 5) {
                std::printf("value differs: a=(%a, %a) b=(%a, %a): reference %a, library %a\n",
                            c[0], c[1], c[2], c[3], expected, value);
              }
            }
            if (first_test ? reference_stage != library_stage : library_first_test) {
              if (stage_differences++ < 5) {
                std::printf("stage differs: a=(%a, %a) b=(%a, %a): reference %d, library %d\n",
                            c[0], c[1], c[2], c[3], reference_stage, library_stage);
              }
            }
            // Two zero products leave the first test by a branch of their own.
            const int expected_path = in_window ? static_cast<int>(SHEWCHUK_PATH_LEAF)
                                                : static_cast<int>(SHEWCHUK_PATH_ZERO_PRODUCTS);
            if (path != expected_path) path_differences++;
            if (in_window) {
              in_window_by_stage[library_stage]++;
            } else {
              zero_products++;
            }
          } else {
            // Outside the window the first test decides nothing by itself.
            if (path == SHEWCHUK_PATH_LEAF || path == SHEWCHUK_PATH_ZERO_PRODUCTS) {
              path_differences++;
            }
            outside_by_stage[library_stage]++;
          }
        }
      }
    }
  }

  std::printf("%ld cases; the 1996 code is wrong on %ld of them\n", cases, reference_wrong);
  std::printf("two zero products with a zero factor each: %ld\ninside the window, by stage: ",
              zero_products);
  for (int s = 0; s < NUM_LIBRARY_STAGES; s++) {
    std::printf(" %s %ld", STAGE_NAME[s], in_window_by_stage[s]);
  }
  std::printf("\noutside the window, by stage:");
  for (int s = 0; s < NUM_LIBRARY_STAGES; s++) {
    std::printf(" %s %ld", STAGE_NAME[s], outside_by_stage[s]);
  }
  std::printf("\n");
  CHECK(wrong_signs == 0);
  CHECK(value_differences == 0);
  CHECK(stage_differences == 0);
  CHECK(path_differences == 0);
  // The test is only meaningful if the sign test and the filter both decide
  // many of the cases inside the window, some reach orient2dadapt(), many
  // are outside the window, and the 1996 code does fail on those.
  CHECK(zero_products > 1000);
  CHECK(in_window_by_stage[STAGE_A0] > 1000);
  CHECK(in_window_by_stage[STAGE_A] > 1000);
  CHECK(in_window_by_stage[STAGE_B] + in_window_by_stage[STAGE_BX] + in_window_by_stage[STAGE_C] +
            in_window_by_stage[STAGE_I] >
        100);
  CHECK(outside_by_stage[STAGE_I] > 1000);
  CHECK(reference_wrong > 1000);
  return finish("orient2d: the first test equals the 1996 sign test and filter inside its window");
}
