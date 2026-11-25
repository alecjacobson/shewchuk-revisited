// A tiny assertion helper for the test executables (no framework needed).
#pragma once

#include <cstdio>

namespace shewchuk_test {

inline int& failure_count() {
  static int count = 0;
  return count;
}

// Records a failure (and keeps going) if `condition` is false.
#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ::shewchuk_test::failure_count()++;                                   \
    }                                                                       \
  } while (0)

// Prints the verdict and returns the process exit status.
inline int finish(const char* what) {
  if (failure_count() == 0) {
    std::printf("PASS: %s\n", what);
    return 0;
  }
  std::printf("FAIL: %s (%d failed checks)\n", what, failure_count());
  return 1;
}

}  // namespace shewchuk_test
