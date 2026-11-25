// Component-level comparison of the library's expansion routines with the
// pristine reference's.
//
// For fast_expansion_sum_zeroelim, scale_expansion_zeroelim and estimate, on
// random and adversarial inputs, the library must produce the same number of
// components and bit-identical components as the 1996 routine of the same
// name, and must not write past the components it reports.
//
// The reference's fast_expansion_sum_zeroelim reads e[elen] or f[flen] (one
// past the end; the value is unused). Here it is therefore given copies with
// one extra element, so that the read is in bounds. The library gets
// exact-size heap copies: if it read past the end, AddressSanitizer would
// report it in the sanitizer build of this test.
//
//   test_expansions [rounds]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "check.h"
#include "generators.h"
#include "implementations.h"
#include "test_hooks.h"

extern "C" {
// Routines of the pristine build (baseline/shewchuk_ref_rename.h). The 1996
// definitions are K&R style; these are their parameter types.
int shewchuk_ref_fast_expansion_sum_zeroelim(int elen, double* e, int flen, double* f, double* h);
int shewchuk_ref_scale_expansion_zeroelim(int elen, double* e, double b, double* h);
double shewchuk_ref_estimate(int elen, double* e);
int shewchuk_ref_grow_expansion_zeroelim(int elen, double* e, double b, double* h);
}

using namespace shewchuk_test;

