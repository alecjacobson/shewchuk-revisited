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
is exact. It is exact for every finite input, whatever the magnitudes of the
coordinates (the 1996 code silently returns wrong signs when an intermediate
overflows or underflows), provided the caller has not changed the rounding
mode or enabled flush-to-zero; a NaN or infinite coordinate gives NaN.
`shewchuk::orient2d_sign` etc. (`shewchuk_orient2d_sign` in C) return the sign
as an `int`: -1, 0 or +1, and `SHEWCHUK_SIGN_NOT_FINITE` for a NaN or infinite
coordinate. There is no initialization call and no global state. Code written
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
| `orient2d` | 34.0 → 5.0 (**6.8x**) | 34.8 → 6.2 (**5.7x**) | 55.8 → 41.6 (**1.3x**) | 219 → 78.8 (**2.8x**) |
| `orient3d` | 13.6 → 11.0 (**1.2x**) | 26.6 → 14.2 (**1.9x**) | 379 → 80.7 (**4.7x**) | 3,392 → 194 (**17.5x**) |
| `incircle` | 15.3 → 11.9 (**1.3x**) | 55.5 → 14.1 (**3.9x**) | 1,184 → 69.8 (**17.0x**) | 15,448 → 200 (**77.1x**) |
| `insphere` | 40.8 → 31.7 (**1.3x**) | 202 → 37.4 (**5.4x**) | 4,712 → 175 (**26.9x**) | 31,637 → 632 (**50.1x**) |

**GCC 16**, CPU cycles per call, 1996 code → this library:

| | random points | 2% near-degenerate | 60% near-degenerate | hardest cases |
|---|--:|--:|--:|--:|
| `orient2d` | 44.1 → 5.0 (**8.8x**) | 44.9 → 6.2 (**7.3x**) | 57.9 → 41.3 (**1.4x**) | 226 → 116 (**2.0x**) |
| `orient3d` | 39.9 → 11.0 (**3.6x**) | 52.1 → 14.7 (**3.6x**) | 371 → 99.9 (**3.7x**) | 3,433 → 237 (**14.5x**) |
| `incircle` | 39.8 → 12.1 (**3.3x**) | 78.8 → 15.2 (**5.2x**) | 1,144 → 86.2 (**13.3x**) | 15,813 → 257 (**61.4x**) |
| `insphere` | 70.8 → 32.9 (**2.2x**) | 235 → 38.9 (**6.0x**) | 4,827 → 195 (**24.7x**) | 32,365 → 723 (**44.8x**) |
<!-- performance-table:end -->

Against other exact implementations (CGAL 6.2.1 `EPICK`, Geogram PCK,
Indirect_Predicates, GeometricPredicates), measured the same way
([docs/comparators.md](docs/comparators.md)): this library is the fastest on
mixtures of random and near-degenerate points and on the hardest cases, by 6x
to 10x on the hardest `orient3d`, `incircle` and `insphere` inputs. It is not
the fastest everywhere: on random points Indirect_Predicates' `orient2d` is
about 20% faster with Apple clang (level with GCC), and when coordinates span
very different magnitudes CGAL's `incircle` is about 5x faster.

Both implementations return the exact sign on every input tested inside the
1996 code's exponent range (over 15 million cases checked against exact
arithmetic); outside it only this library does. The numbers are medians over
10 fresh processes and are regenerated from
[`bench/results`](bench/results) by `tools/readme_table.py`; the method, every
workload and a third compiler are in
[docs/benchmarking.md](docs/benchmarking.md). No x86-64 measurements yet.
