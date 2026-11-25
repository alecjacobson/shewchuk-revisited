// Unit tests of the exact sign oracle on hand-computed cases.
//
// The oracle judges everything else, so it is tested on its own first: the
// big-integer arithmetic against values worked out by hand, the four
// predicates against configurations whose sign is known from geometry, the two
// determinant formulations against each other on the whole corpus, and the
// sign convention against the reference implementation.
#include <cmath>
#include <cstdint>
#include <vector>

#include "bigint.h"
#include "check.h"
#include "corpus.h"
#include "generators.h"
#include "implementations.h"
#include "oracle.h"

using namespace shewchuk_test;

namespace {

using Limbs = std::vector<std::uint32_t>;

BigInt pow2(unsigned k) { return BigInt(1).shifted_left(k); }

void test_bigint() {
  // Construction.
  CHECK(BigInt().sign() == 0 && BigInt().limbs().empty());
  CHECK(BigInt(0).sign() == 0 && BigInt(0).limbs().empty());
  CHECK(BigInt(-5).sign() == -1 && BigInt(-5).limbs() == Limbs{5});
  CHECK(BigInt(0x123456789ALL).limbs() == (Limbs{0x3456789A, 0x12}));
  CHECK(BigInt(INT64_MIN).sign() == -1 && BigInt(INT64_MIN).limbs() == (Limbs{0, 0x80000000}));
  CHECK(BigInt(INT64_MAX).limbs() == (Limbs{0xFFFFFFFF, 0x7FFFFFFF}));

  // Signs of sums and products.
  CHECK(BigInt(5) + BigInt(-7) == BigInt(-2));
  CHECK(BigInt(-7) + BigInt(5) == BigInt(-2));
  CHECK(BigInt(7) + BigInt(-5) == BigInt(2));
  CHECK(BigInt(-7) - BigInt(-7) == BigInt(0));
  CHECK((BigInt(-7) - BigInt(-7)).limbs().empty());
  CHECK(BigInt(-3) * BigInt(4) == BigInt(-12));
  CHECK(BigInt(-3) * BigInt(-4) == BigInt(12));
  CHECK(BigInt(3) * BigInt(0) == BigInt(0));
  CHECK(-BigInt(9) == BigInt(-9));

  // Carries: (2^32 - 1) + 1 = 2^32.
  CHECK((BigInt(0xFFFFFFFFLL) + BigInt(1)).limbs() == (Limbs{0, 1}));
  // Borrows through several limbs: 2^96 - 1 is three limbs of ones.
  CHECK((pow2(96) - BigInt(1)).limbs() == (Limbs{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}));
  CHECK(pow2(96) - BigInt(1) + BigInt(1) == pow2(96));
  // 1 - 2^96 is the negation.
  CHECK(BigInt(1) - pow2(96) == -(pow2(96) - BigInt(1)));

  // Products: (2^32 - 1)^2 = 2^64 - 2^33 + 1 = 0xFFFFFFFE00000001.
  CHECK((BigInt(0xFFFFFFFFLL) * BigInt(0xFFFFFFFFLL)).limbs() == (Limbs{1, 0xFFFFFFFE}));
  // (2^64 - 1)^2 = 2^128 - 2^65 + 1.
  const BigInt m64 = pow2(64) - BigInt(1);
  CHECK((m64 * m64).limbs() == (Limbs{1, 0, 0xFFFFFFFE, 0xFFFFFFFF}));
  CHECK(m64 * m64 == pow2(128) - pow2(65) + BigInt(1));
  // 10^18 * 10^18 = 10^36 = 0xC097CE7BC90715B34B9F1000000000.
  const BigInt e18(1000000000000000000LL);
  CHECK((e18 * e18).limbs() == (Limbs{0x00000000, 0xB34B9F10, 0x7BC90715, 0x00C097CE}));

  // Shifts: 3 * 2^31 = 0x1_80000000 and 3 * 2^33 = 6 * 2^32.
  CHECK(BigInt(3).shifted_left(31).limbs() == (Limbs{0x80000000, 1}));
  CHECK(BigInt(3).shifted_left(33).limbs() == (Limbs{0, 6}));
  CHECK(BigInt(-3).shifted_left(33) == BigInt(-6) * pow2(32));
  CHECK(BigInt(0).shifted_left(100) == BigInt(0));
  CHECK(pow2(1000) * pow2(1100) == pow2(2100));

  // Ring identities on multi-limb operands.
  const BigInt a = pow2(200) + BigInt(0x1234567890ABCDEFLL), b = pow2(131) - BigInt(987654321);
  CHECK((a + b) * (a - b) == a * a - b * b);
  CHECK(a * (b - a) == a * b - a * a);
  CHECK((a - b) + (b - a) == BigInt(0));
}

int both(Pred pred, const std::vector<double>& c) {
  const int sign = oracle_sign(pred, c.data());
  // The two formulations must always agree.
  CHECK(oracle_sign_homogeneous(pred, c.data()) == sign);
  return sign;
}

// Checks the oracle's sign and, for inputs inside the domain of the 1996
// code, that the reference follows the same convention.
void expect(Pred pred, const std::vector<double>& c, int sign, bool in_reference_domain = true) {
  CHECK(static_cast<int>(c.size()) == NUM_COORDS[pred]);
  CHECK(both(pred, c) == sign);
  if (in_reference_domain) {
    CHECK(sign_of(reference_implementation().call(pred, c.data())) == sign);
  }
}

void test_orient2d() {
  // Counterclockwise is positive.
  expect(ORIENT2D, {0, 0, 1, 0, 0, 1}, +1);
  expect(ORIENT2D, {0, 0, 0, 1, 1, 0}, -1);
  expect(ORIENT2D, {0, 0, 1, 1, 2, 2}, 0);
  expect(ORIENT2D, {3, 3, 3, 3, 7, -2}, 0);  // repeated point
  // c one ulp above / below the line y = x through a and b.
  const double above = std::nextafter(3.0, 4.0), below = std::nextafter(3.0, 2.0);
  expect(ORIENT2D, {1, 1, 2, 2, 3, above}, +1);
  expect(ORIENT2D, {1, 1, 2, 2, 3, below}, -1);
  // Classroom example: a = (0.5 + i u, 0.5 + j u), b = (12, 12), c = (24, 24)
  // with u = 2^-53. The determinant is 12 (i - j) u... times -1: expanding
  // (ax - cx)(by - cy) - (ay - cy)(bx - cx) gives -12 (ax - ay) = 12 (j - i) u,
  // so the sign is sign(j - i). Naive double arithmetic gets many of these wrong.
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 16; j++) {
      const double ax = 0.5 + i * 0x1p-53, ay = 0.5 + j * 0x1p-53;
      expect(ORIENT2D, {ax, ay, 12, 12, 24, 24}, (j > i) - (j < i));
    }
  }
  // Far outside the reference's domain: products overflow or underflow in
  // double arithmetic, but the oracle is exact for all finite doubles.
  expect(ORIENT2D, {0x1p600, 0, 0, 0x1p600, 0, 0}, +1, false);
  expect(ORIENT2D, {0x1p-600, 0, 0, 0x1p-600, 0, 0}, +1, false);
  expect(ORIENT2D, {0, 0, 0x1p-1074, 0, 0, 0x1p-1074}, +1, false);
  expect(ORIENT2D, {0, 0, 0, 0x1p-1074, 0x1p-1074, 0}, -1, false);
  // Mixed magnitudes: (2^300, 1), (0, 0), (2^300, 1 + 2^-52): c is above the
  // ray from b through a, so a, b, c are clockwise.
  expect(ORIENT2D, {0x1p300, 1, 0, 0, 0x1p300, 1 + 0x1p-52}, -1, false);
  expect(ORIENT2D, {-0.0, 0.0, 1, -0.0, 0.0, 1}, +1);  // negative zeros
}

