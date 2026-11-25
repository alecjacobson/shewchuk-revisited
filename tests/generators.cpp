#include "generators.h"

#include <cmath>
#include <cstring>
#include <utility>

#include "implementations.h"
#include "stages.h"

namespace shewchuk_test {

double Rng::with_exponent(int e) {
  const double significand = 1.0 + static_cast<double>(u64() >> 12) * 0x1p-52;
  const bool negative = coin();
  return std::ldexp(negative ? -significand : significand, e);
}

double Rng::with_exponent_short(int e, int bits) {
  const std::uint64_t top = (1ull << (bits - 1)) | (u64() & ((1ull << (bits - 1)) - 1));
  const double significand = std::ldexp(static_cast<double>(top), -(bits - 1));
  const bool negative = coin();
  return std::ldexp(negative ? -significand : significand, e);
}

void Hash64::add_double(double x) {
  std::uint64_t bits;
  std::memcpy(&bits, &x, sizeof bits);
  add(bits);
}

namespace {

// Smallest magnitude of a nonzero generated coordinate (paper domain).
const double MIN_MAGNITUDE = 0x1p-142;

// ---------------------------------------------------------------------------
// Moving a double by a number of representable steps (ulps)
// ---------------------------------------------------------------------------

// The double `steps` representable values away from x (steps may be negative).
double step_ulps_unchecked(double x, std::int64_t steps) {
  std::int64_t bits;
  std::memcpy(&bits, &x, sizeof bits);
  // Sign-magnitude bit pattern -> position on a monotone integer line.
  std::int64_t position = bits < 0 ? INT64_MIN - bits : bits;
  position += steps;
  bits = position < 0 ? INT64_MIN - position : position;
  double y;
  std::memcpy(&y, &bits, sizeof y);
  return y;
}

// Like step_ulps_unchecked, but stays inside the domain: zero steps in units
// of MIN_MAGNITUDE, and a step that would produce a nonzero value below
// MIN_MAGNITUDE (or a non-finite one) is not taken.
double step_ulps(double x, std::int64_t steps) {
  if (steps == 0) return x;
  if (x == 0.0) return static_cast<double>(steps) * MIN_MAGNITUDE;
  const double y = step_ulps_unchecked(x, steps);
  if (!std::isfinite(y) || (y != 0.0 && std::fabs(y) < MIN_MAGNITUDE)) return x;
  return y;
}

// ---------------------------------------------------------------------------
// Generic families (any predicate)
// ---------------------------------------------------------------------------

// Coordinates uniform in [-1, 1). Decided by stage A almost surely.
GeneratorFn uniform(Pred pred) {
  return [=](Rng& r, double* c) {
    for (int i = 0; i < NUM_COORDS[pred]; i++) c[i] = r.symmetric();
  };
}

// Every coordinate gets its own exponent in [-max_exponent, max_exponent].
GeneratorFn wide_exponents(Pred pred, int max_exponent) {
  return [=](Rng& r, double* c) {
    for (int i = 0; i < NUM_COORDS[pred]; i++) {
      const int e = r.range(-max_exponent, max_exponent);
      c[i] = r.with_exponent(e);
    }
  };
}

// Integer coordinates in [-k, k]: many exact zeros, repeated points, and
// exact arithmetic throughout.
GeneratorFn small_integers(Pred pred, int k) {
  return [=](Rng& r, double* c) {
    for (int i = 0; i < NUM_COORDS[pred]; i++) c[i] = static_cast<double>(r.range(-k, k));
  };
}

// Coordinates drawn from a pool of three values: shared coordinates and
// repeated points with very different magnitudes.
GeneratorFn shared_coordinates(Pred pred) {
  return [=](Rng& r, double* c) {
    double pool[3];
    pool[0] = r.symmetric();
    const int e = r.range(-40, 40);
    pool[1] = r.with_exponent(e);
    pool[2] = static_cast<double>(r.range(-2, 2));
    for (int i = 0; i < NUM_COORDS[pred]; i++) c[i] = pool[r.below(3)];
  };
}

// All points within k ulps of one point far from the origin (a grid of
// adjacent doubles). Coordinate differences are exact, products are not.
GeneratorFn ulp_grid(Pred pred, int k) {
  return [=](Rng& r, double* c) {
    const int dim = DIMENSION[pred];
    double origin[3];
    for (int d = 0; d < dim; d++) {
      const int e = r.range(0, 30);
      origin[d] = r.with_exponent(e);
    }
    for (int i = 0; i < NUM_COORDS[pred]; i++) c[i] = step_ulps(origin[i % dim], r.range(-k, k));
  };
}

// ---------------------------------------------------------------------------
// Wrappers that perturb another generator
// ---------------------------------------------------------------------------

// Every coordinate moved by a random number of ulps in [-k, k].
GeneratorFn perturb_all(Pred pred, GeneratorFn base, int k) {
  return [=](Rng& r, double* c) {
    base(r, c);
    for (int i = 0; i < NUM_COORDS[pred]; i++) c[i] = step_ulps(c[i], r.range(-k, k));
  };
}

// One coordinate moved by about 2^j ulps, j uniform in [0, max_log]: sweeps
// the distance from degeneracy over many orders of magnitude.
GeneratorFn perturb_one(Pred pred, GeneratorFn base, int max_log) {
  return [=](Rng& r, double* c) {
    base(r, c);
    const int i = static_cast<int>(r.below(NUM_COORDS[pred]));
    std::int64_t steps = std::int64_t{1} << r.range(0, max_log);
    steps += static_cast<std::int64_t>(r.below(static_cast<std::uint64_t>(steps)));
    c[i] = step_ulps(c[i], r.coin() ? steps : -steps);
  };
}

// Random relabelling of the points (changes which point is subtracted).
GeneratorFn permute_points(Pred pred, GeneratorFn base) {
  return [=](Rng& r, double* c) {
    base(r, c);
    const int n = NUM_POINTS[pred], dim = DIMENSION[pred];
    for (int i = n - 1; i > 0; i--) {
      const int j = static_cast<int>(r.below(i + 1));
      for (int d = 0; d < dim; d++) std::swap(c[i * dim + d], c[j * dim + d]);
    }
  };
}

// ---------------------------------------------------------------------------
// Near degenerate: an exactly degenerate real configuration, rounded
// ---------------------------------------------------------------------------

// c on the line through a and b, up to rounding.
void orient2d_near_collinear(Rng& r, double* c) {
  for (int i = 0; i < 4; i++) c[i] = r.symmetric();
  const double t = 3.0 * r.unit() - 1.0;
  c[4] = c[0] + t * (c[2] - c[0]);
  c[5] = c[1] + t * (c[3] - c[1]);
}

// d in the plane through a, b and c, up to rounding.
void orient3d_near_coplanar(Rng& r, double* c) {
  for (int i = 0; i < 9; i++) c[i] = r.symmetric();
  const double s = 2.0 * r.symmetric();
  const double t = 2.0 * r.symmetric();
  for (int d = 0; d < 3; d++) c[9 + d] = c[d] + s * (c[3 + d] - c[d]) + t * (c[6 + d] - c[d]);
}

// Four points on a common circle, up to rounding. Points come from the
// rational parametrization ((1 - t^2) / (1 + t^2), 2t / (1 + t^2)).
void incircle_near_cocircular(Rng& r, double* c) {
  const double cx = r.symmetric();
  const double cy = r.symmetric();
  const double radius = 0.01 + r.unit();
  for (int i = 0; i < 4; i++) {
    const double t = r.symmetric();
    double x = (1.0 - t * t) / (1.0 + t * t);
    double y = 2.0 * t / (1.0 + t * t);
    if (r.coin()) x = -x;
    if (r.coin()) std::swap(x, y);
    c[2 * i] = cx + radius * x;
    c[2 * i + 1] = cy + radius * y;
  }
}

// Five points on a common sphere, up to rounding (stereographic
// parametrization of the unit sphere).
void insphere_near_cospherical(Rng& r, double* c) {
  double center[3];
  for (int d = 0; d < 3; d++) center[d] = r.symmetric();
  const double radius = 0.01 + r.unit();
  for (int i = 0; i < 5; i++) {
    const double u = r.symmetric();
    const double v = r.symmetric();
    const double denominator = 1.0 + u * u + v * v;
    double p[3] = {2.0 * u / denominator, 2.0 * v / denominator, (u * u + v * v - 1.0) / denominator};
    if (r.coin()) p[2] = -p[2];
    const int rotation = static_cast<int>(r.below(3));
    for (int d = 0; d < 3; d++) c[3 * i + d] = center[d] + radius * p[(d + rotation) % 3];
  }
}

// Kettner, Mehlhorn, Pion, Schirra and Yap, "Classroom examples of robustness
// problems in geometric computations": a = (0.5 + i ulp, 0.5 + j ulp),
// b = (12, 12), c = (24, 24) for 0 <= i, j < 256. Exact sign: sign(j - i).
void orient2d_classroom_grid(Rng& r, double* c) {
  c[0] = 0.5 + static_cast<double>(r.range(0, 255)) * 0x1p-53;
  c[1] = 0.5 + static_cast<double>(r.range(0, 255)) * 0x1p-53;
  c[2] = 12;
  c[3] = 12;
  c[4] = 24;
  c[5] = 24;
}

// The incircle analogue: a, b, c on the unit circle and d on a 256 x 256 grid
// of adjacent doubles around (0, -1).
void incircle_classroom_grid(Rng& r, double* c) {
  c[0] = 1;
  c[1] = 0;
  c[2] = 0;
  c[3] = 1;
  c[4] = -1;
  c[5] = 0;
  c[6] = static_cast<double>(r.range(-128, 127)) * 0x1p-53;
  c[7] = -1.0 + static_cast<double>(r.range(-128, 127)) * 0x1p-53;
}

// ---------------------------------------------------------------------------
// Exactly degenerate, exact coordinate differences (all tails zero)
// ---------------------------------------------------------------------------
// Integer lattice configurations translated by a large integer offset and
// scaled by a power of two. The determinant is exactly zero and every
// difference of coordinates is representable.

const int LATTICE_OFFSET = 1 << 20;

double lattice_coordinate(std::int64_t value, std::int64_t offset, int scale) {
  return std::ldexp(static_cast<double>(value + offset), scale);
}

void orient2d_lattice(Rng& r, double* c) {
  const std::int64_t ox = r.range(-LATTICE_OFFSET, LATTICE_OFFSET);
  const std::int64_t oy = r.range(-LATTICE_OFFSET, LATTICE_OFFSET);
  const int dx = r.range(-9, 9);
  const int dy = r.range(-9, 9);
  const int scale = r.range(-20, 20);
  for (int i = 0; i < 3; i++) {
    const int k = r.range(-50, 50);
    c[2 * i] = lattice_coordinate(k * dx, ox, scale);
    c[2 * i + 1] = lattice_coordinate(k * dy, oy, scale);
  }
}

void orient3d_lattice(Rng& r, double* c) {
  std::int64_t offset[3];
  int u[3], v[3];
  const int scale = r.range(-20, 20);
  for (int d = 0; d < 3; d++) {
    offset[d] = r.range(-LATTICE_OFFSET, LATTICE_OFFSET);
    u[d] = r.range(-9, 9);
    v[d] = r.range(-9, 9);
  }
  for (int i = 0; i < 4; i++) {
    const int a = r.range(-30, 30);
    const int b = r.range(-30, 30);
    for (int d = 0; d < 3; d++) c[3 * i + d] = lattice_coordinate(a * u[d] + b * v[d], offset[d], scale);
  }
}

// The 12 integer points on the circle x^2 + y^2 = 25.
const int CIRCLE_25[12][2] = {{5, 0}, {-5, 0}, {0, 5},  {0, -5}, {3, 4},  {3, -4},
                              {-3, 4}, {-3, -4}, {4, 3}, {4, -3}, {-4, 3}, {-4, -3}};

void incircle_lattice(Rng& r, double* c) {
  const std::int64_t ox = r.range(-LATTICE_OFFSET, LATTICE_OFFSET);
  const std::int64_t oy = r.range(-LATTICE_OFFSET, LATTICE_OFFSET);
  const int scale = r.range(-20, 20);
  const int multiple = r.range(1, 40);
  for (int i = 0; i < 4; i++) {
    const int* point = CIRCLE_25[r.below(12)];
    c[2 * i] = lattice_coordinate(multiple * point[0], ox, scale);
    c[2 * i + 1] = lattice_coordinate(multiple * point[1], oy, scale);
  }
}

// An integer point on the sphere x^2 + y^2 + z^2 = 9: a signed permutation of
// (3, 0, 0) or (1, 2, 2).
void sphere_9_point(Rng& r, int* point) {
  static const int BASE[2][3] = {{3, 0, 0}, {1, 2, 2}};
  const int* base = BASE[r.below(2)];
  const int rotation = static_cast<int>(r.below(3));
  for (int d = 0; d < 3; d++) point[d] = base[(d + rotation) % 3] * (r.coin() ? 1 : -1);
}

void insphere_lattice(Rng& r, double* c) {
  std::int64_t offset[3];
  const int scale = r.range(-20, 20);
  const int multiple = r.range(1, 40);
  for (int d = 0; d < 3; d++) offset[d] = r.range(-LATTICE_OFFSET, LATTICE_OFFSET);
  for (int i = 0; i < 5; i++) {
    int point[3];
    sphere_9_point(r, point);
    for (int d = 0; d < 3; d++) c[3 * i + d] = lattice_coordinate(multiple * point[d], offset[d], scale);
  }
}

// ---------------------------------------------------------------------------
// Exactly degenerate, inexact coordinate differences (nonzero tails)
// ---------------------------------------------------------------------------
// These degeneracies hold for arbitrary doubles, with no rounding involved:
// points on a line y = 2^k x, in a plane z = +-2^k x, or signed permutations
// of one coordinate tuple (all at the same distance from the origin). The
// coordinates span several binades, so their differences are not
// representable and the predicates need their exact stage D.

// How the coordinate values of an exactly degenerate configuration are drawn.
struct ValueStyle {
  int max_exponent;       // exponents are uniform in [-max_exponent, max_exponent]
  bool zeros_and_shorts;  // sometimes return zero or a short significand
};

double degenerate_value(Rng& r, ValueStyle style) {
  const int kind = static_cast<int>(r.below(8));
  const int e = r.range(-style.max_exponent, style.max_exponent);
  if (style.zeros_and_shorts && kind == 0) return 0.0;
  if (style.zeros_and_shorts && kind == 1) {
    const int bits = r.range(1, 8);
    return r.with_exponent_short(e, bits);
  }
  return r.with_exponent(e);
}

// Three points on the line y = 2^k x (or x = 2^k y).
GeneratorFn orient2d_degenerate(ValueStyle style) {
  return [=](Rng& r, double* c) {
    const int k = r.range(-3, 3);
    const bool swap_axes = r.coin();
    const double shared = degenerate_value(r, style);
    for (int i = 0; i < 3; i++) {
      const bool reuse = r.below(4) == 0;
      const double x = reuse ? shared : degenerate_value(r, style);
      const double y = std::ldexp(x, k);
      c[2 * i] = swap_axes ? y : x;
      c[2 * i + 1] = swap_axes ? x : y;
    }
  };
}

// Four points in a plane through the origin: one coordinate is +-2^k times
// another.
GeneratorFn orient3d_degenerate(ValueStyle style) {
  return [=](Rng& r, double* c) {
    const int k = r.range(-3, 3);
    const int target = static_cast<int>(r.below(3));
    const int source = (target + 1 + static_cast<int>(r.below(2))) % 3;
    const double sign = r.coin() ? 1.0 : -1.0;
    double pool[3];
    for (int d = 0; d < 3; d++) pool[d] = degenerate_value(r, style);
    for (int i = 0; i < 4; i++) {
      double p[3];
      for (int d = 0; d < 3; d++) {
        const bool reuse = r.below(3) == 0;
        p[d] = reuse ? pool[d] : degenerate_value(r, style);
      }
      p[target] = sign * std::ldexp(p[source], k);
      for (int d = 0; d < 3; d++) c[3 * i + d] = p[d];
    }
  };
}

// Four points among (+-x, +-y) and (+-y, +-x): all on one circle about 0.
GeneratorFn incircle_degenerate(ValueStyle style) {
  return [=](Rng& r, double* c) {
    const double x = degenerate_value(r, style);
    const double y = degenerate_value(r, style);
    for (int i = 0; i < 4; i++) {
      double px = r.coin() ? x : -x;
      double py = r.coin() ? y : -y;
      if (r.coin()) std::swap(px, py);
      c[2 * i] = px;
      c[2 * i + 1] = py;
    }
  };
}

// Writes a random signed permutation of v to p.
void signed_permutation(Rng& r, const double* v, double* p) {
  int order[3] = {0, 1, 2};
  for (int a = 2; a > 0; a--) std::swap(order[a], order[r.below(a + 1)]);
  for (int d = 0; d < 3; d++) p[d] = r.coin() ? v[order[d]] : -v[order[d]];
}

// Five points among the 48 signed permutations of (x, y, z): all on one
// sphere about 0.
GeneratorFn insphere_degenerate(ValueStyle style) {
  return [=](Rng& r, double* c) {
    double v[3];
    for (int d = 0; d < 3; d++) v[d] = degenerate_value(r, style);
    for (int i = 0; i < 5; i++) signed_permutation(r, v, c + 3 * i);
  };
}

// ---------------------------------------------------------------------------
// Mixed magnitudes with a swept distance from degeneracy
// ---------------------------------------------------------------------------
// Large and tiny coordinates in one case, with one coordinate displaced from
// an exactly degenerate position by a log-uniform amount, so that
// |determinant| / (its error bound) sweeps across every filter threshold.

void orient2d_mixed_magnitudes(Rng& r, double* c) {
  const int ea = r.range(-2, 2);
  const double a = r.with_exponent(ea);
  const int eb = r.range(-2, 2);
  const double b = r.with_exponent(eb);
  const int ex = r.range(-70, -55);
  const double x = r.with_exponent(ex);
  const int ed = r.range(-125, -5);
  const double delta = r.with_exponent(ed);
  // a and b on the line y = x; c tiny and displaced from the line by delta.
  c[0] = a;
  c[1] = a;
  c[2] = b;
  c[3] = b;
  c[4] = x;
  c[5] = x + delta;
}

void orient3d_mixed_magnitudes(Rng& r, double* c) {
  // a, b, c in the plane z = x; d small with z displaced.
  for (int i = 0; i < 3; i++) {
    const int ex = r.range(-2, 2);
    c[3 * i] = r.with_exponent(ex);
    const int ey = r.range(-2, 2);
    c[3 * i + 1] = r.with_exponent(ey);
    c[3 * i + 2] = c[3 * i];
  }
  const int ex = r.range(-70, -55);
  const double x = r.with_exponent(ex);
  const int ey = r.range(-70, 2);
  const double y = r.with_exponent(ey);
  const int ed = r.range(-125, -5);
  const double delta = r.with_exponent(ed);
  c[9] = x;
  c[10] = y;
  c[11] = x + delta;
}

void incircle_mixed_magnitudes(Rng& r, double* c) {
  const int ex = r.range(-2, 2);
  const double x = r.with_exponent(ex);
  const int ey = r.range(-60, -45);
  const double y = r.with_exponent(ey);
  const double points[4][2] = {{x, y}, {-x, y}, {y, x}, {-y, -x}};
  for (int i = 0; i < 4; i++) {
    c[2 * i] = points[i][0];
    c[2 * i + 1] = points[i][1];
  }
  // Displace the small coordinate of one point.
  const int i = static_cast<int>(r.below(4));
  const int j = std::fabs(c[2 * i]) < std::fabs(c[2 * i + 1]) ? 2 * i : 2 * i + 1;
  const int ed = r.range(-125, -50);
  c[j] += r.with_exponent(ed);
}

void insphere_mixed_magnitudes(Rng& r, double* c) {
  double v[3];
  const int e0 = r.range(-2, 2);
  v[0] = r.with_exponent(e0);
  const int e1 = r.range(-40, -30);
  v[1] = r.with_exponent(e1);
  const int e2 = r.range(-2, 2);
  v[2] = r.with_exponent(e2);
  for (int i = 0; i < 5; i++) signed_permutation(r, v, c + 3 * i);
  // Displace the smallest coordinate of one point.
  const int i = static_cast<int>(r.below(5));
  int j = 3 * i;
  for (int d = 1; d < 3; d++) {
    if (std::fabs(c[3 * i + d]) < std::fabs(c[j])) j = 3 * i + d;
  }
  const int ed = r.range(-125, -35);
  c[j] += r.with_exponent(ed);
}

// ---------------------------------------------------------------------------
// Threshold straddling
// ---------------------------------------------------------------------------

// The (deciding stage, sign) class of a case under the reference.
int reference_class(Pred pred, const double* c) {
  int stage;
  const double value = staged_reference_call(pred, c, &stage);
  return stage * 3 + sign_of(value) + 1;
}

// Starting from a seed case, moves one coordinate in ulp steps until the
// (stage, sign) class changes, then bisects to two ADJACENT doubles with
// different classes and returns one side at random. This puts inputs exactly
// on both sides of every error-bound threshold and every sign change.
GeneratorFn threshold(Pred pred, GeneratorFn seed_case) {
  return [=](Rng& r, double* c) {
    for (int attempt = 0; attempt < 8; attempt++) {
      seed_case(r, c);
      const int i = static_cast<int>(r.below(NUM_COORDS[pred]));
      const double start = c[i];
      const int start_class = reference_class(pred, c);
      std::int64_t far_steps = 0;  // a step count where the class differs
      for (int j = 0; j <= 50;) {
        const std::int64_t steps = (std::int64_t{1} << j) * (r.coin() ? 1 : -1);
        c[i] = step_ulps(start, steps);
        if (reference_class(pred, c) != start_class) {
          far_steps = steps;
          break;
        }
        j += 1 + static_cast<int>(r.below(3));
      }
      if (far_steps == 0) {
        c[i] = start;
        continue;  // no boundary near this seed; draw another
      }
      std::int64_t near_steps = 0;  // a step count where the class is unchanged
      while (far_steps - near_steps > 1 || near_steps - far_steps > 1) {
        const std::int64_t middle = near_steps + (far_steps - near_steps) / 2;
        c[i] = step_ulps(start, middle);
        if (reference_class(pred, c) != start_class) {
          far_steps = middle;
        } else {
          near_steps = middle;
        }
      }
      c[i] = step_ulps(start, r.coin() ? near_steps : far_steps);
      return;
    }
    // Falls through with the last seed case unchanged (still a valid input).
  };
}

std::vector<Generator> build_generators() {
  initialize_implementations();
  std::vector<Generator> generators;

  // Exponent half-ranges that keep every predicate far from overflow and
  // underflow (degree 2, 3, 4 and 5 polynomials of the coordinates).
  const int WIDE_EXPONENT[NUM_PREDS] = {140, 120, 100, 60};
  const ValueStyle WIDE_STYLE[NUM_PREDS] = {{40, true}, {30, true}, {30, true}, {20, true}};
  const ValueStyle FULL_53_BIT_STYLE = {4, false};

  const GeneratorFn near_degenerate[NUM_PREDS] = {orient2d_near_collinear, orient3d_near_coplanar,
                                                  incircle_near_cocircular,
                                                  insphere_near_cospherical};
  const GeneratorFn lattice[NUM_PREDS] = {orient2d_lattice, orient3d_lattice, incircle_lattice,
                                          insphere_lattice};
  const GeneratorFn mixed[NUM_PREDS] = {orient2d_mixed_magnitudes, orient3d_mixed_magnitudes,
                                        incircle_mixed_magnitudes, insphere_mixed_magnitudes};
  auto degenerate = [](Pred pred, ValueStyle style) -> GeneratorFn {
    switch (pred) {
      case ORIENT2D: return orient2d_degenerate(style);
      case ORIENT3D: return orient3d_degenerate(style);
      case INCIRCLE: return incircle_degenerate(style);
      default: return insphere_degenerate(style);
    }
  };

  for (int index = 0; index < NUM_PREDS; index++) {
    const Pred p = static_cast<Pred>(index);
    const std::string prefix = std::string(PRED_NAME[p]) + "/";
    const GeneratorFn wide = degenerate(p, WIDE_STYLE[p]);
    auto add = [&](const char* family, GeneratorFn fn, int cost = 1) {
      generators.push_back({prefix + family, p, cost, std::move(fn)});
    };

    add("uniform", uniform(p));
    add("wide_exponents", wide_exponents(p, WIDE_EXPONENT[p]));
    add("small_integers", small_integers(p, 1));
    add("shared_coordinates", shared_coordinates(p));
    add("ulp_grid", ulp_grid(p, 6));
    add("near_degenerate", near_degenerate[p]);
    if (p == ORIENT2D) add("classroom_grid", orient2d_classroom_grid);
    if (p == INCIRCLE) add("classroom_grid", incircle_classroom_grid);
    add("degenerate_lattice", lattice[p]);
    add("degenerate_lattice_ulps", perturb_all(p, lattice[p], 2));
    add("degenerate_wide", permute_points(p, wide));
    add("degenerate_wide_ulps", perturb_all(p, wide, 2));
    add("degenerate_wide_one_coordinate", perturb_one(p, permute_points(p, wide), 50));
    add("degenerate_53bit", permute_points(p, degenerate(p, FULL_53_BIT_STYLE)));
    add("mixed_magnitudes", permute_points(p, mixed[p]));
    add("threshold_near_degenerate", threshold(p, near_degenerate[p]), 8);
    add("threshold_mixed_magnitudes", threshold(p, mixed[p]), 8);
    add("threshold_degenerate_wide", threshold(p, permute_points(p, wide)), 8);
  }
  return generators;
}

}  // namespace

const std::vector<Generator>& all_generators() {
  static const std::vector<Generator> generators = build_generators();
  return generators;
}

}  // namespace shewchuk_test
