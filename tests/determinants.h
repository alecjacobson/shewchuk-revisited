// The four predicate determinants over any exact number type Z
// (needs Z(int), +, -, *).
//
// Two independent formulations:
//   translated   Shewchuk's n x n form after subtracting the last point.
//   homogeneous  the (n+1) x (n+1) matrix [coordinates, (lift), 1], expanded by
//                cofactors along the first row; no translation at all.
// They are equal as polynomials, so an exact evaluation of either gives the
// sign the predicates must return.
#pragma once

#include <vector>

#include "predicate_ids.h"

namespace shewchuk_test {

template <class Z>
Z orient2d_translated(const Z* p) {
  const Z acx = p[0] - p[4], acy = p[1] - p[5];
  const Z bcx = p[2] - p[4], bcy = p[3] - p[5];
  return acx * bcy - acy * bcx;
}

template <class Z>
Z orient3d_translated(const Z* p) {
  const Z adx = p[0] - p[9], ady = p[1] - p[10], adz = p[2] - p[11];
  const Z bdx = p[3] - p[9], bdy = p[4] - p[10], bdz = p[5] - p[11];
  const Z cdx = p[6] - p[9], cdy = p[7] - p[10], cdz = p[8] - p[11];
  return adx * (bdy * cdz - bdz * cdy) + bdx * (cdy * adz - cdz * ady) +
         cdx * (ady * bdz - adz * bdy);
}

template <class Z>
Z incircle_translated(const Z* p) {
  const Z adx = p[0] - p[6], ady = p[1] - p[7];
  const Z bdx = p[2] - p[6], bdy = p[3] - p[7];
  const Z cdx = p[4] - p[6], cdy = p[5] - p[7];
  const Z alift = adx * adx + ady * ady;
  const Z blift = bdx * bdx + bdy * bdy;
  const Z clift = cdx * cdx + cdy * cdy;
  return alift * (bdx * cdy - bdy * cdx) + blift * (cdx * ady - cdy * adx) +
         clift * (adx * bdy - ady * bdx);
}

template <class Z>
Z insphere_translated(const Z* p) {
  // Rows (x, y, z, x^2 + y^2 + z^2) of a, b, c, d relative to e.
  Z row[4][4];
  for (int i = 0; i < 4; i++) {
    const Z x = p[3 * i] - p[12], y = p[3 * i + 1] - p[13], z = p[3 * i + 2] - p[14];
    row[i][0] = x;
    row[i][1] = y;
    row[i][2] = z;
    row[i][3] = x * x + y * y + z * z;
  }
  // 4 x 4 determinant by complementary 2 x 2 minors of rows (0, 1) and (2, 3).
  auto minor = [&](int i, int j, int a, int b) -> Z {
    return row[i][a] * row[j][b] - row[i][b] * row[j][a];
  };
  return minor(0, 1, 0, 1) * minor(2, 3, 2, 3) - minor(0, 1, 0, 2) * minor(2, 3, 1, 3) +
         minor(0, 1, 0, 3) * minor(2, 3, 1, 2) + minor(0, 1, 1, 2) * minor(2, 3, 0, 3) -
         minor(0, 1, 1, 3) * minor(2, 3, 0, 2) + minor(0, 1, 2, 3) * minor(2, 3, 0, 1);
}

template <class Z>
Z determinant_translated(Pred pred, const Z* p) {
  switch (pred) {
    case ORIENT2D: return orient2d_translated(p);
    case ORIENT3D: return orient3d_translated(p);
    case INCIRCLE: return incircle_translated(p);
    default: return insphere_translated(p);
  }
}

// Determinant of a row-major n x n matrix by cofactor expansion (n <= 5).
template <class Z>
Z determinant_by_cofactors(const Z* a, int n) {
  if (n == 1) return a[0];
  if (n == 2) return a[0] * a[3] - a[1] * a[2];
  Z det(0);
  std::vector<Z> sub((n - 1) * (n - 1));
  for (int j = 0; j < n; j++) {
    for (int r = 1; r < n; r++) {
      int column = 0;
      for (int k = 0; k < n; k++) {
        if (k != j) sub[(r - 1) * (n - 1) + column++] = a[r * n + k];
      }
    }
    const Z term = a[j] * determinant_by_cofactors(sub.data(), n - 1);
    det = (j % 2) ? det - term : det + term;
  }
  return det;
}

// The homogeneous determinant. It equals the translated one exactly:
// subtracting the last row from the others leaves a single 1 in the ones
// column, at the bottom right corner (cofactor sign +), and the remaining
// minor has rows (p_i - p_last, lift_i - lift_last). For the lifted
// predicates lift_i - lift_last = |p_i - p_last|^2 + 2 p_last . (p_i - p_last),
// and the second term is a combination of the other columns.
//
// The ones column may stay 1 when the coordinates are scaled by a positive
// factor: the determinant is linear in that column and homogeneous in the
// others, so only a positive factor is lost.
template <class Z>
Z determinant_homogeneous(Pred pred, const Z* p) {
  const int n = NUM_POINTS[pred], dim = DIMENSION[pred];
  const bool lifted = pred == INCIRCLE || pred == INSPHERE;
  std::vector<Z> a(n * n);
  for (int i = 0; i < n; i++) {
    Z lift(0);
    for (int k = 0; k < dim; k++) {
      a[i * n + k] = p[i * dim + k];
      if (lifted) lift = lift + p[i * dim + k] * p[i * dim + k];
    }
    if (lifted) a[i * n + dim] = lift;
    a[i * n + n - 1] = Z(1);
  }
  return determinant_by_cofactors(a.data(), n);
}

}  // namespace shewchuk_test
