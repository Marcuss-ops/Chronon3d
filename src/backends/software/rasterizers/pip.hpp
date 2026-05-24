#pragma once

#include <chronon3d/math/vec2.hpp>
#include <vector>

namespace chronon3d::renderer {

enum class PipMode {
    Scalar,
    Simd
};

PipMode get_pip_mode();

bool point_in_polygon_even_odd(Vec2 p, const std::vector<Vec2>& poly);

#if defined(__AVX2__)
bool point_in_polygon_avx2(Vec2 p, const std::vector<Vec2>& poly);
#else
inline bool point_in_polygon_avx2(Vec2 p, const std::vector<Vec2>& poly) {
    return point_in_polygon_even_odd(p, poly);
}
#endif

} // namespace chronon3d::renderer
