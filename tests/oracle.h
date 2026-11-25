// Exact sign oracle for orient2d, orient3d, incircle and insphere.
//
// Every finite double is an integer times a power of two. The oracle scales
// all coordinates of a case by one common power of two (which cannot change
// the sign of a homogeneous polynomial), converts them to big integers and
// evaluates the determinant exactly. No floating-point arithmetic is involved
// after the conversion, and no code is shared with predicates.c.
//
// Inputs must be finite; any finite doubles are accepted, including
// subnormals and values outside the domain of the 1996 predicates.
#pragma once

#include "predicate_ids.h"

namespace shewchuk_test {

// Exact sign (-1, 0, +1) of the predicate `pred` on the flat coordinate array
// `coords` (layout in predicate_ids.h), with the sign convention of the
// reference implementation. This is the oracle every test uses.
int oracle_sign(Pred pred, const double* coords);

// The same sign from the homogeneous formulation (still big integers). Used
// to cross-check oracle_sign on every platform, with or without GMP.
int oracle_sign_homogeneous(Pred pred, const double* coords);

}  // namespace shewchuk_test
