#include "oracle_gmp.h"

#if SHEWCHUK_HAVE_GMP

#include <gmp.h>

#include "determinants.h"

namespace shewchuk_test {
namespace {

// Value-semantic wrapper around mpq_t (the C API, so that no particular C++
// standard library is required of the GMP build).
class Rational {
 public:
  Rational() { mpq_init(value_); }
  explicit Rational(int n) {
    mpq_init(value_);
    mpq_set_si(value_, n, 1);
  }
  explicit Rational(double x) {
    mpq_init(value_);
    mpq_set_d(value_, x);
  }
  Rational(const Rational& other) {
    mpq_init(value_);
    mpq_set(value_, other.value_);
  }
  Rational& operator=(const Rational& other) {
    mpq_set(value_, other.value_);
    return *this;
  }
  ~Rational() { mpq_clear(value_); }

  int sign() const { return mpq_sgn(value_); }

  friend Rational operator+(const Rational& a, const Rational& b) {
    Rational r;
    mpq_add(r.value_, a.value_, b.value_);
    return r;
  }
  friend Rational operator-(const Rational& a, const Rational& b) {
    Rational r;
    mpq_sub(r.value_, a.value_, b.value_);
    return r;
  }
  friend Rational operator*(const Rational& a, const Rational& b) {
    Rational r;
    mpq_mul(r.value_, a.value_, b.value_);
    return r;
  }

 private:
  mpq_t value_;
};

}  // namespace

bool gmp_oracle_available() { return true; }

int gmp_oracle_sign(Pred pred, const double* coords) {
  Rational p[MAX_COORDS];
  for (int i = 0; i < NUM_COORDS[pred]; i++) p[i] = Rational(coords[i]);
  return determinant_homogeneous(pred, p).sign();
}

}  // namespace shewchuk_test

#else

#include <cstdlib>

namespace shewchuk_test {
bool gmp_oracle_available() { return false; }
int gmp_oracle_sign(Pred, const double*) { std::abort(); }
}  // namespace shewchuk_test

#endif
