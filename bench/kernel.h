// The timed loops.
//
// They live in their own translation unit and receive the implementation as a
// table of function pointers at run time, so every predicate call is a real,
// non-inlined call into another translation unit: the "library call"
// scenario, which is what a user of a compiled predicates library gets.
// (Do not build the harness with link-time optimization.)
#pragma once

#include <cstddef>

#include "implementations.h"
#include "predicate_ids.h"

namespace shewchuk_bench {

enum class Mode {
  // Independent calls: the next call can start before the previous one has
  // finished (what a loop over independent queries sees).
  Throughput,
  // The address of each input depends on the previous result, so calls
  // cannot overlap (what a caller that waits for each answer sees).
  Latency,
};

// Calls the predicate on `count` consecutive cases stored back to back in
// `cases` and returns the sum of the signs of the results (the checksum).
long long run_kernel(const shewchuk_test::Implementation& implementation,
                     shewchuk_test::Pred pred, Mode mode, const double* cases, std::size_t count);

}  // namespace shewchuk_bench
