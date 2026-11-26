// The "overhead" pseudo-implementation: functions with the predicates'
// signatures that do almost nothing. Timing it gives the cost of the harness
// loop itself (loading the arguments, the indirect call and return, the sign
// accumulation), which is included in every other number.
//
// It lives in its own translation unit so that, like a real implementation,
// it is reached through a call that cannot be inlined.
#include "overhead.h"

namespace shewchuk_bench {
namespace {

double overhead_orient2d(const double* a, const double* b, const double*) { return a[0] - b[1]; }
double overhead_orient3d(const double* a, const double* b, const double*, const double*) {
  return a[0] - b[1];
}
double overhead_insphere(const double* a, const double* b, const double*, const double*,
                         const double*) {
  return a[0] - b[1];
}
const char* always_available() { return nullptr; }

}  // namespace

const shewchuk_test::Implementation& overhead_implementation() {
  static const shewchuk_test::Implementation implementation = {
      "overhead", "harness loop and call overhead only: not a predicate (benchmark only)",
      overhead_orient2d, overhead_orient3d, overhead_orient3d, overhead_insphere,
      always_available};
  return implementation;
}

}  // namespace shewchuk_bench
