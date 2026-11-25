// Stage numbers recorded by the instrumented reference build
// (keep in sync with tools/instrument_stages.cpp).
#pragma once

namespace shewchuk_test {

enum Stage {
  STAGE_A0 = 0,  // orient2d only: decided by the signs of the two products
  STAGE_A = 1,   // first floating-point filter
  STAGE_B = 2,   // second filter
  STAGE_BX = 3,  // exit because all difference tails are zero
  STAGE_C = 4,   // third filter
  STAGE_D = 5,   // fully exact
};
inline constexpr int NUM_STAGES = 6;
inline constexpr const char* STAGE_NAME[NUM_STAGES] = {"A0", "A", "B", "Bx", "C", "D"};

}  // namespace shewchuk_test
