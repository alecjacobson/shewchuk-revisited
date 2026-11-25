#include "oracle.h"

#include <climits>
#include <cmath>
#include <cstdint>

#include "bigint.h"
#include "determinants.h"

namespace shewchuk_test {
namespace {

// Converts the coordinates to integers m_i * 2^(e_i - e_min).
void to_scaled_integers(const double* coords, int count, BigInt* out) {
  std::int64_t mantissa[MAX_COORDS];
  int exponent[MAX_COORDS];
  int min_exponent = INT_MAX;
  for (int i = 0; i < count; i++) {
    if (coords[i] == 0.0) {
      mantissa[i] = 0;
      exponent[i] = INT_MAX;
      continue;
    }
    // x = fraction * 2^e with |fraction| in [0.5, 1); both calls are exact,
    // and fraction * 2^53 is an integer because doubles have 53 bits.
    int e;
    const double fraction = std::frexp(coords[i], &e);
    mantissa[i] = static_cast<std::int64_t>(std::ldexp(fraction, 53));
    exponent[i] = e - 53;
    if (exponent[i] < min_exponent) min_exponent = exponent[i];
  }
  for (int i = 0; i < count; i++) {
    out[i] = mantissa[i] == 0
                 ? BigInt()
                 : BigInt(mantissa[i]).shifted_left(static_cast<unsigned>(exponent[i] - min_exponent));
  }
}

}  // namespace

int oracle_sign(Pred pred, const double* coords) {
  BigInt p[MAX_COORDS];
  to_scaled_integers(coords, NUM_COORDS[pred], p);
  return determinant_translated(pred, p).sign();
}

int oracle_sign_homogeneous(Pred pred, const double* coords) {
  BigInt p[MAX_COORDS];
  to_scaled_integers(coords, NUM_COORDS[pred], p);
  return determinant_homogeneous(pred, p).sign();
}

}  // namespace shewchuk_test