namespace {

using Expansion = std::vector<double>;

const double CANARY = 0x1.badc0ffee0ddp+300;
// The largest sum any predicate asks for is 2880 + 2880 components
// (insphereexact, into deter[5760]).
const int LONGEST_INPUT = 2880;

bool same_bits(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

long comparisons = 0, longest = 0;

void report(const char* what, const Expansion& e, const Expansion& f, double b) {
  if (failure_count() > 5) return;
  std::printf("  MISMATCH in %s\n    e:", what);
  for (double x : e) std::printf(" %a", x);
  std::printf("\n    f:");
  for (double x : f) std::printf(" %a", x);
  std::printf("\n    b: %a\n", b);
}

// Runs both implementations of fast_expansion_sum_zeroelim; returns the sum.
Expansion check_sum(const Expansion& e, const Expansion& f) {
  const int elen = static_cast<int>(e.size()), flen = static_cast<int>(f.size());
  const std::size_t capacity = e.size() + f.size();

  // Reference: inputs padded by one element, see the top of the file.
  Expansion e_padded(e), f_padded(f);
  e_padded.push_back(CANARY);
  f_padded.push_back(CANARY);
  Expansion expected(capacity, CANARY);
  const int expected_length = shewchuk_ref_fast_expansion_sum_zeroelim(
      elen, e_padded.data(), flen, f_padded.data(), expected.data());

  // Library: exact-size inputs, output followed by canaries.
  const Expansion e_exact(e), f_exact(f);
  Expansion actual(capacity + 1, CANARY);
  const int actual_length = shewchuk_hooked_fast_expansion_sum_zeroelim(
      elen, e_exact.data(), flen, f_exact.data(), actual.data());

  bool ok = actual_length == expected_length && actual_length >= 1 &&
            static_cast<std::size_t>(actual_length) <= capacity;
  for (int i = 0; ok && i < expected_length; i++) ok = same_bits(actual[i], expected[i]);
  for (std::size_t i = ok ? actual_length : capacity + 1; i <= capacity; i++) {
    ok = ok && same_bits(actual[i], CANARY);
  }
  if (!ok) report("fast_expansion_sum_zeroelim", e, f, 0.0);
  CHECK(ok);
  comparisons++;
  if (expected_length > longest) longest = expected_length;
  expected.resize(static_cast<std::size_t>(expected_length));
  return expected;
}

// Runs both implementations of scale_expansion_zeroelim; returns the product.
Expansion check_scale(const Expansion& e, double b) {
  const int elen = static_cast<int>(e.size());
  const std::size_t capacity = 2 * e.size();

  Expansion e_copy(e);
  Expansion expected(capacity, CANARY);
  const int expected_length =
      shewchuk_ref_scale_expansion_zeroelim(elen, e_copy.data(), b, expected.data());

  const Expansion e_exact(e);
  Expansion actual(capacity + 1, CANARY);
  const int actual_length =
      shewchuk_hooked_scale_expansion_zeroelim(elen, e_exact.data(), b, actual.data());

  bool ok = actual_length == expected_length && actual_length >= 1 &&
            static_cast<std::size_t>(actual_length) <= capacity;
  for (int i = 0; ok && i < expected_length; i++) ok = same_bits(actual[i], expected[i]);
  for (std::size_t i = ok ? actual_length : capacity + 1; i <= capacity; i++) {
    ok = ok && same_bits(actual[i], CANARY);
  }
  if (!ok) report("scale_expansion_zeroelim", e, {}, b);
  CHECK(ok);
  comparisons++;
  if (expected_length > longest) longest = expected_length;
  expected.resize(static_cast<std::size_t>(expected_length));
  return expected;
}

void check_estimate(const Expansion& e) {
  Expansion e_copy(e);
  const Expansion e_exact(e);
  const double expected = shewchuk_ref_estimate(static_cast<int>(e.size()), e_copy.data());
  const double actual = shewchuk_hooked_estimate(static_cast<int>(e.size()), e_exact.data());
  const bool ok = same_bits(actual, expected);
  if (!ok) report("estimate", e, {}, 0.0);
  CHECK(ok);
  comparisons++;
}

// --- Input families ---------------------------------------------------------

// A valid (nonoverlapping, zero-free, increasing magnitude) expansion: the
// exact sum of `terms` random doubles, built with the reference's
// grow_expansion_zeroelim. `spread` is the exponent range of the terms: a
// small spread gives short expansions with a lot of cancellation, a large
// one gives one component per term.
Expansion valid_expansion(Rng& r, int terms, int spread) {
  Expansion e(static_cast<std::size_t>(terms) + 1);
  const int base = r.range(-60, 60);
  e[0] = r.with_exponent(base);
  int length = 1;
  for (int i = 1; i < terms; i++) {
    const int exponent = base + r.range(-spread, spread);
    const double term = r.coin() ? r.with_exponent(exponent)
                                 : r.with_exponent_short(exponent, r.range(1, 20));
    length = shewchuk_ref_grow_expansion_zeroelim(length, e.data(), term, e.data());
  }
  e.resize(static_cast<std::size_t>(length));
  return e;
}

// Arbitrary finite doubles in no particular order, with zeros (of both
// signs), repeated magnitudes and sign flips mixed in. Not a valid expansion:
// the routines must still perform the same operations on it.
Expansion arbitrary_components(Rng& r, int length) {
  Expansion e(static_cast<std::size_t>(length));
  double previous = 1.0;
  for (double& x : e) {
    switch (r.below(8)) {
      case 0: x = 0.0; break;
      case 1: x = -0.0; break;
      case 2: x = previous; break;
      case 3: x = -previous; break;
      default: {
        const int exponent = r.range(-80, 80);
        x = r.with_exponent(exponent);
      }
    }
    if (x != 0.0) previous = x;
  }
  return e;
}

// Components sorted by increasing magnitude but overlapping, with ties.
Expansion sorted_overlapping(Rng& r, int length) {
  Expansion e(static_cast<std::size_t>(length));
  int exponent = r.range(-80, -40);
  for (double& x : e) {
    x = r.below(6) == 0 ? 0.0 : r.with_exponent_short(exponent, r.range(1, 53));
    exponent += r.range(0, 3);
  }
  return e;
}

double random_scalar(Rng& r) {
  switch (r.below(10)) {
    case 0: return 0.0;
    case 1: return r.coin() ? 1.0 : -1.0;
    case 2: return std::ldexp(1.0, r.range(-40, 40));
    case 3: return 134217729.0;  // the splitter
    case 4: {
      const int exponent = r.range(-40, 40);
      return r.with_exponent_short(exponent, r.range(1, 27));
    }
    default: {
      const int exponent = r.range(-40, 40);
      return r.with_exponent(exponent);
    }
  }
}

Expansion negated(const Expansion& e) {
  Expansion n(e);
  for (double& x : n) x = -x;
  return n;
}

void check_all(Rng& r, const Expansion& e, const Expansion& f) {
  check_sum(e, f);
  check_sum(f, e);
  check_scale(e, random_scalar(r));
  check_estimate(e);
}

}  // namespace

int main(int argc, char** argv) {
  const long rounds = argc > 1 ? std::atol(argv[1]) : 3000;
  if (rounds < 1) {
    std::printf("usage: test_expansions [rounds]\n");
    return 2;
  }
  initialize_implementations();
  Rng r(0x5EEDC0DEu);

  // Fixed edge cases.
  {
    const Expansion zero = {0.0}, negative_zero = {-0.0}, one = {1.0}, tiny = {0x1p-60};
    const Expansion pair = {0x1p-60, 1.0}, zeros = {0.0, 0.0, 0.0, 0.0};
    const Expansion* fixed[] = {&zero, &negative_zero, &one, &tiny, &pair, &zeros};
    for (const Expansion* e : fixed) {
      for (const Expansion* f : fixed) {
        check_all(r, *e, *f);
        check_sum(*e, negated(*f));
      }
    }
  }

  for (long round = 0; round < rounds; round++) {
    // Valid expansions of every length mix, including length 1.
    {
      // One generator call per statement: the order of evaluation of
      // function arguments is unspecified.
      const int e_terms = r.range(1, 40), f_terms = r.range(1, 40);
      const int e_spread = r.range(0, 120), f_spread = r.range(0, 120);
      const Expansion e = valid_expansion(r, e_terms, e_spread);
      const Expansion f = valid_expansion(r, f_terms, f_spread);
      check_all(r, e, f);
      // Exact cancellation, total and partial: h = e - e, and e + (f - e).
      check_sum(e, negated(e));
      const Expansion difference = check_sum(f, negated(e));
      check_sum(e, difference);
      // One input much longer than the other; one-component inputs.
      const Expansion single = {r.with_exponent(r.range(-100, 100))};
      check_all(r, single, e);
      // Interleaved exponent ranges: the merge alternates between e and f.
      check_sum(e, check_scale(e, random_scalar(r)));
    }
    // Invalid inputs.
    {
      const Expansion e = arbitrary_components(r, r.range(1, 30));
      const Expansion f = arbitrary_components(r, r.range(1, 30));
      check_all(r, e, f);
      const Expansion g = sorted_overlapping(r, r.range(1, 30));
      const Expansion h = sorted_overlapping(r, r.range(1, 30));
      check_all(r, g, h);
      check_all(r, g, e);
    }
  }

  // Accumulate scaled expansions the way the predicates do. (A valid
  // expansion cannot get very long: its components do not overlap, and a
  // double only has about 2100 binary places.)
  long longest_valid = 0;
  for (int chain = 0; chain < 4; chain++) {
    Expansion total = valid_expansion(r, 30, 400);
    for (int step = 0; step < 60; step++) {
      const int terms = r.range(20, 60);
      const Expansion term = valid_expansion(r, terms, 450);
      const Expansion scaled = check_scale(term, random_scalar(r));
      total = check_sum(total, scaled);
      check_estimate(total);
      if (static_cast<long>(total.size()) > longest_valid) longest_valid = total.size();
    }
  }

  // The largest lengths any predicate can ask for, on arbitrary components.
  for (int repeat = 0; repeat < 8; repeat++) {
    const Expansion e = arbitrary_components(r, LONGEST_INPUT);
    const Expansion f = arbitrary_components(r, LONGEST_INPUT);
    check_all(r, e, f);
    check_sum(check_scale(e, random_scalar(r)), f);
  }

  std::printf("%ld comparisons; longest result %ld components, longest valid expansion %ld\n",
              comparisons, longest, longest_valid);
  CHECK(longest > LONGEST_INPUT);
  CHECK(longest_valid >= 40);
  return finish("library expansion routines equal the reference's, component by component");
}
