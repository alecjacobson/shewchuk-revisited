// Corpus tiers and the recorded hashes of the reference on each tier.
#pragma once

#include <cstdint>
#include <cstring>

namespace shewchuk_test {

struct Tier {
  const char* name;
  // Cases drawn from each generator (expensive generators draw fewer, see
  // Generator::cost).
  long cases_per_generator;
  // Hash of every generated coordinate. It depends only on the generators
  // (and, for the threshold generators, on the reference), so a mismatch means
  // the corpus itself differs on this platform.
  std::uint64_t corpus_hash;
  // Hash of every value the pristine reference returns on the corpus when
  // built without contraction. Identical for every compiler, optimization
  // level and architecture tested so far; the control for refactor-only
  // changes, which must reproduce it bit for bit.
  std::uint64_t reference_value_hash;
};

// Update only on purpose (a generator was added or changed), with the values
// printed by `differential --impl reference --tier <name>`.
inline constexpr Tier TIERS[] = {
    {"quick", 10000, 0x661235cb969d53b0, 0x0144b4b46660d9f0},
    {"deep", 200000, 0x6810df0844e524d8, 0x7dcfb7679d71ad7c},
};

inline const Tier* find_tier(const char* name) {
  for (const Tier& tier : TIERS) {
    if (std::strcmp(tier.name, name) == 0) return &tier;
  }
  return nullptr;
}

}  // namespace shewchuk_test
