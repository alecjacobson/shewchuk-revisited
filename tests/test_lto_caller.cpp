// A caller compiled with fast-math flags and link-time optimization still
// gets exact results from the library.
//
// The library promises that the code calling it may be compiled with any
// flags. Link-time optimization is where that could break: the optimizer
// sees the caller (lto_caller.cpp, compiled with -O3 -ffast-math) and the
// library together, may inline the predicates into the caller, and generates
// all machine code at link time, on a link line that carries the caller's
// flags. This test runs the whole quick corpus through such a caller.
//
//   test_lto_caller exact       The library as the project builds it (strict
//                               flags last), with the caller's flags in front
//                               and link-time optimization. Must return the
//                               recorded values bit for bit.
//   test_lto_caller contracted  NEGATIVE CONTROL: the same, but the library
//                               copy is built with -ffp-contract=fast, as if
//                               the strict flags had been lost. Must NOT
//                               return the recorded values.
//
// Exit status: 0 pass, 1 fail, 2 usage error, 77 skipped.
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "corpus.h"
#include "generators.h"
#include "oracle.h"

using namespace shewchuk_test;

extern "C" {
double shewchuk_lto_caller_exact(int predicate, const double* c);
double shewchuk_lto_caller_contracted(int predicate, const double* c);
}

namespace {

const int EXIT_SKIPPED = 77;

#if SHEWCHUK_HAVE_LTO_CALLER
// Null if code built with the test's target flags can run on this CPU.
const char* cpu_unavailable_reason() {
#if SHEWCHUK_LTO_CALLER_USES_FMA
  // The caller and the library copies are compiled with -mfma, so that the
  // compiler has an instruction to contract to.
  if (!__builtin_cpu_supports("fma")) return "this CPU has no FMA instructions";
#endif
  return nullptr;
}

int run(double (*call)(int, const double*), bool expect_exact) {
  const Tier& tier = *find_tier("quick");
  Hash64 corpus_hash, value_hash;
  long cases = 0, wrong = 0;
  for_each_case(tier.cases_per_generator, [&](const Generator& generator, const double* c) {
    const Pred pred = generator.pred;
    cases++;
    for (int i = 0; i < NUM_COORDS[pred]; i++) corpus_hash.add_double(c[i]);
    const double value = call(pred, c);
    value_hash.add_double(value);
    // A NaN has no sign: never let it pass as "zero".
    if (!std::isfinite(value) || sign_of(value) != oracle_sign(pred, c)) wrong++;
  });

  std::printf("%ld cases, %ld wrong signs\n", cases, wrong);
  std::printf("corpus hash: %016" PRIx64 "\n", corpus_hash.value());
  std::printf("value hash:  %016" PRIx64 " (recorded library value hash: %016" PRIx64 ")\n",
              value_hash.value(), tier.library_value_hash);

  if (corpus_hash.value() != tier.corpus_hash) {
    std::printf("FAIL: corpus hash differs from the recorded %016" PRIx64 "\n", tier.corpus_hash);
    return 1;
  }
  // The library's own recorded values (corpus.h): the reference's, except
  // where its exact integer stage decides.
  const bool exact = wrong == 0 && value_hash.value() == tier.library_value_hash;
  if (expect_exact) {
    if (!exact) {
      std::printf("FAIL: the caller's flags or link-time optimization changed the library's "
                  "arithmetic\n");
      return 1;
    }
    std::printf("PASS: a fast-math caller with link-time optimization gets the recorded values\n");
    return 0;
  }
  if (exact) {
    std::printf("FAIL: negative control was NOT detected: a library copy built with contraction "
                "returned the recorded values\n");
    return 1;
  }
  std::printf("PASS: negative control detected, as expected\n");
  return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  const bool exact = argc == 2 && std::strcmp(argv[1], "exact") == 0;
  const bool contracted = argc == 2 && std::strcmp(argv[1], "contracted") == 0;
  if (!exact && !contracted) {
    std::printf("usage: test_lto_caller exact|contracted\n");
    return 2;
  }
#if !SHEWCHUK_HAVE_LTO_CALLER
  std::printf("SKIPPED: not built: link-time optimization is not available with this "
              "toolchain\n");
  return EXIT_SKIPPED;
#else
  if (const char* reason = cpu_unavailable_reason()) {
    std::printf("SKIPPED: %s\n", reason);
    return EXIT_SKIPPED;
  }
  if (exact) return run(shewchuk_lto_caller_exact, true);
#if SHEWCHUK_HAVE_LTO_CALLER_CONTRACTED
  return run(shewchuk_lto_caller_contracted, false);
#else
  std::printf("SKIPPED: not built: the contraction negative control is only defined for GCC and "
              "clang on x86-64 and arm64\n");
  return EXIT_SKIPPED;
#endif
#endif
}
