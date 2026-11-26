#include "implementations.h"

#include <cstring>
#include <initializer_list>

#include "oracle_gmp.h"
#include "shewchuk/predicates.h"
#include "shewchuk_ref.h"
#include "stages.h"
#include "test_hooks.h"

#if SHEWCHUK_BUILD_COMPARATORS
// Third-party comparators (comparators/CMakeLists.txt). The generated header
// defines SHEWCHUK_HAVE_COMPARATOR_<NAME> and SHEWCHUK_COMPARATOR_<NAME>_BUILD
// (version and flags) for each one that was built.
#include "comparators.h"
#include "shewchuk_comparator_info.h"
#endif

extern "C" {
// Test-only builds of the same pristine file (see tests/CMakeLists.txt).
SHEWCHUK_REF_DECLARE(shewchuk_staged_)
extern int shewchuk_staged_last_stage;
#if SHEWCHUK_HAVE_CONTRACTED_REFERENCE
SHEWCHUK_REF_DECLARE(shewchuk_contracted_)
#endif
#if SHEWCHUK_HAVE_DEFAULT_FLAGS_REFERENCE
SHEWCHUK_REF_DECLARE(shewchuk_default_flags_)
#endif

// Test-only builds of the library (src/predicates.cpp) under other names
// (see tests/CMakeLists.txt, shewchuk_add_library_variant).
#define SHEWCHUK_TEST_DECLARE_LIBRARY(prefix)                                          \
  double prefix##orient2d(const double* pa, const double* pb, const double* pc);       \
  double prefix##orient3d(const double* pa, const double* pb, const double* pc,        \
                          const double* pd);                                           \
  double prefix##incircle(const double* pa, const double* pb, const double* pc,        \
                          const double* pd);                                           \
  double prefix##insphere(const double* pa, const double* pb, const double* pc,        \
                          const double* pd, const double* pe);
SHEWCHUK_TEST_DECLARE_LIBRARY(shewchuk_expansion_only_)
SHEWCHUK_TEST_DECLARE_LIBRARY(shewchuk_portable_)
}

