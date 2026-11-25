// Thread-safety smoke test for the library.
//
// The 1996 code keeps its error bounds in globals written by exactinit(); the
// library has no mutable state at all. Several threads call the four
// predicates on the same inputs at the same time and must get the values a
// single thread got. Built with -DSHEWCHUK_SANITIZE=thread, ThreadSanitizer
// additionally reports any data race inside the library.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "check.h"
#include "generators.h"
#include "implementations.h"

using namespace shewchuk_test;

namespace {

struct Case {
  Pred pred;
  double coords[MAX_COORDS];
  double expected;
};

}  // namespace

int main() {
  initialize_implementations();
  const Implementation& library = *find_implementation("library");

  // A small slice of every generator, so that every stage is exercised. The
  // generators are not thread-safe, so the cases are collected up front.
  std::vector<Case> cases;
  for_each_case(40, [&](const Generator& generator, const double* c) {
    Case one;
    one.pred = generator.pred;
    std::memcpy(one.coords, c, sizeof(double) * NUM_COORDS[generator.pred]);
    one.expected = library.call(generator.pred, c);
    cases.push_back(one);
  });

  const int num_threads = 4, repeats = 3;
  std::atomic<long> differences{0}, calls{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; t++) {
    threads.emplace_back([&, t] {
      long local_differences = 0, local_calls = 0;
      for (int repeat = 0; repeat < repeats; repeat++) {
        // Each thread starts at a different case.
        for (std::size_t i = 0; i < cases.size(); i++) {
          const Case& one = cases[(i + t * cases.size() / num_threads) % cases.size()];
          const double value = library.call(one.pred, one.coords);
          if (std::memcmp(&value, &one.expected, sizeof value) != 0) local_differences++;
          local_calls++;
        }
      }
      differences += local_differences;
      calls += local_calls;
    });
  }
  for (std::thread& thread : threads) thread.join();

  std::printf("%d threads, %ld calls on %zu cases, %ld differences\n", num_threads, calls.load(),
              cases.size(), differences.load());
  CHECK(calls.load() == static_cast<long>(num_threads * repeats * cases.size()));
  CHECK(differences.load() == 0);
  return finish("library predicates called from several threads");
}
