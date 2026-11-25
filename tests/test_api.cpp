// The public interface: prefixed C functions, the shewchuk:: wrappers and the
// opt-in 1996 names. Includes only the installed headers.
#include "shewchuk/predicates.h"
#include "shewchuk/predicates_1996.h"

#include <cstdio>

#include "check.h"

namespace {

int sign(double x) { return (x > 0.0) - (x < 0.0); }

}  // namespace

int main() {
  // Counterclockwise triangle; d inside its circumcircle.
  const double a[2] = {0.0, 0.0}, b[2] = {1.0, 0.0}, c[2] = {0.0, 1.0}, d[2] = {0.25, 0.25};
  // Tetrahedron with orient3d(p, q, r, s) > 0; t inside its circumsphere.
  const double p[3] = {0.0, 0.0, 0.0}, q[3] = {1.0, 0.0, 0.0}, r[3] = {0.0, 1.0, 0.0};
  const double s[3] = {0.0, 0.0, -1.0}, t[3] = {0.25, 0.25, -0.25};
  // Three exactly collinear points that plain floating point gets wrong:
  // the naive determinant evaluates to a nonzero value.
  const double u[2] = {0x1.0000000000011p-1, 0x1.0000000000011p-1}, v[2] = {12.0, 12.0},
               w[2] = {24.0, 24.0};

  CHECK(sign(shewchuk_orient2d(a, b, c)) == 1);
  CHECK(sign(shewchuk_orient2d(a, c, b)) == -1);
  CHECK(sign(shewchuk_orient2d(u, v, w)) == 0);
  CHECK(sign(shewchuk_orient3d(p, q, r, s)) == 1);
  CHECK(sign(shewchuk_orient3d(p, q, r, p)) == 0);
  CHECK(sign(shewchuk_incircle(a, b, c, d)) == 1);
  CHECK(sign(shewchuk_incircle(a, b, c, a)) == 0);
  CHECK(sign(shewchuk_insphere(p, q, r, s, t)) == 1);
  CHECK(sign(shewchuk_insphere(p, q, r, s, p)) == 0);

  // The namespace wrappers are the same functions.
  CHECK(shewchuk::orient2d(a, b, c) == shewchuk_orient2d(a, b, c));
  CHECK(shewchuk::orient3d(p, q, r, s) == shewchuk_orient3d(p, q, r, s));
  CHECK(shewchuk::incircle(a, b, c, d) == shewchuk_incircle(a, b, c, d));
  CHECK(shewchuk::insphere(p, q, r, s, t) == shewchuk_insphere(p, q, r, s, t));

  // The 1996 names, called the 1996 way: exactinit() first (a no-op here),
  // non-const points.
  double pa[2] = {0.0, 0.0}, pb[2] = {1.0, 0.0}, pc[2] = {0.0, 1.0}, pd[2] = {0.25, 0.25};
  double qa[3] = {0.0, 0.0, 0.0}, qb[3] = {1.0, 0.0, 0.0}, qc[3] = {0.0, 1.0, 0.0};
  double qd[3] = {0.0, 0.0, -1.0}, qe[3] = {0.25, 0.25, -0.25};
  exactinit();
  CHECK(orient2d(pa, pb, pc) == shewchuk_orient2d(a, b, c));
  CHECK(orient3d(qa, qb, qc, qd) == shewchuk_orient3d(p, q, r, s));
  CHECK(incircle(pa, pb, pc, pd) == shewchuk_incircle(a, b, c, d));
  CHECK(insphere(qa, qb, qc, qd, qe) == shewchuk_insphere(p, q, r, s, t));

  return shewchuk_test::finish("public interface");
}
