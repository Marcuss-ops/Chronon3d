#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace chronon3d::renderer::detail {

/// Iterate the same integer-row scanline coverage used by the card rasterizers.
/// Callers supply blending because projected cards and material cards have
/// different alpha conventions.
template <typename PixelWriter>
void for_each_card_triangle_pixel(Framebuffer& fb, std::array<Vec2, 3> vertices,
                                  PixelWriter&& write_pixel) {
    auto& v0 = vertices[0];
    auto& v1 = vertices[1];
    auto& v2 = vertices[2];
    if (v0.y > v1.y) std::swap(v0, v1);
    if (v1.y > v2.y) std::swap(v1, v2);
    if (v0.y > v1.y) std::swap(v0, v1);

    const int y0 = std::max(0, static_cast<int>(std::ceil(v0.y)));
    const int y2 = std::min(fb.height() - 1, static_cast<int>(std::floor(v2.y)));
    if (y0 > y2) return;
    const float height = v2.y - v0.y;
    if (height < 0.5f) return;

    for (int y = y0; y <= y2; ++y) {
        const float fy = static_cast<float>(y);
        const float t_long = (fy - v0.y) / height;
        const float x_long = v0.x + (v2.x - v0.x) * t_long;
        float x_short;
        if (fy < v1.y) {
            const float segment = v1.y - v0.y;
            x_short = segment > 0.5f
                ? v0.x + (v1.x - v0.x) * ((fy - v0.y) / segment) : v1.x;
        } else {
            const float segment = v2.y - v1.y;
            x_short = segment > 0.5f
                ? v1.x + (v2.x - v1.x) * ((fy - v1.y) / segment) : v2.x;
        }
        const int x0 = std::max(0, static_cast<int>(std::ceil(std::min(x_long, x_short))));
        const int x1 = std::min(fb.width() - 1,
                                static_cast<int>(std::floor(std::max(x_long, x_short))));
        if (x0 > x1) continue;
        Color* row = fb.pixels_row(y);
        for (int x = x0; x <= x1; ++x) write_pixel(row[x]);
    }
}

} // namespace chronon3d::renderer::detail
