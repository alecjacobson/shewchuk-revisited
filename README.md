# shewchuk-revisited

Revisiting Jonathan Shewchuk's
[robust geometric predicates](https://www.cs.cmu.edu/~quake/robust.html)
(`orient2d`, `orient3d`, `incircle`, `insphere`) with modern C++: faster, and
still exact.

> **Status:** early. The library is currently a bit-identical port of the 1996
> code; the speedups are still to come. The roadmap and every idea being tried
> are tracked in the [issues](../../issues); every change lands through a
> [pull request](../../pulls?q=is%3Apr) with its exactness evidence and
> before/after numbers.

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

_Arrives with the first benchmark pull request. No numbers are claimed here
until they are measured and reproducible from this repository._
