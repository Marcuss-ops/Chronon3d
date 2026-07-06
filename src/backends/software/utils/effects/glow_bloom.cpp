// ---------------------------------------------------------------------------
// glow_bloom.cpp — Bloom-mode pipeline runner extracted from glow_pipeline.cpp
//
// FASE 7 Step 1 — extracted run_bloom_mode() from the anonymous namespace
// into chronon3d::renderer scope.
// ---------------------------------------------------------------------------

#include "glow_bloom.hpp"
#include "effects_internal.hpp"
#include "effect_helpers.hpp"
#include "../render_effects_processor.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

void run_bloom_mode(Framebuffer& fb, const GlowPipeline& p,
                    const std::optional<raster::BBox>& clip) {
    const i32 w = fb.width(), h = fb.height();
    auto effect_clip = expand_effect_clip(clip, w, h, p.radius);
    i32 x_min = 0, x_max = w;
    i32 y_min = 0, y_max = h;
    if (effect_clip) {
        x_min = std::max(0, effect_clip->x0);
        x_max = std::min(w, effect_clip->x1);
        y_min = std::max(0, effect_clip->y0);
        y_max = std::min(h, effect_clip->y1);
    }
    const i32 blur_pad = blur_padding_for_radius(p.radius);
    const i32 x_min_pad = std::max(0, x_min - blur_pad);
    const i32 x_max_pad = std::min(w, x_max + blur_pad);
    const i32 y_min_pad = std::max(0, y_min - blur_pad);
    const i32 y_max_pad = std::min(h, y_max + blur_pad);
    const i32 roi_w = x_max_pad - x_min_pad;
    const i32 roi_h = y_max_pad - y_min_pad;
    auto bright_fb = acquire_temp_framebuffer(roi_w, roi_h);
    bright_fb->clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    auto& bright = *bright_fb;
    if (roi_w > 0 && roi_h > 0) {
        for (i32 y = 0; y < roi_h; ++y) {
            const i32 sy = y + y_min_pad;
            if (sy < y_min || sy >= y_max) continue;
            const Color* src_row   = fb.pixels_row(sy);
            Color*       bright_row = bright.pixels_row(y);
            for (i32 x = 0; x < roi_w; ++x) {
                const i32 sx = x + x_min_pad;
                if (sx < x_min || sx >= x_max) continue;
                const Color c = src_row[sx];
                const f32 lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                if (lum > p.threshold && c.a > 0.0f) {
                    const f32 denom = std::max(1.0f - p.threshold, 0.001f);
                    const f32 excess = std::clamp((lum - p.threshold) / denom, 0.0f, 1.0f);
                    bright_row[x] = {c.r * excess, c.g * excess, c.b * excess, c.a};
                }
            }
        }
        if (p.radius > 0.0f) apply_blur(bright, p.radius, std::nullopt, 1);
        for (i32 y = 0; y < roi_h; ++y) {
            const i32 dy = y + y_min_pad;
            if (dy < 0 || dy >= h) continue;
            Color*       dst_row    = fb.pixels_row(dy);
            const Color* bright_row = bright.pixels_row(y);
            for (i32 x = 0; x < roi_w; ++x) {
                const i32 dx = x + x_min_pad;
                if (dx < 0 || dx >= w) continue;
                const Color b = bright_row[x];
                if (b.a <= 0.0f) continue;
                // NaN/Inf guard: skip contaminated bloom values to prevent
                // framebuffer corruption.
                if (std::isnan(b.r) || std::isnan(b.g) || std::isnan(b.b) ||
                    std::isinf(b.r) || std::isinf(b.g) || std::isinf(b.b)) {
                    continue;
                }
                const float bloom_r = b.r * p.intensity;
                const float bloom_g = b.g * p.intensity;
                const float bloom_b = b.b * p.intensity;
                dst_row[dx].r += bloom_r;
                dst_row[dx].g += bloom_g;
                dst_row[dx].b += bloom_b;
                // Alpha unchanged: additive RGB contribution does not
                // affect coverage (consistent with compositing convention).
            }
        }
    }
}

} // namespace chronon3d::renderer
