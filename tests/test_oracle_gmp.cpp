// Cross-checks the default big-integer oracle against the independent GMP
// rational oracle on the corpus. Skipped (exit 77) when GMP is not available.
#include <cstdio>

#include "check.h"
#include "corpus.h"
#include "generators.h"
#include "oracle.h"
#include "oracle_gmp.h"

using namespace shewchuk_test;

int main() {
  if (!gmp_oracle_available()) {
    std::printf("SKIPPED: built without GMP, so the second oracle cannot cross-check the first\n");
    return 77;
  }
  long cases = 0, disagreements = 0, signs[3] = {0, 0, 0};
  for_each_case(find_tier("quick")->cases_per_generator / 4,
                [&](const Generator& generator, const double* c) {
                  const int expected = oracle_sign(generator.pred, c);
                  const int gmp = gmp_oracle_sign(generator.pred, c);
                  cases++;
                  signs[expected + 1]++;
                  if (gmp != expected) {
                    if (disagreements++ < 5) {
                      std::printf("DISAGREE in %s: big integer %d, GMP %d\n",
                                  generator.name.c_str(), expected, gmp);
                    }
                  }
                });
  std::printf("big-integer vs GMP rational oracle: %ld cases (%ld negative, %ld zero, %ld positive), "
              "%ld disagreements\n",
              cases, signs[0], signs[1], signs[2], disagreements);
  CHECK(cases > 10000);
  CHECK(disagreements == 0);
  // The comparison is only meaningful if all three signs occur often.
  CHECK(signs[0] > cases / 10 && signs[1] > cases / 10 && signs[2] > cases / 10);
  return finish("oracle cross-check against GMP");
}
