#include "light_rays.hpp"

#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/backends/software/sampling/sampler2d.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

void apply_light_rays(Framebuffer& fb, const LightRaysParams& params,
                      EffectScratchResources& scratch,
                      const std::optional<raster::BBox>& clip) {
    const int width = fb.width();
    const int height = fb.height();
    const float max_length = std::isfinite(params.length) ? std::max(params.length, 0.0f) : 0.0f;
    const float density = std::isfinite(params.density)
        ? std::clamp(params.density, 0.0f, 8.0f) : 0.0f;
    if (width <= 0 || height <= 0 || max_length <= 0.0f || density <= 0.0f) return;

    int x0 = 0, x1 = width, y0 = 0, y1 = height;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, width); x1 = std::clamp(clip->x1, 0, width);
        y0 = std::clamp(clip->y0, 0, height); y1 = std::clamp(clip->y1, 0, height);
    }

    scratch.ensure_size(width, height);
    Framebuffer& source = *scratch.framebuffer;
    source.blit(fb, 0, 0);
    const sampling::Sampler2D sampler(source, sampling::EdgeMode::Transparent);
    const float origin_x = (std::isfinite(params.origin.x) ? params.origin.x : 0.5f) * width;
    const float origin_y = (std::isfinite(params.origin.y) ? params.origin.y : 0.5f) * height;
    const float decay = std::isfinite(params.decay) ? std::clamp(params.decay, 0.0f, 1.0f) : 0.0f;
    constexpr float decay_distance_px = 8.0f;
    const float samples_per_pixel = 1.0f / 12.0f;

    for (int y = y0; y < y1; ++y) {
        Color* dst = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float dx = px - origin_x;
            const float dy = py - origin_y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= 1.0e-6f || distance > max_length) continue;

            const int steps = std::clamp(static_cast<int>(std::ceil(distance * samples_per_pixel)) + 1,
                                         2, 64);
            float accumulated = 0.0f;
            for (int i = 0; i < steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps - 1);
                const Color sample = sampler.bilinear(origin_x + dx * t,
                                                      origin_y + dy * t);
                const auto rgb = color::unpremultiply_rgb(sample);
                const float luminance = std::max(0.0f,
                    (0.2126f * rgb.r + 0.7152f * rgb.g + 0.0722f * rgb.b) * sample.a);
                const float distance_from_origin = distance * t;
                accumulated += luminance * std::pow(decay, distance_from_origin / decay_distance_px);
            }

            const float energy = std::clamp(accumulated * density * 4.0f /
                                             static_cast<float>(steps), 0.0f, 1.0f);
            if (energy <= 0.0f) continue;
            const Color original = source.get_pixel(x, y);
            const float ray_alpha = energy * std::clamp(params.color.a, 0.0f, 1.0f);
            dst[x] = Color{original.r + params.color.r * ray_alpha,
                           original.g + params.color.g * ray_alpha,
                           original.b + params.color.b * ray_alpha,
                           original.a + ray_alpha * (1.0f - original.a)};
        }
    }
}

} // namespace chronon3d::renderer
