// The library's compile-time constants equal what exactinit() computes.
//
// src/constants.h already checks every constant at compile time against its
// closed formula (including it here re-runs those static_asserts with this
// translation unit's compiler). This test adds the run-time side:
//   1. each constant has the bit pattern of the documented hex literal;
//   2. each constant equals the global that the pristine predicates.c
//      computes in exactinit();
//   3. each constant equals its formula evaluated at run time, where the
//      compiler cannot fold it.
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "check.h"
#include "constants.h"
#include "implementations.h"

extern "C" {
// The globals of the pristine build (baseline/shewchuk_ref_rename.h).
extern double shewchuk_ref_splitter, shewchuk_ref_epsilon, shewchuk_ref_resulterrbound;
extern double shewchuk_ref_ccwerrboundA, shewchuk_ref_ccwerrboundB, shewchuk_ref_ccwerrboundC;
extern double shewchuk_ref_o3derrboundA, shewchuk_ref_o3derrboundB, shewchuk_ref_o3derrboundC;
extern double shewchuk_ref_iccerrboundA, shewchuk_ref_iccerrboundB, shewchuk_ref_iccerrboundC;
extern double shewchuk_ref_isperrboundA, shewchuk_ref_isperrboundB, shewchuk_ref_isperrboundC;
}

using namespace shewchuk_test;
namespace k = shewchuk::detail;

namespace {

std::uint64_t bits_of(double x) {
  std::uint64_t bits;
  std::memcpy(&bits, &x, sizeof bits);
  return bits;
}

// (a + b * eps) * eps, or that times eps again, computed at run time.
double formula(double a, double b, bool squared) {
  volatile double eps = 0.5;
  for (int i = 1; i < 53; i++) eps = eps * 0.5;
  volatile double result = (a + b * eps) * eps;
  if (squared) result = result * eps;
  return result;
}

struct Row {
  const char* name;
  double constant;       // src/constants.h
  std::uint64_t bits;    // the documented value, as IEEE 754 bits
  const double* global;  // the pristine build's global
  double formula;
};

}  // namespace

int main() {
  initialize_implementations();  // runs the reference's exactinit()

  // The loop of exactinit(), at run time.
  volatile double runtime_epsilon = 1.0, runtime_splitter = 1.0, check = 1.0, lastcheck;
  bool every_other = true;
  do {
    lastcheck = check;
    runtime_epsilon = runtime_epsilon * 0.5;
    if (every_other) runtime_splitter = runtime_splitter * 2.0;
    every_other = !every_other;
    check = 1.0 + runtime_epsilon;
  } while ((check != 1.0) && (check != lastcheck));
  runtime_splitter = runtime_splitter + 1.0;

  const Row rows[] = {
      {"epsilon", k::epsilon, 0x3CA0000000000000u, &shewchuk_ref_epsilon, runtime_epsilon},
      {"splitter", k::splitter, 0x41A0000002000000u, &shewchuk_ref_splitter, runtime_splitter},
      {"resulterrbound", k::resulterrbound, 0x3CB8000000000002u, &shewchuk_ref_resulterrbound,
       formula(3.0, 8.0, false)},
      {"ccwerrboundA", k::ccwerrboundA, 0x3CB8000000000004u, &shewchuk_ref_ccwerrboundA,
       formula(3.0, 16.0, false)},
      {"ccwerrboundB", k::ccwerrboundB, 0x3CB0000000000003u, &shewchuk_ref_ccwerrboundB,
       formula(2.0, 12.0, false)},
      {"ccwerrboundC", k::ccwerrboundC, 0x3982000000000004u, &shewchuk_ref_ccwerrboundC,
       formula(9.0, 64.0, true)},
      {"o3derrboundA", k::o3derrboundA, 0x3CCC000000000007u, &shewchuk_ref_o3derrboundA,
       formula(7.0, 56.0, false)},
      {"o3derrboundB", k::o3derrboundB, 0x3CB8000000000007u, &shewchuk_ref_o3derrboundB,
       formula(3.0, 28.0, false)},
      {"o3derrboundC", k::o3derrboundC, 0x399A000000000009u, &shewchuk_ref_o3derrboundC,
       formula(26.0, 288.0, true)},
      {"iccerrboundA", k::iccerrboundA, 0x3CD4000000000006u, &shewchuk_ref_iccerrboundA,
       formula(10.0, 96.0, false)},
      {"iccerrboundB", k::iccerrboundB, 0x3CC0000000000006u, &shewchuk_ref_iccerrboundB,
       formula(4.0, 48.0, false)},
      {"iccerrboundC", k::iccerrboundC, 0x39A6000000000009u, &shewchuk_ref_iccerrboundC,
       formula(44.0, 576.0, true)},
      {"isperrboundA", k::isperrboundA, 0x3CE0000000000007u, &shewchuk_ref_isperrboundA,
       formula(16.0, 224.0, false)},
      {"isperrboundB", k::isperrboundB, 0x3CC4000000000009u, &shewchuk_ref_isperrboundB,
       formula(5.0, 72.0, false)},
      {"isperrboundC", k::isperrboundC, 0x39B1C0000000000Bu, &shewchuk_ref_isperrboundC,
       formula(71.0, 1408.0, true)},
  };

  std::printf("%-15s %-24s %-18s %s\n", "constant", "value", "bits", "exactinit()");
  for (const Row& row : rows) {
    std::printf("%-15s %-24a %016" PRIx64 "   %a\n", row.name, row.constant, bits_of(row.constant),
                *row.global);
    CHECK(bits_of(row.constant) == row.bits);
    CHECK(bits_of(row.constant) == bits_of(*row.global));
    CHECK(bits_of(row.constant) == bits_of(row.formula));
  }
  return finish("library constants");
}
