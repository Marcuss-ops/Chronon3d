// ---------------------------------------------------------------------------
// radial_blur.cpp — Scalar Radial Blur implementation (A6)
// ---------------------------------------------------------------------------
//
// Tap offsets depend on the pixel's vector from the blur centre. The reusable
// table therefore stores only per-sample values (u, cos, sin, weight); dx/dy
// are derived in the pixel loop. This keeps the exact zoom+spin sampling
// semantics while removing the former vector allocation and trigonometry from
// every pixel.

#include "radial_blur.hpp"
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

namespace chronon3d::renderer {

struct RadialTap {
    float u{0.0f};
    float cos_angle{1.0f};
    float sin_angle{0.0f};
    float weight{0.0f};
};

struct AlphaCentroid {
    float x{0.0f};
    float y{0.0f};
    float sum{0.0f};
};

[[nodiscard]] static AlphaCentroid compute_alpha_centroid(const Framebuffer& fb) {
    AlphaCentroid out;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            const float a = row[x].a;
            out.sum += a;
            out.x += a * static_cast<float>(x);
            out.y += a * static_cast<float>(y);
        }
    }
    if (out.sum > 0.0f) {
        out.x /= out.sum;
        out.y /= out.sum;
    }
    return out;
}

[[nodiscard]] static int normalized_sample_count(int samples) noexcept {
    return std::max(2, samples);
}

static void build_radial_taps(
    float amount, int samples,
    std::vector<EffectScratchResources::RadialTap>& taps) {
    const int n = normalized_sample_count(samples);
    const float inv_n = 1.0f / static_cast<float>(n);
    const float max_angle_rad = amount * (std::numbers::pi_v<float> / 180.0f);
    taps.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) * inv_n * 2.0f - 1.0f;
        const float angle = u * max_angle_rad;
        taps[static_cast<std::size_t>(i)] = EffectScratchResources::RadialTap{
            u, std::cos(angle), std::sin(angle), inv_n};
    }
}

