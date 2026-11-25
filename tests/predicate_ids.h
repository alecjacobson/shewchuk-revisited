// Shared vocabulary of the test suite: predicate ids and input layout.
//
// A test case is a flat array of coordinates, point after point:
//   orient2d  ax ay bx by cx cy               (6)
//   orient3d  ax ay az ... dx dy dz           (12)
//   incircle  ax ay ... dx dy                 (8)
//   insphere  ax ay az ... ex ey ez           (15)
#pragma once

namespace shewchuk_test {

enum Pred { ORIENT2D = 0, ORIENT3D = 1, INCIRCLE = 2, INSPHERE = 3 };
inline constexpr int NUM_PREDS = 4;
inline constexpr int MAX_COORDS = 15;
inline constexpr int NUM_COORDS[NUM_PREDS] = {6, 12, 8, 15};
inline constexpr int NUM_POINTS[NUM_PREDS] = {3, 4, 4, 5};
inline constexpr int DIMENSION[NUM_PREDS] = {2, 3, 2, 3};
inline constexpr const char* PRED_NAME[NUM_PREDS] = {"orient2d", "orient3d", "incircle",
                                                      "insphere"};

// -1, 0 or +1. NaN maps to 0, so callers that must not accept NaN check
// std::isfinite separately.
inline int sign_of(double x) { return (x > 0.0) - (x < 0.0); }

}  // namespace shewchuk_test
