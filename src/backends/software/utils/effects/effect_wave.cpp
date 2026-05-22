// ---------------------------------------------------------------------------
// effect_wave.cpp — Fake-3D wave deformation effect
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "effects_internal.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

namespace {

constexpr f32 kTau = 6.28318530718f;

void deform_horizontal(const Framebuffer& src, Framebuffer& dst,
                       const Fake3DWaveParams& p, float time_seconds, bool shadow_pass) {
    const i32 w      = src.width();
    const i32 h      = src.height();
    const i32 slices = std::clamp(p.slices, 1, 256);
    const f32 cx     = static_cast<f32>(w) * 0.5f;

    for (i32 s = 0; s < slices; ++s) {
        const i32 y0   = (s * h) / slices;
        const i32 y1   = ((s + 1) * h) / slices;
        const i32 yy1  = std::max(y0 + 1, y1);
        const f32 norm  = (static_cast<f32>(s) + 0.5f) / static_cast<f32>(slices);
        const f32 angle = time_seconds * p.speed + norm * p.frequency * kTau + p.phase;
        const f32 wave  = std::sin(angle);
        const f32 depth = std::cos(angle);
        const f32 dx    = wave * p.amplitude_px;
        const f32 scale_x = safe_scale(depth * p.depth_px, p.perspective);
        f32 shade = 1.0f + std::max(0.0f, depth) * p.highlight;
        shade *= 1.0f - std::max(0.0f, -depth) * p.side_darkening;
        shade = clamp01(shade);

        for (i32 y = y0; y < yy1; ++y) {
            Color* dst_row = dst.pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                const f32 src_x = ((static_cast<f32>(x) - cx) / scale_x) + cx - dx;
                const Color c = src.sample_bilinear(src_x + 0.5f, static_cast<f32>(y) + 0.5f);
                if (c.a <= 0.0f) continue;
                if (shadow_pass) {
                    dst_row[x] = {p.shadow_color.r, p.shadow_color.g, p.shadow_color.b, c.a * p.shadow_color.a};
                } else {
                    dst_row[x] = {c.r * shade, c.g * shade, c.b * shade, c.a};
                }
            }
        }
    }
}

void deform_vertical(const Framebuffer& src, Framebuffer& dst,
                     const Fake3DWaveParams& p, float time_seconds, bool shadow_pass) {
    const i32 w      = src.width();
    const i32 h      = src.height();
    const i32 slices = std::clamp(p.slices, 1, 256);
    const f32 cy     = static_cast<f32>(h) * 0.5f;

    for (i32 s = 0; s < slices; ++s) {
        const i32 x0   = (s * w) / slices;
        const i32 x1   = ((s + 1) * w) / slices;
        const i32 xx1  = std::max(x0 + 1, x1);
        const f32 norm  = (static_cast<f32>(s) + 0.5f) / static_cast<f32>(slices);
        const f32 angle = time_seconds * p.speed + norm * p.frequency * kTau + p.phase;
        const f32 wave  = std::sin(angle);
        const f32 depth = std::cos(angle);
        const f32 dy    = wave * p.amplitude_px;
        const f32 scale_y = safe_scale(depth * p.depth_px, p.perspective);
        f32 shade = 1.0f + std::max(0.0f, depth) * p.highlight;
        shade *= 1.0f - std::max(0.0f, -depth) * p.side_darkening;
        shade = clamp01(shade);

        for (i32 x = x0; x < xx1; ++x) {
            for (i32 y = 0; y < h; ++y) {
                Color* dst_row = dst.pixels_row(y);
                const f32 src_y = ((static_cast<f32>(y) - cy) / scale_y) + cy - dy;
                const Color c = src.sample_bilinear(static_cast<f32>(x) + 0.5f, src_y + 0.5f);
                if (c.a <= 0.0f) continue;
                if (shadow_pass) {
                    dst_row[x] = {p.shadow_color.r, p.shadow_color.g, p.shadow_color.b, c.a * p.shadow_color.a};
                } else {
                    dst_row[x] = {c.r * shade, c.g * shade, c.b * shade, c.a};
                }
            }
        }
    }
}

void deform_wave(const Framebuffer& src, Framebuffer& dst,
                 const Fake3DWaveParams& p, float time_seconds, bool shadow_pass) {
    switch (p.axis) {
    case WaveAxis::Horizontal:
        deform_horizontal(src, dst, p, time_seconds, shadow_pass);
        break;
    case WaveAxis::Vertical:
        deform_vertical(src, dst, p, time_seconds, shadow_pass);
        break;
    }
}

} // namespace

void apply_fake_3d_wave(Framebuffer& fb, const Fake3DWaveParams& params, float time_seconds) {
    if (!params.shadow_enabled && params.amplitude_px <= 0.0f && params.depth_px <= 0.0f) {
        return;
    }

    const Framebuffer src = fb;
    auto body_fb = acquire_temp_framebuffer(src.width(), src.height());
    deform_wave(src, *body_fb, params, time_seconds, false);

    if (params.shadow_enabled && params.shadow_color.a > 0.0f) {
        auto shadow_fb = acquire_temp_framebuffer(src.width(), src.height());
        deform_wave(src, *shadow_fb, params, time_seconds, true);
        if (params.shadow_offset.x != 0.0f || params.shadow_offset.y != 0.0f) {
            auto shifted_fb = acquire_temp_framebuffer(src.width(), src.height());
            const i32 ox = static_cast<i32>(std::round(params.shadow_offset.x));
            const i32 oy = static_cast<i32>(std::round(params.shadow_offset.y));
            for (i32 y = 0; y < shadow_fb->height(); ++y) {
                const Color* shadow_row = shadow_fb->pixels_row(y);
                for (i32 x = 0; x < shadow_fb->width(); ++x) {
                    const Color c = shadow_row[x];
                    if (c.a <= 0.0f) continue;
                    const i32 dx = x + ox;
                    const i32 dy = y + oy;
                    if (dx >= 0 && dx < shifted_fb->width() && dy >= 0 && dy < shifted_fb->height()) {
                        Color* shifted_row = shifted_fb->pixels_row(dy);
                        shifted_row[dx] = c;
                    }
                }
            }
            shadow_fb = std::move(shifted_fb);
        }
        if (params.shadow_blur > 0.0f) {
            apply_blur(*shadow_fb, params.shadow_blur);
        }
        apply_shadow_buffer(*body_fb, *shadow_fb);
    }

    fb = std::move(*body_fb);
}

} // namespace renderer
} // namespace chronon3d
