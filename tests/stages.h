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
  STAGE_D = 5,   // fully exact (expansion arithmetic)
  // Not a stage of the 1996 code, so the instrumented reference never records
  // it: the library's exact integer stage (src/integer_stage.h).
  STAGE_I = 6,
};
// The stages of the 1996 code, and those plus the library's integer stage.
inline constexpr int NUM_STAGES = 6;
inline constexpr int NUM_LIBRARY_STAGES = 7;
inline constexpr const char* STAGE_NAME[NUM_LIBRARY_STAGES] = {"A0", "A", "B", "Bx", "C", "D", "I"};

}  // namespace shewchuk_test
