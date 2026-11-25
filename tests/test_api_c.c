/* The public headers are usable from C, including the opt-in 1996 names. */
#include "shewchuk/predicates_1996.h"

#include <stdio.h>

int main(void) {
  double a[2] = {0.0, 0.0}, b[2] = {1.0, 0.0}, c[2] = {0.0, 1.0}, d[2] = {0.25, 0.25};
  double p[3] = {0.0, 0.0, 0.0}, q[3] = {1.0, 0.0, 0.0}, r[3] = {0.0, 1.0, 0.0};
  double s[3] = {0.0, 0.0, -1.0}, t[3] = {0.25, 0.25, -0.25};
  int failures = 0;

  exactinit();
  failures += !(orient2d(a, b, c) > 0.0);
  failures += !(orient2d(a, c, b) < 0.0);
  failures += !(orient2d(a, b, a) == 0.0);
  failures += !(orient3d(p, q, r, s) > 0.0);
  failures += !(incircle(a, b, c, d) > 0.0);
  failures += !(insphere(p, q, r, s, t) > 0.0);
  failures += !(shewchuk_orient2d(a, b, c) == orient2d(a, b, c));

  if (failures) {
    printf("FAIL: public interface from C (%d failed checks)\n", failures);
    return 1;
  }
  printf("PASS: public interface from C\n");
  return 0;
}
