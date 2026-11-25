// Deterministic, streaming input generators for the four predicates.
//
// Uniform random input never leaves the first filter, so it tests nothing
// beyond stage A. These generators are built to reach every stage: near
// degenerate inputs, grids of adjacent doubles, exactly degenerate inputs with
// and without exact coordinate differences, mixed magnitudes, and pairs of
// adjacent doubles that straddle each filter threshold.
//
// The corpus must be bit-identical on every platform, so:
//   * randomness comes only from the splitmix64 stream below, never from
//     <random> distributions (their output differs between standard libraries);
//   * doubles are built from +, -, *, /, ldexp and integer conversions only
//     (all correctly rounded); no sin, cos or other libm approximations;
//   * no expression makes two generator calls whose order is unspecified;
//   * the suite is compiled without floating-point contraction.
//
// Every generated coordinate is zero or has magnitude in [2^-142, 2^201], the
// domain the 1996 paper states for the predicates.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "predicate_ids.h"

namespace shewchuk_test {

// splitmix64 with a few helpers for building doubles bit by bit.
class Rng {
 public:
  explicit Rng(std::uint64_t seed) : state_(seed) {}

  std::uint64_t u64() {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  // Uniform-ish integer in [0, n); n is small, so the modulo bias is irrelevant.
  std::uint64_t below(std::uint64_t n) { return u64() % n; }
  // Integer in [lo, hi].
  int range(int lo, int hi) { return lo + static_cast<int>(below(static_cast<std::uint64_t>(hi - lo + 1))); }
  bool coin() { return (u64() & 1) != 0; }
  // Double in [0, 1) with 53 random bits.
  double unit() { return static_cast<double>(u64() >> 11) * 0x1p-53; }
  // Double in [-1, 1).
  double symmetric() { return 2.0 * unit() - 1.0; }
  // Random sign, full random 53-bit significand, magnitude in [2^e, 2^(e+1)).
  double with_exponent(int e);
  // Like with_exponent, but only the top `bits` significand bits are random
  // (short significands make more products and differences exact).
  double with_exponent_short(int e, int bits);

 private:
  std::uint64_t state_;
};

// Writes one test case (NUM_COORDS[pred] doubles) to the output array.
using GeneratorFn = std::function<void(Rng&, double*)>;

struct Generator {
  std::string name;  // "<predicate>/<family>"
  Pred pred;
  // The generator produces max(1, cases_per_generator / cost) cases. Expensive
  // generators (those that bisect with the reference) have cost > 1.
  int cost;
  GeneratorFn fn;
};

// All generators, in a fixed order. The first call initializes the
// instrumented reference, which the threshold generators use to locate
// stage and sign boundaries.
const std::vector<Generator>& all_generators();

// 64-bit FNV-1a over 64-bit words, byte order independent.
class Hash64 {
 public:
  void add(std::uint64_t word) {
    for (int i = 0; i < 8; i++) {
      state_ ^= (word >> (8 * i)) & 0xff;
      state_ *= 0x100000001B3ull;
    }
  }
  void add_double(double x);  // the IEEE 754 bit pattern of x
  std::uint64_t value() const { return state_; }

 private:
  std::uint64_t state_ = 0xCBF29CE484222325ull;
};

// Streams the corpus: calls visit(generator, coords) for every case of every
// generator, in order. Each generator has its own stream, seeded from its
// name, so adding or reordering generators does not change existing cases.
// Nothing is stored: memory use is independent of the corpus size.
template <class Visit>
void for_each_case(long cases_per_generator, Visit&& visit) {
  for (const Generator& generator : all_generators()) {
    Hash64 seed;
    for (char ch : generator.name) seed.add(static_cast<unsigned char>(ch));
    Rng rng(seed.value());
    long count = cases_per_generator / generator.cost;
    if (count < 1) count = 1;
    for (long i = 0; i < count; i++) {
      double coords[MAX_COORDS];
      generator.fn(rng, coords);
      visit(generator, static_cast<const double*>(coords));
    }
  }
}

}  // namespace shewchuk_test
