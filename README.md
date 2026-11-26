# shewchuk-revisited

Revisiting Jonathan Shewchuk's
[robust geometric predicates](https://www.cs.cmu.edu/~quake/robust.html)
(`orient2d`, `orient3d`, `incircle`, `insphere`) with modern C++: faster, and
still exact.

## Installation and use

Requires CMake 3.20 or newer and a C++20 compiler. Add the repository as a
subdirectory, or fetch it:

```cmake
include(FetchContent)
FetchContent_Declare(shewchuk_revisited
  GIT_REPOSITORY https://github.com/alecjacobson/shewchuk-revisited.git
  GIT_TAG main)
FetchContent_MakeAvailable(shewchuk_revisited)  # or: add_subdirectory(shewchuk-revisited)

target_link_libraries(my_target PRIVATE shewchuk::predicates)
```

```cpp
#include <shewchuk/predicates.h>
#include <cstdio>

int main() {
  const double a[2] = {0.0, 0.0}, b[2] = {1.0, 0.0}, c[2] = {0.5, 1e-30};
  // > 0: counterclockwise, < 0: clockwise, == 0: exactly collinear.
  const double side = shewchuk::orient2d(a, b, c);
  std::printf("%s\n", side > 0 ? "left" : side < 0 ? "right" : "collinear");
}
```

The header declares `orient2d`, `orient3d`, `incircle` and `insphere`, as
`shewchuk::name` in C++ and `shewchuk_name` in C. Only the sign of the result
is exact. There is no initialization call and no global state. Code written
for the 1996 `predicates.c` can include `<shewchuk/predicates_1996.h>` to get
the original names (`exactinit()` does nothing).

The library must be built by its own CMake files: they compile it with the
floating-point flags that exactness depends on (no contraction into fused
multiply-add, no fast-math), and copying the source into a build with other
flags can silently give wrong signs. Your own code can use any flags. Tested:
GCC and clang on x86-64 and arm64, Apple clang on arm64, MSVC on x86-64.

## Performance

Against the original 1996 `predicates.c`, same compiler and optimization
level, on an Apple M4. "Near-degenerate" inputs are almost collinear,
coplanar, cocircular or cospherical points, where the original falls back to
its slow exact stages; "hardest cases" need its last stage.

<!-- performance-table:begin -->
**Apple clang 21**, CPU cycles per call, 1996 code → this library:

| | random points | 2% near-degenerate | 60% near-degenerate | hardest cases |
|---|--:|--:|--:|--:|
| `orient2d` | 34.2 → 5.0 (**6.8x**) | 35.0 → 6.2 (**5.7x**) | 55.8 → 41.4 (**1.3x**) | 220 → 79.1 (**2.8x**) |
| `orient3d` | 13.6 → 11.0 (**1.2x**) | 26.6 → 14.0 (**1.9x**) | 378 → 82.2 (**4.6x**) | 3,403 → 203 (**16.7x**) |
| `incircle` | 15.3 → 11.8 (**1.3x**) | 55.2 → 14.0 (**3.9x**) | 1,175 → 71.3 (**16.5x**) | 15,487 → 204 (**75.8x**) |
| `insphere` | 40.8 → 30.7 (**1.3x**) | 203 → 36.4 (**5.6x**) | 4,723 → 172 (**27.4x**) | 31,731 → 626 (**50.7x**) |

**GCC 16**, CPU cycles per call, 1996 code → this library:

| | random points | 2% near-degenerate | 60% near-degenerate | hardest cases |
|---|--:|--:|--:|--:|
| `orient2d` | 44.2 → 5.0 (**8.8x**) | 45.0 → 6.2 (**7.3x**) | 57.9 → 41.7 (**1.4x**) | 225 → 110 (**2.0x**) |
| `orient3d` | 39.8 → 11.3 (**3.5x**) | 52.0 → 14.9 (**3.5x**) | 371 → 99.8 (**3.7x**) | 3,422 → 234 (**14.6x**) |
| `incircle` | 39.8 → 12.1 (**3.3x**) | 79.0 → 14.9 (**5.3x**) | 1,143 → 84.5 (**13.5x**) | 15,713 → 254 (**61.8x**) |
| `insphere` | 70.8 → 32.5 (**2.2x**) | 235 → 38.4 (**6.1x**) | 4,814 → 190 (**25.3x**) | 32,372 → 718 (**45.1x**) |
<!-- performance-table:end -->

Both implementations return the exact sign on every input tested (over 15
million cases checked against exact arithmetic). The numbers are medians over
10 fresh processes and are regenerated from
[`bench/results`](bench/results) by `tools/readme_table.py`; the method, every
workload and a third compiler are in
[docs/benchmarking.md](docs/benchmarking.md). No x86-64 measurements yet.