namespace shewchuk_test {
namespace {

// The stage numbers of the library's test-only build are the suite's.
constexpr bool same_stage(int library_stage, int suite_stage) {
  return library_stage == suite_stage;
}
static_assert(same_stage(SHEWCHUK_STAGE_A0, STAGE_A0) && same_stage(SHEWCHUK_STAGE_A, STAGE_A) &&
              same_stage(SHEWCHUK_STAGE_B, STAGE_B) && same_stage(SHEWCHUK_STAGE_BX, STAGE_BX) &&
              same_stage(SHEWCHUK_STAGE_C, STAGE_C) && same_stage(SHEWCHUK_STAGE_D, STAGE_D) &&
              same_stage(SHEWCHUK_STAGE_I, STAGE_I));

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
const char* contracted_unavailable_reason() {
  return "not built: the contraction negative control is only defined for GCC and clang on "
         "x86-64 and arm64";
}
#endif

#if SHEWCHUK_HAVE_DEFAULT_FLAGS_REFERENCE
SHEWCHUK_TEST_ADAPTERS(default_flags, shewchuk_default_flags_)

const char* default_flags_unavailable_reason() {
#if defined(__x86_64__) || defined(__i386__)
  // Built with -mfma: baseline x86-64 has no instruction to contract to.
  if (!__builtin_cpu_supports("fma")) return "this CPU has no FMA instructions";
#endif
  return nullptr;
}
#else
const char* default_flags_unavailable_reason() {
  return "not built: only GCC contracts across statements by default (GNU dialects imply "
         "-ffp-contract=fast), and only x86-64 and arm64 are covered";
}
#endif

#if !SHEWCHUK_HAVE_CONTRACTED_REFERENCE || !SHEWCHUK_HAVE_DEFAULT_FLAGS_REFERENCE
// Placeholders so that the table entry exists and reports why it cannot run.
[[maybe_unused]] double missing_orient2d(const double*, const double*, const double*) {
  return 0.0;
}
[[maybe_unused]] double missing_orient3d(const double*, const double*, const double*,
                                         const double*) {
  return 0.0;
}
[[maybe_unused]] double missing_insphere(const double*, const double*, const double*,
                                         const double*, const double*) {
  return 0.0;
}
#endif

// --- Comparators (docs/comparators.md) ---------------------------------------

// Exact evaluation with GMP rationals: the suite's second oracle
// (oracle_gmp.cpp) as a slow reference point.
template <class... Points>
double gmp_rational(Pred pred, Points... points) {
  double coords[MAX_COORDS];
  int next = 0;
  for (const double* point : {points...}) {
    for (int i = 0; i < DIMENSION[pred]; i++) coords[next++] = point[i];
  }
  return static_cast<double>(gmp_oracle_sign(pred, coords));
}
double gmp_rational_orient2d(const double* a, const double* b, const double* c) {
  return gmp_rational(ORIENT2D, a, b, c);
}
double gmp_rational_orient3d(const double* a, const double* b, const double* c,
                             const double* d) {
  return gmp_rational(ORIENT3D, a, b, c, d);
}
double gmp_rational_incircle(const double* a, const double* b, const double* c,
                             const double* d) {
  return gmp_rational(INCIRCLE, a, b, c, d);
}
double gmp_rational_insphere(const double* a, const double* b, const double* c, const double* d,
                             const double* e) {
  return gmp_rational(INSPHERE, a, b, c, d, e);
}
const char* gmp_rational_unavailable_reason() {
  return gmp_oracle_available() ? nullptr : "not built: GMP was not found";
}

[[maybe_unused]] const char* indirect_unavailable_reason() {
#if SHEWCHUK_COMPARATOR_INDIRECT_USES_AVX2 && (defined(__GNUC__) || defined(__clang__))
  // Built with -mavx2 -mfma, upstream's default for x86-64.
  if (!__builtin_cpu_supports("avx2") || !__builtin_cpu_supports("fma")) {
    return "this CPU has no AVX2 and FMA instructions";
  }
#endif
  return nullptr;
}

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
#if SHEWCHUK_HAVE_DEFAULT_FLAGS_REFERENCE
    if (default_flags_unavailable_reason() == nullptr) shewchuk_default_flags_exactinit();
#endif
#if SHEWCHUK_HAVE_COMPARATOR_GEOGRAM
    shewchuk_comparator_geogram_initialize();
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
      {"library",
       "the library (src/predicates.cpp): the 1996 filter, the exact integer stage, then the "
       "1996 adaptive stages",
       shewchuk_orient2d, shewchuk_orient3d, shewchuk_incircle, shewchuk_insphere,
       always_available},
      {"library_hooked", "the library's test-only build with stage recording",
       shewchuk_hooked_orient2d, shewchuk_hooked_orient3d, shewchuk_hooked_incircle,
       shewchuk_hooked_insphere, always_available},
      {"library_expansion_only",
       "the library built without its integer stage (test only): bit-identical port of the "
       "reference",
       shewchuk_expansion_only_orient2d, shewchuk_expansion_only_orient3d,
       shewchuk_expansion_only_incircle, shewchuk_expansion_only_insphere, always_available},
      {"library_portable",
       "the library with the portable 64x64->128 multiplication forced in its integer stage "
       "(test only)",
       shewchuk_portable_orient2d, shewchuk_portable_orient3d, shewchuk_portable_incircle,
       shewchuk_portable_insphere, always_available},
#if SHEWCHUK_HAVE_CONTRACTED_REFERENCE
      {"reference_contracted",
       "NEGATIVE CONTROL: the same file built with -O2 -ffp-contract=fast; must be wrong",
       contracted_orient2d, contracted_orient3d, contracted_incircle, contracted_insphere,
       contracted_unavailable_reason},
#else
      {"reference_contracted", "NEGATIVE CONTROL: not built for this compiler", missing_orient2d,
       missing_orient3d, missing_orient3d, missing_insphere, contracted_unavailable_reason},
#endif
#if SHEWCHUK_HAVE_DEFAULT_FLAGS_REFERENCE
      {"reference_default_flags",
       "NEGATIVE CONTROL: the same file built with -O2 and the compiler's default "
       "floating-point flags; must be wrong",
       default_flags_orient2d, default_flags_orient3d, default_flags_incircle,
       default_flags_insphere, default_flags_unavailable_reason},
#else
      {"reference_default_flags", "NEGATIVE CONTROL: not built for this compiler",
       missing_orient2d, missing_orient3d, missing_orient3d, missing_insphere,
       default_flags_unavailable_reason},
#endif
      // --- Comparators: third-party code. Wrong signs are reported, not hidden.
      {"gmp_rational",
       "COMPARATOR: exact evaluation with GMP rationals (the suite's second oracle); a slow "
       "reference point",
       gmp_rational_orient2d, gmp_rational_orient3d, gmp_rational_incircle,
       gmp_rational_insphere, gmp_rational_unavailable_reason, true},
#if SHEWCHUK_HAVE_COMPARATOR_GEOGRAM
      // PCK has no incircle or insphere without symbolic perturbation; its
      // in-circle test does not depend on the orientation of the triangle; its
      // in-sphere test requires a tetrahedron that is positive for its own
      // orient_3d, which is negative in the 1996 convention.
      {"geogram",
       "COMPARATOR: Geogram PCK, orient_2d, orient_3d, in_circle_2d_SOS, in_sphere_3d_SOS "
       "(" SHEWCHUK_COMPARATOR_GEOGRAM_BUILD ")",
       shewchuk_comparator_geogram_orient2d, shewchuk_comparator_geogram_orient3d,
       shewchuk_comparator_geogram_incircle, shewchuk_comparator_geogram_insphere,
       always_available, true, {false, false, true, true}, {false, false, true, false},
       {0, 0, 0, -1}},
#endif
#if SHEWCHUK_HAVE_COMPARATOR_CGAL
      {"cgal",
       "COMPARATOR: CGAL Exact_predicates_inexact_constructions_kernel "
       "(" SHEWCHUK_COMPARATOR_CGAL_BUILD ")",
       shewchuk_comparator_cgal_orient2d, shewchuk_comparator_cgal_orient3d,
       shewchuk_comparator_cgal_incircle, shewchuk_comparator_cgal_insphere, always_available,
       true},
#endif
#if SHEWCHUK_HAVE_COMPARATOR_INDIRECT
      {"indirect",
       "COMPARATOR: Indirect_Predicates, the direct predicates on doubles "
       "(" SHEWCHUK_COMPARATOR_INDIRECT_BUILD ")",
       shewchuk_comparator_indirect_orient2d, shewchuk_comparator_indirect_orient3d,
       shewchuk_comparator_indirect_incircle, shewchuk_comparator_indirect_insphere,
       indirect_unavailable_reason, true},
#endif
#if SHEWCHUK_HAVE_COMPARATOR_GEOMETRIC_PREDICATES
      {"geometric_predicates",
       "COMPARATOR: GeometricPredicates, predicates::adaptive "
       "(" SHEWCHUK_COMPARATOR_GEOMETRIC_PREDICATES_BUILD ")",
       shewchuk_comparator_geometric_predicates_orient2d,
       shewchuk_comparator_geometric_predicates_orient3d,
       shewchuk_comparator_geometric_predicates_incircle,
       shewchuk_comparator_geometric_predicates_insphere, always_available, true},
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