void test_orient3d() {
  // a, b, c counterclockwise seen from +z. d above (+z) is negative, below is
  // positive: with ad = (0,0,-1), bd = (1,0,-1), cd = (0,1,-1) the determinant
  // is 1 * (1 * (-1) - (-1) * 0) = -1.
  expect(ORIENT3D, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1}, -1);
  expect(ORIENT3D, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, -1}, +1);
  expect(ORIENT3D, {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0}, 0);
  expect(ORIENT3D, {0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1}, +1);  // b and c swapped
  // d a hair above / below the plane z = 0.
  expect(ORIENT3D, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0.5, 0.5, 0x1p-140}, -1);
  expect(ORIENT3D, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0.5, 0.5, -0x1p-140}, +1);
  expect(ORIENT3D, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0.5, 0.5, 0x1p-1074}, -1, false);
  // The plane z = 1 far from the origin in x and y: d one ulp above it.
  const double above = std::nextafter(1.0, 2.0);
  expect(ORIENT3D, {0x1p40, 0, 1, 0x1p40 + 1, 0, 1, 0x1p40, 1, 1, 7, 7, above}, -1);
  expect(ORIENT3D, {0x1p40, 0, 1, 0x1p40 + 1, 0, 1, 0x1p40, 1, 1, 7, 7, 1}, 0);
  expect(ORIENT3D, {0x1p400, 0, 0, 0, 0x1p400, 0, 0, 0, 0x1p400, 0, 0, 0}, +1, false);
}

