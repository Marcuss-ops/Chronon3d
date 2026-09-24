#include "checkerboard.hpp"

#include <chronon3d/effects/color_contract.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {
namespace {

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) return x < edge0 ? 0.0f : 1.0f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float axis_square_wave(float coordinate, float cell_size, float feather) {
    const float scaled = coordinate / cell_size;
    const float cell_floor = std::floor(scaled);
    const float local = scaled - cell_floor;
    const auto cell = static_cast<long long>(cell_floor);
    const float sign = (cell & 1LL) == 0 ? 1.0f : -1.0f;
    const float width = std::clamp(feather / cell_size, 0.0f, 1.0f);
    if (width <= 0.0f) return sign;

    const float distance = std::min(local, 1.0f - local);
    if (distance >= width * 0.5f) return sign;
    const float toward_neighbor = 1.0f - smoothstep(0.0f, width * 0.5f, distance);
    return sign * (1.0f - toward_neighbor) - sign * toward_neighbor;
}

} // namespace

void apply_checkerboard(Framebuffer& fb, const CheckerboardParams& params,
                        const std::optional<raster::BBox>& clip) {
    const int width = fb.width();
    const int height = fb.height();
    int x0 = 0, x1 = width, y0 = 0, y1 = height;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, width); x1 = std::clamp(clip->x1, 0, width);
        y0 = std::clamp(clip->y0, 0, height); y1 = std::clamp(clip->y1, 0, height);
    }
    if (!std::isfinite(params.size) || params.size < 1.0e-4f) return;
    const float feather = std::isfinite(params.feather)
        ? std::clamp(params.feather, 0.0f, params.size) : 0.0f;

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        const float wy = axis_square_wave(static_cast<float>(y) + 0.5f, params.size, feather);
        for (int x = x0; x < x1; ++x) {
            const float wx = axis_square_wave(static_cast<float>(x) + 0.5f, params.size, feather);
            const float t = 0.5f - 0.5f * wx * wy;
            const Color& a = params.color_a;
            const Color& b = params.color_b;
            const float alpha = std::lerp(a.a, b.a, t);
            row[x] = color::premultiply_rgb({std::lerp(a.r, b.r, t),
                                             std::lerp(a.g, b.g, t),
                                             std::lerp(a.b, b.b, t)}, alpha);
        }
    }
}

} // namespace chronon3d::renderer
