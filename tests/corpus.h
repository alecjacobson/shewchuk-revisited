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
  // Hash of every value the library returns. It differs from the reference's
  // only through the exact integer stage, whose effect on the values is fixed
  // (differential.cpp, integer_stage_value()), so it is as portable as the
  // reference's hash.
  std::uint64_t library_value_hash;
};

// Update only on purpose (a generator was added or changed), with the values
// printed by `differential --impl reference --tier <name>` and
// `differential --impl library --integer-stage --tier <name>`.
inline constexpr Tier TIERS[] = {
    {"quick", 10000, 0xc6eb1bed637b6295, 0x994e864faaace7f0, 0xdb3723f41c66fff2},
    {"deep", 200000, 0x134b449e4a58d2bc, 0x9cc0da23412f4d94, 0x0ab869c130ea03ac},
};

inline const Tier* find_tier(const char* name) {
  for (const Tier& tier : TIERS) {
    if (std::strcmp(tier.name, name) == 0) return &tier;
  }
  return nullptr;
}

}  // namespace shewchuk_test