void test_incircle() {
  // a, b, c counterclockwise on the unit circle; inside is positive.
  expect(INCIRCLE, {1, 0, 0, 1, -1, 0, 0, 0}, +1);
  expect(INCIRCLE, {1, 0, 0, 1, -1, 0, 2, 0}, -1);
  expect(INCIRCLE, {1, 0, 0, 1, -1, 0, 0, -1}, 0);
  expect(INCIRCLE, {-1, 0, 0, 1, 1, 0, 0, 0}, -1);  // clockwise flips the sign
  // d one ulp inside / outside the circle at (0, -1).
  expect(INCIRCLE, {1, 0, 0, 1, -1, 0, 0, -1 + 0x1p-53}, +1);
  expect(INCIRCLE, {1, 0, 0, 1, -1, 0, 0, -1 - 0x1p-52}, -1);
  // (3,4), (-4,3), (-3,-4), (4,-3), (5,0), (0,5) lie on x^2 + y^2 = 25.
  expect(INCIRCLE, {3, 4, -4, 3, -3, -4, 4, -3}, 0);
  expect(INCIRCLE, {5, 0, 0, 5, -3, -4, 4, -3}, 0);
  // The same circle translated by 2^30 in x (all coordinates still exact).
  const double t = 0x1p30;
  expect(INCIRCLE, {t + 3, 4, t - 4, 3, t - 3, -4, t + 4, -3}, 0);
  expect(INCIRCLE, {t + 3, 4, t - 4, 3, t - 3, -4, t + 4, -3 + 0x1p-51}, +1);
  expect(INCIRCLE, {t + 3, 4, t - 4, 3, t - 3, -4, t + 4, -3 - 0x1p-51}, -1);
  expect(INCIRCLE, {0x1p300, 0, 0, 0x1p300, -0x1p300, 0, 0, 0}, +1, false);
}

void test_insphere() {
  // a, b, c, d with orient3d > 0 on the sphere of center (.5, .5, -.5) and
  // squared radius 3/4; inside is positive.
  const std::vector<double> abcd = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, -1};
  auto with_e = [&](double x, double y, double z) {
    std::vector<double> c = abcd;
    c.insert(c.end(), {x, y, z});
    return c;
  };
  CHECK(oracle_sign(ORIENT3D, abcd.data()) == +1);
  expect(INSPHERE, with_e(0.25, 0.25, -0.25), +1);
  expect(INSPHERE, with_e(0.5, 0.5, -0.5), +1);
  expect(INSPHERE, with_e(2, 2, 2), -1);
  // (1, 1, 0) and (1, 1, -1) are on the sphere: (.5)^2 * 3 = 3/4.
  expect(INSPHERE, with_e(1, 1, 0), 0);
  expect(INSPHERE, with_e(1, 1, -1), 0);
  // Moving (1, 1, 0) up leaves the sphere, moving it down enters it.
  expect(INSPHERE, with_e(1, 1, 0x1p-60), -1);
  expect(INSPHERE, with_e(1, 1, -0x1p-60), +1);
  expect(INSPHERE, with_e(1, 1, 0x1p-1074), -1, false);
  // Swapping two of a, b, c, d flips the sign.
  expect(INSPHERE, {1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0.25, 0.25, -0.25}, -1);
  // Signed permutations of (1, 2, 2) and (3, 0, 0) lie on x^2 + y^2 + z^2 = 9.
  expect(INSPHERE, {3, 0, 0, 0, 3, 0, 0, 0, 3, 1, 2, 2, -2, 1, -2}, 0);
}

// The translated and homogeneous big-integer formulations agree on every
// generator, including those that reach the exact stage.
void test_formulations_agree_on_corpus() {
  long cases = 0, disagreements = 0;
  for_each_case(find_tier("quick")->cases_per_generator / 10,
                [&](const Generator& generator, const double* c) {
                  cases++;
                  if (oracle_sign(generator.pred, c) != oracle_sign_homogeneous(generator.pred, c)) {
                    disagreements++;
                  }
                });
  std::printf("translated vs homogeneous big-integer oracle: %ld cases, %ld disagreements\n", cases,
              disagreements);
  CHECK(cases > 10000);
  CHECK(disagreements == 0);
}

}  // namespace

int main() {
  initialize_implementations();
  test_bigint();
  test_orient2d();
  test_orient3d();
  test_incircle();
  test_insphere();
  test_formulations_agree_on_corpus();
  return finish("exact oracle unit tests");
}
