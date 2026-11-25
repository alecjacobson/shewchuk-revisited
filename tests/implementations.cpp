#include "implementations.h"

#include <cstring>

#include "shewchuk/predicates.h"
#include "shewchuk_ref.h"
#include "stages.h"
#include "test_hooks.h"

extern "C" {
// Test-only builds of the same pristine file (see tests/CMakeLists.txt).
SHEWCHUK_REF_DECLARE(shewchuk_staged_)
extern int shewchuk_staged_last_stage;
#if SHEWCHUK_HAVE_CONTRACTED_REFERENCE
SHEWCHUK_REF_DECLARE(shewchuk_contracted_)
#endif
}

namespace shewchuk_test {
namespace {

// The stage numbers of the library's test-only build are the suite's.
constexpr bool same_stage(int library_stage, int suite_stage) {
  return library_stage == suite_stage;
}
static_assert(same_stage(SHEWCHUK_STAGE_A0, STAGE_A0) && same_stage(SHEWCHUK_STAGE_A, STAGE_A) &&
              same_stage(SHEWCHUK_STAGE_B, STAGE_B) && same_stage(SHEWCHUK_STAGE_BX, STAGE_BX) &&
              same_stage(SHEWCHUK_STAGE_C, STAGE_C) && same_stage(SHEWCHUK_STAGE_D, STAGE_D));

// The 1996 functions take non-const pointers but never write through them.
double* unconst(const double* p) { return const_cast<double*>(p); }

// Defines <tag>_orient2d ... <tag>_insphere with the table's signature,
// forwarding to the functions with the given symbol prefix.
#define SHEWCHUK_TEST_ADAPTERS(tag, prefix)                                              \
  double tag##_orient2d(const double* a, const double* b, const double* c) {             \
    return prefix##orient2d(unconst(a), unconst(b), unconst(c));                         \
  }                                                                                      \
  double tag##_orient3d(const double* a, const double* b, const double* c,               \
                        const double* d) {                                               \
    return prefix##orient3d(unconst(a), unconst(b), unconst(c), unconst(d));             \
  }                                                                                      \
  double tag##_incircle(const double* a, const double* b, const double* c,               \
                        const double* d) {                                               \
    return prefix##incircle(unconst(a), unconst(b), unconst(c), unconst(d));             \
  }                                                                                      \
  double tag##_insphere(const double* a, const double* b, const double* c,               \
                        const double* d, const double* e) {                              \
    return prefix##insphere(unconst(a), unconst(b), unconst(c), unconst(d), unconst(e)); \
  }

SHEWCHUK_TEST_ADAPTERS(reference, shewchuk_ref_)
SHEWCHUK_TEST_ADAPTERS(staged, shewchuk_staged_)

const char* always_available() { return nullptr; }

#if SHEWCHUK_HAVE_CONTRACTED_REFERENCE
SHEWCHUK_TEST_ADAPTERS(contracted, shewchuk_contracted_)

const char* contracted_unavailable_reason() {
#if defined(__x86_64__) || defined(__i386__)
  // The contracted build is compiled with -mfma.
  if (!__builtin_cpu_supports("fma")) return "this CPU has no FMA instructions";
#endif
  return nullptr;
}
#else
// Placeholders so that the table entry exists and reports why it cannot run.
double missing_orient2d(const double*, const double*, const double*) { return 0.0; }
double missing_orient3d(const double*, const double*, const double*, const double*) { return 0.0; }
double missing_insphere(const double*, const double*, const double*, const double*,
                        const double*) {
  return 0.0;
}
const char* contracted_unavailable_reason() {
  return "not built: the contraction negative control is only defined for GCC and clang on "
         "x86-64 and arm64";
}
#endif

}  // namespace

void initialize_implementations() {
  static const bool done = [] {
    shewchuk_ref_exactinit();
    shewchuk_staged_exactinit();
#if SHEWCHUK_HAVE_CONTRACTED_REFERENCE
    // exactinit() itself is plain arithmetic on constants; it is safe to run
    // even when the predicates of this build are not.
    if (contracted_unavailable_reason() == nullptr) shewchuk_contracted_exactinit();
#endif
    return true;
  }();
  (void)done;
}

const std::vector<Implementation>& all_implementations() {
  static const std::vector<Implementation> table = {
      {"reference", "pristine 1996 predicates.c, contraction off (the baseline)",
       reference_orient2d, reference_orient3d, reference_incircle, reference_insphere,
       always_available},
      {"reference_staged", "the same file with stage-recording instrumentation (test only)",
       staged_orient2d, staged_orient3d, staged_incircle, staged_insphere, always_available},
      {"library", "the library (src/predicates.cpp): bit-identical port of the reference",
       shewchuk_orient2d, shewchuk_orient3d, shewchuk_incircle, shewchuk_insphere,
       always_available},
      {"library_hooked", "the library's test-only build with stage recording",
       shewchuk_hooked_orient2d, shewchuk_hooked_orient3d, shewchuk_hooked_incircle,
       shewchuk_hooked_insphere, always_available},
#if SHEWCHUK_HAVE_CONTRACTED_REFERENCE
      {"reference_contracted",
       "NEGATIVE CONTROL: the same file built with -O2 -ffp-contract=fast; must be wrong",
       contracted_orient2d, contracted_orient3d, contracted_incircle, contracted_insphere,
       contracted_unavailable_reason},
#else
      {"reference_contracted", "NEGATIVE CONTROL: not built for this compiler", missing_orient2d,
       missing_orient3d, missing_orient3d, missing_insphere, contracted_unavailable_reason},
#endif
  };
  return table;
}

const Implementation* find_implementation(const char* name) {
  for (const Implementation& implementation : all_implementations()) {
    if (std::strcmp(implementation.name, name) == 0) return &implementation;
  }
  return nullptr;
}

const Implementation& reference_implementation() { return all_implementations().front(); }

double staged_reference_call(Pred pred, const double* c, int* stage) {
  static const Implementation& staged = *find_implementation("reference_staged");
  shewchuk_staged_last_stage = -1;
  const double value = staged.call(pred, c);
  *stage = shewchuk_staged_last_stage;
  return value;
}

double staged_library_call(Pred pred, const double* c, int* stage) {
  static const Implementation& hooked = *find_implementation("library_hooked");
  shewchuk_hooked_last_stage = -1;
  const double value = hooked.call(pred, c);
  *stage = shewchuk_hooked_last_stage;
  return value;
}

}  // namespace shewchuk_test
