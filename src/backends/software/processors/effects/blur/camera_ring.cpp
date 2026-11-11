#include "camera_ring_processor.hpp"

#include <chronon3d/effects/camera_ring.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chronon3d::renderer {
namespace {

Color sample_bilinear_clamped(const Framebuffer& source, float u, float v) {
    const float max_x = static_cast<float>(source.width() - 1);
    const float max_y = static_cast<float>(source.height() - 1);
    const float x = std::clamp(u * static_cast<float>(source.width()) - 0.5f,
                               0.0f, max_x);
    const float y = std::clamp(v * static_cast<float>(source.height()) - 0.5f,
                               0.0f, max_y);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, source.width() - 1);
    const int y1 = std::min(y0 + 1, source.height() - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const Color c00 = source.get_pixel(x0, y0);
    const Color c10 = source.get_pixel(x1, y0);
    const Color c01 = source.get_pixel(x0, y1);
    const Color c11 = source.get_pixel(x1, y1);
    const auto mix = [tx, ty](float a, float b, float c, float d) {
        return (a * (1.0f - tx) + b * tx) * (1.0f - ty) +
               (c * (1.0f - tx) + d * tx) * ty;
    };
    return Color{
        mix(c00.r, c10.r, c01.r, c11.r),
        mix(c00.g, c10.g, c01.g, c11.g),
        mix(c00.b, c10.b, c01.b, c11.b),
        mix(c00.a, c10.a, c01.a, c11.a),
    };
}

// A 2x2 pixel box approximates the sensor's spatial integration before the
// Poisson reconstruction. Without it a 16-tap stochastic kernel leaves visible
// checker-frequency residue at the Nyquist limit; the safe-zone bypass is
// unaffected and therefore remains an exact copy.
Color sample_prefiltered_clamped(const Framebuffer& source, float u, float v) {
    const float du = 0.5f / static_cast<float>(source.width());
    const float dv = 0.5f / static_cast<float>(source.height());
    const Color a = sample_bilinear_clamped(source, u - du, v - dv);
    const Color b = sample_bilinear_clamped(source, u + du, v - dv);
    const Color c = sample_bilinear_clamped(source, u - du, v + dv);
    const Color d = sample_bilinear_clamped(source, u + du, v + dv);
    return Color{(a.r + b.r + c.r + d.r) * 0.25f,
                 (a.g + b.g + c.g + d.g) * 0.25f,
                 (a.b + b.b + c.b + d.b) * 0.25f,
                 (a.a + b.a + c.a + d.a) * 0.25f};
}

}  // namespace

void apply_camera_ring(Framebuffer& fb, const CameraRingParams& params,
                       const std::optional<raster::BBox>& clip) {
    params.validate();
    if (fb.width() <= 0 || fb.height() <= 0)
        throw std::invalid_argument("CameraRing target surface is invalid");

    if (params.samples <= 1 || params.max_blur_radius <= 0.0f) return;

    int x0 = 0, y0 = 0, x1 = fb.width(), y1 = fb.height();
    if (clip) {
        x0 = std::clamp(clip->x0, 0, fb.width());
        y0 = std::clamp(clip->y0, 0, fb.height());
        x1 = std::clamp(clip->x1, 0, fb.width());
        y1 = std::clamp(clip->y1, 0, fb.height());
    }
    if (x1 <= x0 || y1 <= y0) return;

    const Framebuffer source = fb;
    const float width = static_cast<float>(source.width());
    const float height = static_cast<float>(source.height());
    const float aspect = width / height;
    const std::uint32_t taps = params.samples;

    for (int y = y0; y < y1; ++y) {
        Color* dst = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / width;
            const float v = (static_cast<float>(y) + 0.5f) / height;
            const float coc = effects::camera_ring_coc(params, u, v);
            if (coc <= 0.001f) {
                dst[x] = source.get_pixel(x, y);
                continue;
            }

            const float radius = coc * params.max_blur_radius;
            const float radius_x = radius * params.anamorphic_stretch;
            const float radius_y = radius * aspect;

            const float delta_x = u - params.center.x;
            const float delta_y = v - params.center.y;
            const float delta_len = std::sqrt(delta_x * delta_x + delta_y * delta_y);
            const float chroma_amount = coc * coc * params.chroma_dispersion;
            float chroma_x = 0.0f, chroma_y = 0.0f;
            if (delta_len > 1.0e-6f) {
                chroma_x = delta_x / delta_len * chroma_amount;
                chroma_y = delta_y / delta_len * chroma_amount * aspect;
            }

            const float rotation =
                effects::camera_ring_pixel_jitter(static_cast<std::uint32_t>(x),
                                                  static_cast<std::uint32_t>(y)) *
                6.28318530718f;
            Color accumulated{0.0f, 0.0f, 0.0f, 0.0f};
            for (std::uint32_t tap = 0; tap < taps; ++tap) {
                const Vec2 offset = effects::camera_ring_tap_offset(tap, taps, rotation);
                const float base_u = u + offset.x * radius_x;
                const float base_v = v + offset.y * radius_y;
                const Color green = sample_prefiltered_clamped(source, base_u, base_v);
                // With dispersion disabled, all three channels share the
                // same sample. Reuse it instead of doing two more 2x2
                // prefiltered bilinear gathers for every tap.
                const Color red = chroma_amount == 0.0f ? green
                    : sample_prefiltered_clamped(
                        source, base_u + chroma_x, base_v + chroma_y);
                const Color blue = chroma_amount == 0.0f ? green
                    : sample_prefiltered_clamped(
                        source, base_u - chroma_x, base_v - chroma_y);
                accumulated.r += red.r;
                accumulated.g += green.g;
                accumulated.b += blue.b;
                accumulated.a += green.a;
            }
            const float inv = 1.0f / static_cast<float>(taps);
            dst[x] = Color{accumulated.r * inv, accumulated.g * inv,
                           accumulated.b * inv, accumulated.a * inv};
        }
    }
}

}  // namespace chronon3d::renderer
