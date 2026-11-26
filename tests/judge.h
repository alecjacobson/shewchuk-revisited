// Judges one returned value against the exact oracle, taking into account
// what the implementation declares about itself in the table
// (implementations.h): symbolic perturbation, an in-circle test that ignores
// the orientation of the triangle, a precondition on the orientation.
//
// For every implementation of this project none of those is declared and the
// verdict is simply "the sign is the exact sign".
#pragma once

#include <cmath>

#include "implementations.h"
#include "oracle.h"
#include "predicate_ids.h"

namespace shewchuk_test {

enum class Verdict {
  RIGHT,
  // The exact sign is zero and the implementation, as declared, broke the tie
  // with a symbolic perturbation.
  PERTURBED_ZERO,
  // Nothing to compare with: the implementation ignores the orientation of
  // the other points and they are collinear or coplanar.
  UNDEFINED,
  // The input violates the implementation's declared precondition.
  OUTSIDE_PRECONDITION,
  WRONG,
};

struct Judgement {
  Verdict verdict;
  // The sign the implementation was asked for (meaningless for UNDEFINED and
  // OUTSIDE_PRECONDITION).
  int asked;
};

// `exact_sign` is oracle_sign(pred, c).
inline Judgement judge(const Implementation& implementation, Pred pred, const double* c,
                       double value, int exact_sign) {
  int asked = exact_sign;
  if (implementation.unoriented[pred] || implementation.required_base_orientation[pred] != 0) {
    const int base = oracle_sign(pred == INCIRCLE ? ORIENT2D : ORIENT3D, c);
    if (implementation.unoriented[pred]) {
      if (base == 0) return {Verdict::UNDEFINED, 0};
      asked = exact_sign * base;
    } else if (base != implementation.required_base_orientation[pred]) {
      return {Verdict::OUTSIDE_PRECONDITION, 0};
    }
  }
  // A NaN has no sign: never let it pass as "zero".
  if (!std::isfinite(value)) return {Verdict::WRONG, asked};
  if (sign_of(value) == asked) return {Verdict::RIGHT, asked};
  if (asked == 0 && implementation.perturbs_zero[pred]) return {Verdict::PERTURBED_ZERO, asked};
  return {Verdict::WRONG, asked};
}

}  // namespace shewchuk_test
