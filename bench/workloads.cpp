#include "workloads.h"

#include <cstring>

#include "implementations.h"

namespace shewchuk_bench {

using namespace shewchuk_test;

const std::vector<WorkloadSpec>& all_workloads() {
  const int none = -1;
  // Calls are listed as {orient2d, orient3d, incircle, insphere}.
  static const std::vector<WorkloadSpec> workloads = {
      // --- Whole generator families
      {"uniform",
       "coordinates uniform in [-1, 1): the first filter decides (orient2d: its sign test or "
       "its filter)",
       none, {{"uniform", 1.0}}, {1 << 21, 1 << 21, 1 << 21, 1 << 21}},
      {"wide_exponents", "every coordinate has its own binary exponent", none,
       {{"wide_exponents", 1.0}}, {1 << 21, 1 << 20, 1 << 18, 1 << 17}},
      {"near_degenerate",
       "an exactly degenerate real configuration (collinear, coplanar, cocircular, "
       "cospherical) rounded to doubles",
       none, {{"near_degenerate", 1.0}}, {1 << 19, 1 << 16, 1 << 15, 1 << 13}},
      {"degenerate_lattice",
       "exactly degenerate, coordinate differences exact (integer lattice, offset and scaled)",
       none, {{"degenerate_lattice", 1.0}}, {1 << 19, 1 << 16, 1 << 16, 1 << 15}},
      {"degenerate_53bit",
       "exactly degenerate, full 53-bit coordinates whose differences are not representable",
       none, {{"degenerate_53bit", 1.0}}, {1 << 19, 1 << 15, 1 << 14, 1 << 13}},

      // --- One stage each
      {"stage_A0", "uniform cases decided by the sign test before the filter (orient2d only)",
       STAGE_A0, {{"uniform", 1.0}}, {1 << 21, 0, 0, 0}},
      // For the other predicates every uniform case is a stage A case: use `uniform`.
      {"stage_A", "uniform cases decided by the first filter (orient2d only)", STAGE_A,
       {{"uniform", 1.0}}, {1 << 21, 0, 0, 0}},
      {"stage_B", "near-degenerate cases decided by the second filter", STAGE_B,
       {{"near_degenerate", 1.0}}, {1 << 19, 1 << 15, 1 << 15, 1 << 13}},
      {"stage_Bx", "near-degenerate cases that exit because every difference tail is zero",
       STAGE_BX, {{"near_degenerate", 1.0}}, {1 << 19, 1 << 16, 1 << 16, 1 << 15}},
      {"stage_C", "near-degenerate cases decided by the third filter", STAGE_C,
       {{"near_degenerate", 1.0}}, {1 << 19, 1 << 16, 1 << 15, 1 << 13}},
      {"stage_D",
       "mixed-magnitude cases a tiny displacement away from degenerate, decided by the exact "
       "stage",
       STAGE_D, {{"mixed_magnitudes", 1.0}}, {1 << 19, 1 << 15, 1 << 14, 1 << 13}},

      // --- Mixtures (probabilities per call, drawn independently)
      {"mix_easy", "98% uniform, 2% near_degenerate: mostly general position", none,
       {{"uniform", 0.98}, {"near_degenerate", 0.02}}, {1 << 20, 1 << 20, 1 << 19, 1 << 18}},
      {"mix_grid", "93% uniform, 7% degenerate_lattice: grid-like input with exact ties", none,
       {{"uniform", 0.93}, {"degenerate_lattice", 0.07}}, {1 << 20, 1 << 20, 1 << 19, 1 << 18}},
      {"mix_surface", "40% uniform, 60% near_degenerate: points sampled on a curve or surface",
       none, {{"uniform", 0.40}, {"near_degenerate", 0.60}}, {1 << 19, 1 << 17, 1 << 15, 1 << 14}},
  };
  return workloads;
}

namespace {

const Generator* find_generator(Pred pred, const char* family) {
  const std::string name = std::string(PRED_NAME[pred]) + "/" + family;
  for (const Generator& generator : all_generators()) {
    if (generator.name == name) return &generator;
  }
  return nullptr;
}

std::uint64_t stream_seed(const WorkloadSpec& spec, Pred pred, std::uint64_t seed) {
  Hash64 hash;
  for (const char* ch = PRED_NAME[pred]; *ch; ch++) hash.add(static_cast<unsigned char>(*ch));
  hash.add('/');
  for (const char* ch = spec.name; *ch; ch++) hash.add(static_cast<unsigned char>(*ch));
  hash.add(seed);
  return hash.value();
}

// After this many consecutive rejected candidates the stage filter is taken
// to be unsatisfiable.
const long MAX_CONSECUTIVE_REJECTIONS = 20'000'000;

}  // namespace

WorkloadStream::WorkloadStream(const WorkloadSpec& spec, Pred pred, std::uint64_t seed)
    : pred_(pred), stage_filter_(spec.stage_filter), rng_(stream_seed(spec, pred, seed)) {
  double total = 0.0;
  for (const Component& component : spec.components) {
    const Generator* generator = find_generator(pred, component.family);
    if (!generator) {
      error_ = std::string("workload ") + spec.name + ": no generator family '" +
               component.family + "' for " + PRED_NAME[pred];
      return;
    }
    generators_.push_back(generator);
    total += component.probability;
    cumulative_.push_back(total);
  }
  if (generators_.empty() || total < 0.999999 || total > 1.000001) {
    error_ = std::string("workload ") + spec.name + ": component probabilities must sum to 1";
  }
}

void WorkloadStream::draw(Rng& rng, double* c) const {
  std::size_t which = 0;
  if (generators_.size() > 1) {
    const double u = rng.unit();
    while (which + 1 < generators_.size() && u >= cumulative_[which]) which++;
  }
  generators_[which]->fn(rng, c);
}

bool WorkloadStream::prepare(std::size_t count) {
  if (!ok()) return false;
  const int num_coords = NUM_COORDS[pred_];
  Rng rng = rng_;  // fill() replays the same stream
  long rejections = 0;
  for (std::size_t produced = 0; produced < count;) {
    double c[MAX_COORDS];
    draw(rng, c);
    candidates_++;
    int stage = -1;
    const double value = staged_reference_call(pred_, c, &stage);
    if (stage < 0 || stage >= NUM_STAGES) {
      error_ = "the instrumented reference returned without recording a stage";
      return false;
    }
    if (stage_filter_ >= 0) {
      const bool keep = stage == stage_filter_;
      kept_.push_back(keep);
      if (!keep) {
        if (++rejections > MAX_CONSECUTIVE_REJECTIONS) {
          error_ = std::string("no case decided by stage ") + STAGE_NAME[stage_filter_] +
                   " in 20 million candidates";
          return false;
        }
        continue;
      }
      rejections = 0;
    }
    stage_counts_[stage]++;
    reference_checksum_ += sign_of(value);
    for (int i = 0; i < num_coords; i++) {
      std::uint64_t bits;
      std::memcpy(&bits, &c[i], sizeof bits);
      input_hash_ = (input_hash_ ^ bits) * 0x9FB21C651E98DF25ull;
      input_hash_ ^= input_hash_ >> 32;
    }
    cases_++;
    produced++;
  }
  return true;
}

bool WorkloadStream::fill(double* out, std::size_t count) {
  if (!ok()) return false;
  if (filled_ + static_cast<long>(count) > cases_) {
    error_ = "fill() beyond the prepared number of cases";
    return false;
  }
  const int num_coords = NUM_COORDS[pred_];
  for (std::size_t produced = 0; produced < count;) {
    double* c = out + produced * num_coords;
    draw(rng_, c);
    if (stage_filter_ >= 0 && !kept_[next_candidate_++]) continue;
    produced++;
  }
  filled_ += static_cast<long>(count);
  return true;
}

}  // namespace shewchuk_bench
