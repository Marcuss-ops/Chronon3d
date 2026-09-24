#include "four_color_gradient.hpp"

#include <chronon3d/effects/color_contract.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

void apply_four_color_gradient(Framebuffer& fb, const FourColorGradientParams& params,
                               const std::optional<raster::BBox>& clip) {
    const int width = fb.width();
    const int height = fb.height();
    int x0 = 0, x1 = width, y0 = 0, y1 = height;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, width); x1 = std::clamp(clip->x1, 0, width);
        y0 = std::clamp(clip->y0, 0, height); y1 = std::clamp(clip->y1, 0, height);
    }

    const float smoothness = std::isfinite(params.blend_smoothness)
        ? std::max(params.blend_smoothness, 0.0f) : 0.0f;
    const float regularizer = std::max(smoothness * smoothness, 1.0e-8f);
    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        for (int x = x0; x < x1; ++x) {
            const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            std::array<float, 4> weights{};
            float total = 0.0f;
            for (std::size_t i = 0; i < params.points.size(); ++i) {
                const Vec2 point = params.points[i];
                const float dx = nx - point.x;
                const float dy = ny - point.y;
                const float distance2 = dx * dx + dy * dy;
                if (distance2 <= 1.0e-12f) {
                    weights.fill(0.0f);
                    weights[i] = 1.0f;
                    total = 1.0f;
                    break;
                }
                weights[i] = 1.0f / (distance2 + regularizer);
                total += weights[i];
            }
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (std::size_t i = 0; i < params.colors.size(); ++i) {
                const float weight = weights[i] / total;
                const Color& color = params.colors[i];
                r += color.r * weight;
                g += color.g * weight;
                b += color.b * weight;
                a += color.a * weight;
            }
            row[x] = color::premultiply_rgb({r, g, b}, a);
        }
    }
}

} // namespace chronon3d::renderer
