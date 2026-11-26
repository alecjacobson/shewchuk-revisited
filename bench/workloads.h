// Benchmark workloads: streams of predicate inputs, tagged by the stage that
// decides each input in the pristine reference.
//
// A workload draws cases from one or more generator families of the test
// corpus (tests/generators.h, looked up by name, so the benchmark and the
// exactness tests share one set of generators), runs each case through the
// stage-instrumented reference, and optionally keeps only the cases decided
// by one stage ("generate and classify"). Three kinds:
//
//   family    every case of one generator family, e.g. `uniform`;
//   stage     the cases of a family that one stage decides, e.g. `stage_B`;
//   mixture   cases drawn at random from several families with stated
//             probabilities, e.g. `mix_easy`.
//
// Streams are deterministic (seeded from the workload name) and are produced
// chunk by chunk into a caller-owned buffer: no array of all inputs is ever
// stored, and no input is ever produced twice.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "generators.h"
#include "predicate_ids.h"
#include "stages.h"

namespace shewchuk_bench {

struct Component {
  const char* family;  // generator family in tests/generators.cpp
  double probability;  // of drawing the next candidate from this family
};

struct WorkloadSpec {
  const char* name;
  const char* description;
  // Keep only the cases decided by this stage; -1 keeps every case.
  int stage_filter;
  std::vector<Component> components;
  // Timed calls per predicate at scale 1 (0: not defined for the predicate).
  // Cheap stages get many calls; stages that cost thousands of cycles get
  // fewer, so that every workload takes a comparable time.
  long calls[shewchuk_test::NUM_PREDS];
};

// All workloads, in a fixed order.
const std::vector<WorkloadSpec>& all_workloads();

// Deterministic stream of the cases of one workload for one predicate.
//
// Classification and timing are kept apart. prepare() makes a first pass over
// the stream with the stage-instrumented reference: it gathers the statistics
// and remembers which candidates the stage filter keeps. fill() then
// regenerates the same cases without calling any predicate. Classifying each
// chunk right before timing it would be simpler, but it measurably slows the
// timed calls down (about 0.5% to 1.5%): the instrumented copy executes the
// same branch patterns at other addresses and competes for the branch
// predictor.
class WorkloadStream {
 public:
  // Check ok() after construction: false if a generator family does not exist.
  WorkloadStream(const WorkloadSpec& spec, shewchuk_test::Pred pred, std::uint64_t seed);

  bool ok() const { return error_.empty(); }
  const std::string& error() const { return error_; }

  // Classifies the first `count` cases. Call once, before fill(). Returns
  // false, with error() set, if the stage filter rejects so many candidates
  // that the workload cannot be produced.
  bool prepare(std::size_t count);

  // Writes the next `count` cases (NUM_COORDS[pred] doubles each) to `out`.
  // Returns false, with error() set, beyond the prepared number of cases.
  bool fill(double* out, std::size_t count);

  // Statistics over the prepared cases.
  long cases() const { return cases_; }
  long candidates() const { return candidates_; }  // drawn, including rejected
  const long* stage_counts() const { return stage_counts_; }
  // Sum of the signs the reference returns: the checksum every exact
  // implementation must reproduce.
  long long reference_checksum() const { return reference_checksum_; }
  // Hash of every coordinate, in order.
  std::uint64_t input_hash() const { return input_hash_; }

 private:
  // Draws the next candidate into c.
  void draw(shewchuk_test::Rng& rng, double* c) const;

  shewchuk_test::Pred pred_;
  int stage_filter_;
  std::vector<const shewchuk_test::Generator*> generators_;
  std::vector<double> cumulative_;
  shewchuk_test::Rng rng_;  // state of the fill() pass
  std::string error_;
  // With a stage filter: whether each candidate, in order, is kept.
  std::vector<bool> kept_;
  std::size_t next_candidate_ = 0;
  long filled_ = 0;
  long cases_ = 0;
  long candidates_ = 0;
  long stage_counts_[shewchuk_test::NUM_STAGES] = {};
  long long reference_checksum_ = 0;
  std::uint64_t input_hash_ = 0x9E3779B97F4A7C15ull;
};

}  // namespace shewchuk_bench
