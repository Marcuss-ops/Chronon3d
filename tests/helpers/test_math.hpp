#pragma once
// Shared test math helpers — included by multiple scene camera test files
// to avoid ODR violations in unity builds.

#include <cmath>

namespace chronon3d::test {

inline bool approx(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) <= tol;
}

} // namespace chronon3d::test
