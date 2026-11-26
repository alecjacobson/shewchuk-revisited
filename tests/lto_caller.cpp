// A caller of the library compiled with hostile floating-point flags
// (-O3 -ffast-math) and with link-time optimization. See test_lto_caller.cpp.
//
// The build defines SHEWCHUK_LTO_CALLER (the name of the function defined
// here) and renames the four predicates to those of one of the library's
// link-time-optimized test builds (tests/CMakeLists.txt). The link-time
// optimizer sees this file and that build of the library together and may
// inline one into the other.
#include "shewchuk/predicates.h"

extern "C" double SHEWCHUK_LTO_CALLER(int predicate, const double* c) {
  switch (predicate) {
    case 0: return shewchuk::orient2d(c, c + 2, c + 4);
    case 1: return shewchuk::orient3d(c, c + 3, c + 6, c + 9);
    case 2: return shewchuk::incircle(c, c + 2, c + 4, c + 6);
    default: return shewchuk::insphere(c, c + 3, c + 6, c + 9, c + 12);
  }
}
