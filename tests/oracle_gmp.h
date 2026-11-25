// Second, independent exact oracle built on GMP rationals (optional).
#pragma once

#include "predicate_ids.h"

namespace shewchuk_test {

// True if the suite was built with GMP.
bool gmp_oracle_available();

// Exact sign (-1, 0, +1) of the predicate, computed differently from
// oracle_sign in every respect: the doubles are converted by GMP (mpq_set_d
// is exact), there is no common scaling, the arithmetic is GMP's, and the
// determinant is the homogeneous one. Must not be called when
// gmp_oracle_available() is false.
int gmp_oracle_sign(Pred pred, const double* coords);

}  // namespace shewchuk_test
