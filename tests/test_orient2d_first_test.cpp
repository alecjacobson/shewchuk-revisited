// The merged first test of the library's orient2d on edge cases.
//
// The 1996 orient2d first looks at the signs of its two products (stage A0)
// and only then applies the error-bound filter (stage A). The library does
// both with one comparison (see shewchuk_orient2d in src/predicates.cpp and
// docs/exactness.md). The corpus tests already require identical values and
// identical stages on in-domain inputs. This test adds the inputs the
// argument for the merged test has to survive and the corpus does not
// contain: products that are zero of either sign, equal, opposite, tiny,
// subnormal, huge, or overflowed to infinity.
//
// With pc at the origin the two products are ax*by and ay*bx, so every
// combination of special values is reached directly. For every combination
// the library must return the reference's value bit for bit, and its
// test-only build must report the stage the reference decided the call in. A
// call the reference decides by the sign test and the library would hand to
// orient2dadapt() fails the stage check, whatever value comes back.
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "check.h"
#include "implementations.h"
#include "stages.h"

using namespace shewchuk_test;

int main() {
  initialize_implementations();
  const Implementation& reference = reference_implementation();
  const Implementation& library = *find_implementation("library");

  const double magnitudes[] = {
      0.0,
      std::numeric_limits<double>::denorm_min(),
      DBL_MIN,
      0x1p-600,
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

  long cases = 0, value_differences = 0, stage_differences = 0, nan_products = 0;
  long by_stage[NUM_STAGES] = {};
  for (int i = 0; i < num_values; i++) {
    for (int j = 0; j < num_values; j++) {
      for (int k = 0; k < num_values; k++) {
        for (int l = 0; l < num_values; l++) {
          const double c[6] = {value_at(i), value_at(j), value_at(k), value_at(l), 0.0, 0.0};
          // Finite coordinates cannot make a product NaN here; the merged
          // test is not claimed for a NaN product (docs/exactness.md).
          if (std::isnan(c[0] * c[3]) || std::isnan(c[1] * c[2])) {
            nan_products++;
            continue;
          }
          cases++;
          const double expected = reference.call(ORIENT2D, c);
          const double value = library.call(ORIENT2D, c);
          // inf - inf: both hand the call to orient2dadapt(), which returns
          // NaN; the bits of a NaN are not compared.
          const bool same = (std::isnan(expected) && std::isnan(value)) ||
                            std::memcmp(&expected, &value, sizeof value) == 0;
          if (!same) {
            if (value_differences++ < 5) {
              std::printf("value differs: a=(%a, %a) b=(%a, %a): reference %a, library %a\n", c[0],
                          c[1], c[2], c[3], expected, value);
            }
          }
          int reference_stage = -1, library_stage = -1;
          staged_reference_call(ORIENT2D, c, &reference_stage);
          staged_library_call(ORIENT2D, c, &library_stage);
          if (reference_stage != library_stage) {
            if (stage_differences++ < 5) {
              std::printf("stage differs: a=(%a, %a) b=(%a, %a): reference %d, library %d\n", c[0],
                          c[1], c[2], c[3], reference_stage, library_stage);
            }
          }
          if (reference_stage >= 0 && reference_stage < NUM_STAGES) by_stage[reference_stage]++;
        }
      }
    }
  }

  std::printf("%ld cases:", cases);
  for (int s = 0; s < NUM_STAGES; s++) std::printf(" %s %ld", STAGE_NAME[s], by_stage[s]);
  std::printf("\n");
  CHECK(nan_products == 0);
  CHECK(value_differences == 0);
  CHECK(stage_differences == 0);
  // The test is only meaningful if the sign test and the filter both decide
  // many of the cases, and some reach orient2dadapt().
  CHECK(by_stage[STAGE_A0] > 1000);
  CHECK(by_stage[STAGE_A] > 1000);
  CHECK(cases - by_stage[STAGE_A0] - by_stage[STAGE_A] > 100);
  return finish("orient2d: the merged first test equals the 1996 sign test and filter");
}
