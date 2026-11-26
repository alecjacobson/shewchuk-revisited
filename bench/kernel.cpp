#include "kernel.h"

#include <cstdint>
#include <cstring>

namespace shewchuk_bench {
namespace {

using shewchuk_test::Implementation;

// Always zero, but the compiler cannot know: used to make an input address
// depend on the previous result without changing which input is read.
volatile std::uint64_t g_opaque_zero = 0;

inline long long sign_of(double x) { return (x > 0.0) - (x < 0.0); }

// One loop per predicate and mode. `CALL` uses p, the first coordinate of the
// current case.
#define SHEWCHUK_BENCH_LOOPS(name, num_coords, CALL)                                  \
  long long throughput_##name(const Implementation& impl, const double* cases,       \
                              std::size_t count) {                                    \
    const auto fn = impl.name;                                                        \
    long long checksum = 0;                                                           \
    for (std::size_t i = 0; i < count; i++) {                                         \
      const double* p = cases + i * (num_coords);                                     \
      checksum += sign_of(CALL);                                                      \
    }                                                                                 \
    return checksum;                                                                  \
  }                                                                                   \
  long long latency_##name(const Implementation& impl, const double* cases,          \
                           std::size_t count) {                                       \
    const auto fn = impl.name;                                                        \
    const std::uint64_t mask = g_opaque_zero;                                         \
    long long checksum = 0;                                                           \
    std::size_t offset = 0;                                                           \
    for (std::size_t i = 0; i < count; i++) {                                         \
      const double* p = cases + i * (num_coords) + offset;                            \
      const double result = CALL;                                                     \
      std::uint64_t bits;                                                             \
      std::memcpy(&bits, &result, sizeof bits);                                       \
      offset = static_cast<std::size_t>(bits & mask);                                 \
      checksum += sign_of(result);                                                    \
    }                                                                                 \
    return checksum;                                                                  \
  }

SHEWCHUK_BENCH_LOOPS(orient2d, 6, fn(p, p + 2, p + 4))
SHEWCHUK_BENCH_LOOPS(orient3d, 12, fn(p, p + 3, p + 6, p + 9))
SHEWCHUK_BENCH_LOOPS(incircle, 8, fn(p, p + 2, p + 4, p + 6))
SHEWCHUK_BENCH_LOOPS(insphere, 15, fn(p, p + 3, p + 6, p + 9, p + 12))

}  // namespace

long long run_kernel(const Implementation& implementation, shewchuk_test::Pred pred, Mode mode,
                     const double* cases, std::size_t count) {
  const bool latency = mode == Mode::Latency;
  switch (pred) {
    case shewchuk_test::ORIENT2D:
      return latency ? latency_orient2d(implementation, cases, count)
                     : throughput_orient2d(implementation, cases, count);
    case shewchuk_test::ORIENT3D:
      return latency ? latency_orient3d(implementation, cases, count)
                     : throughput_orient3d(implementation, cases, count);
    case shewchuk_test::INCIRCLE:
      return latency ? latency_incircle(implementation, cases, count)
                     : throughput_incircle(implementation, cases, count);
    default:
      return latency ? latency_insphere(implementation, cases, count)
                     : throughput_insphere(implementation, cases, count);
  }
}

}  // namespace shewchuk_bench