void apply_radial_blur(
    Framebuffer& fb,
    float center_x, float center_y,
    float amount,
    int samples,
    EffectScratchResources& scratch,
    const std::optional<raster::BBox>& clip)
{
    if (amount <= 0.0f || samples <= 1) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    if (y0 >= y1 || x0 >= x1) return;

    const float cx_px = center_x * static_cast<float>(std::max(0, w - 1)) + 0.5f;
    const float cy_px = center_y * static_cast<float>(std::max(0, h - 1)) + 0.5f;
    const float blur_scale = amount * 0.1f;

    // The scratch workspace is REQUIRED (reference, not pointer): the old
    // `scratch == nullptr` branch allocated a heap framebuffer per call, i.e.
    // one per effect per frame in the render loop.
    scratch.ensure_size(w, h);
    auto& taps = scratch.radial_taps;
    if (scratch.radial_samples != samples ||
        scratch.radial_amount != amount) {
        // Contract: spin angle is u * amount degrees (see the original A6
        // algorithm doc and the native radial_blur.comp header). Only the
        // zoom extent uses blur_scale = amount * 0.1.
        build_radial_taps(amount, samples, taps);
        scratch.radial_samples = samples;
        scratch.radial_amount = amount;
    }

    Framebuffer* temp = scratch.framebuffer.get();
    temp->blit(fb, 0, 0);
    const AlphaCentroid source_centroid = compute_alpha_centroid(*temp);
    // FIXING.md H-01 — inline clamp bilinear on raw Color* (no Sampler2D vtable/indirection per pixel).
    // Sampler2D is header-inline but still carries m_source pointer + edge-mode switch per fetch;
    // inlining the clamp+lerp here removes the per-tap fetch call overhead on the hottest loop.
    const int tw = temp->width();
    const int th = temp->height();
    auto clamp_bilinear = [&](float sx, float sy) noexcept -> Color {
        const float u = sx - 0.5f, v = sy - 0.5f;
        const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
        const int x1 = x0 + 1, y1 = y0 + 1;
        const float tx = u - static_cast<float>(x0), ty = v - static_cast<float>(y0);
        auto fetch_clamp = [&](int x, int y) noexcept -> Color {
            x = std::clamp(x, 0, tw - 1); y = std::clamp(y, 0, th - 1);
            return temp->get_pixel(x, y);
        };
        const Color c00 = fetch_clamp(x0, y0), c10 = fetch_clamp(x1, y0);
        const Color c01 = fetch_clamp(x0, y1), c11 = fetch_clamp(x1, y1);
        const Color r0{ c00.r + (c10.r - c00.r) * tx, c00.g + (c10.g - c00.g) * tx, c00.b + (c10.b - c00.b) * tx, c00.a + (c10.a - c00.a) * tx };
        const Color r1{ c01.r + (c11.r - c01.r) * tx, c01.g + (c11.g - c01.g) * tx, c01.b + (c11.b - c01.b) * tx, c01.a + (c11.a - c01.a) * tx };
        return Color{ r0.r + (r1.r - r0.r) * ty, r0.g + (r1.g - r0.g) * ty, r0.b + (r1.b - r0.b) * ty, r0.a + (r1.a - r0.a) * ty };
    };

    for (int y = y0; y < y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        const float py = static_cast<float>(y);
        for (int x = x0; x < x1; ++x) {
            const float px = static_cast<float>(x);
            const float dx_c = px - cx_px;
            const float dy_c = py - cy_px;
            if (std::abs(dx_c) < 0.001f && std::abs(dy_c) < 0.001f) {
                dst_row[x] = temp->get_pixel(x, y);
                continue;
            }

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (const auto& tap : taps) {
                const float zoom_x = dx_c * tap.u * blur_scale;
                const float zoom_y = dy_c * tap.u * blur_scale;
                const float rot_x = cx_px + dx_c * tap.cos_angle - dy_c * tap.sin_angle;
                const float rot_y = cy_px + dx_c * tap.sin_angle + dy_c * tap.cos_angle;
                const float sx = px + zoom_x + (rot_x - px);
                const float sy = py + zoom_y + (rot_y - py);
                const Color c = clamp_bilinear(sx, sy);
                r += c.r * tap.weight;
                g += c.g * tap.weight;
                b += c.b * tap.weight;
                a += c.a * tap.weight;
            }
            dst_row[x] = Color{r, g, b, a};
        }
    }

    const AlphaCentroid blurred_centroid = compute_alpha_centroid(fb);
    const float shift_x = blurred_centroid.sum > 0.0f
        ? (blurred_centroid.x - source_centroid.x) : 0.0f;
    const float shift_y = blurred_centroid.sum > 0.0f
        ? (blurred_centroid.y - source_centroid.y) : 0.0f;
    const bool source_is_at_blur_center =
        std::abs(source_centroid.x - cx_px) < 1e-3f &&
        std::abs(source_centroid.y - cy_px) < 1e-3f;
    if (!source_is_at_blur_center &&
        (std::abs(shift_x) > 1e-3f || std::abs(shift_y) > 1e-3f)) {
        Framebuffer* recentered = scratch.framebuffer_b.get();
        // Reuse the same clamp helper pattern (source is fb, shifted by shift_x/y).
        const int sw = fb.width(), sh = fb.height();
        for (int y = 0; y < h; ++y) {
            Color* dst_row = recentered->pixels_row(y);
            for (int x = 0; x < w; ++x) {
                const float sx = static_cast<float>(x) + shift_x;
                const float sy = static_cast<float>(y) + shift_y;
                const float u = sx - 0.5f, v = sy - 0.5f;
                const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
                const int x1 = x0 + 1, y1 = y0 + 1;
                const float tx = u - static_cast<float>(x0), ty = v - static_cast<float>(y0);
                auto fetch = [&](int fx, int fy) noexcept -> Color {
                    fx = std::clamp(fx, 0, sw - 1); fy = std::clamp(fy, 0, sh - 1);
                    return fb.get_pixel(fx, fy);
                };
                const Color c00 = fetch(x0, y0), c10 = fetch(x1, y0);
                const Color c01 = fetch(x0, y1), c11 = fetch(x1, y1);
                const Color r0{ c00.r + (c10.r - c00.r) * tx, c00.g + (c10.g - c00.g) * tx, c00.b + (c10.b - c00.b) * tx, c00.a + (c10.a - c00.a) * tx };
                const Color r1{ c01.r + (c11.r - c01.r) * tx, c01.g + (c11.g - c01.g) * tx, c01.b + (c11.b - c01.b) * tx, c01.a + (c11.a - c01.a) * tx };
                dst_row[x] = Color{ r0.r + (r1.r - r0.r) * ty, r0.g + (r1.g - r0.g) * ty, r0.b + (r1.b - r0.b) * ty, r0.a + (r1.a - r0.a) * ty };
            }
        }
        fb.blit(*recentered, 0, 0);
    }
}

} // namespace chronon3d::renderer
