// ---------------------------------------------------------------------------
// glow_pipeline.cpp — Unified glow/bloom pipeline
//
// This file replaces effect_glow_impl.cpp and effect_bloom_impl.cpp as the
// single home for the Layer and Bloom mode implementations. The two legacy
// effect files now forward through run_glow_pipeline() so callers stay
// unchanged. `draw_glow` (the 5 expanded-shape node primitive) remains a
// separate fast path in effect_stack.cpp — no blur, cheaper for small radii.
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "effects_internal.hpp"
#include "effect_helpers.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace chronon3d::renderer {

namespace {

// ── Layer-mode helpers (moved from effect_glow_impl.cpp) ─────────────

[[nodiscard]] BlendMode glow_blend_mode(const GlowPipeline& p) {
    if (p.quality == GlowQuality::SkiaLike || !p.layers.empty()) {
        return p.blend;
    }
    return p.additive ? BlendMode::Add : BlendMode::Screen;
}

[[nodiscard]] float glow_trigger_from_pixel(const Color& c, const GlowPipeline& p) {
    if (c.a <= 0.0f) return 0.0f;

    float trigger = c.a;
    if (p.threshold > 0.0f) {
        const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        const float denom = std::max(1e-4f, 1.0f - p.threshold);
        trigger = (lum - p.threshold) / denom;
    }

    trigger = std::clamp(trigger, 0.0f, 1.0f);
    if (p.softness != 1.0f) {
        trigger = std::pow(trigger, std::max(0.01f, p.softness));
    }
    return trigger;
}

[[nodiscard]] Color glow_color_from_trigger(float trigger, const GlowPipeline& p, float intensity_scale) {
    const float amount = trigger * p.intensity * intensity_scale;
    return {
        p.color.r * amount,
        p.color.g * amount,
        p.color.b * amount,
        p.color.a * amount
    };
}

void accumulate_glow_pass(Framebuffer& dst, const Framebuffer& src, const GlowPipeline& p) {
    const float falloff = std::max(0.01f, p.falloff);
    for (i32 y = 0; y < dst.height(); ++y) {
        Color* dst_row = dst.pixels_row(y);
        const Color* src_row = src.pixels_row(y);
        for (i32 x = 0; x < dst.width(); ++x) {
            Color g = src_row[x];
            if (g.a <= 0.0f) continue;

            const float shaped = std::pow(std::clamp(g.a, 0.0f, 1.0f), falloff);
            if (g.a > 0.0f) {
                const float ratio = shaped / g.a;
                g.r *= ratio;
                g.g *= ratio;
                g.b *= ratio;
            }
            g.a = shaped;

            Color& acc = dst_row[x];
            acc.r += g.r;
            acc.g += g.g;
            acc.b += g.b;
            acc.a = std::max(acc.a, g.a);
        }
    }
}

void accumulate_scaled_glow_pass(Framebuffer& dst, const Framebuffer& src, const GlowPipeline& p, float scale) {
    const float falloff = std::max(0.01f, p.falloff);
    for (i32 y = 0; y < dst.height(); ++y) {
        Color* dst_row = dst.pixels_row(y);
        const float sy = (static_cast<float>(y) + 0.5f) * scale;
        for (i32 x = 0; x < dst.width(); ++x) {
            const float sx = (static_cast<float>(x) + 0.5f) * scale;
            Color g = src.sample(sx, sy, SamplingMode::Bilinear);
            if (g.a <= 0.0f) continue;

            const float shaped = std::pow(std::clamp(g.a, 0.0f, 1.0f), falloff);
            if (g.a > 0.0f) {
                const float ratio = shaped / g.a;
                g.r *= ratio;
                g.g *= ratio;
                g.b *= ratio;
            }
            g.a = shaped;

            Color& acc = dst_row[x];
            acc.r += g.r;
            acc.g += g.g;
            acc.b += g.b;
            acc.a = std::max(acc.a, g.a);
        }
    }
}

struct GlowLayerPass {
    float radius_scale;
    float intensity_scale;
    float buffer_scale;
};

[[nodiscard]] f32 glow_effect_extent(const GlowPipeline& p) {
    f32 base_radius = std::max(0.0f, p.radius);
    if (!p.layers.empty()) {
        f32 max_layer_r = 0.0f;
        for (const auto& l : p.layers) {
            max_layer_r = std::max(max_layer_r, l.radius);
        }
        base_radius = max_layer_r;
    }
    const f32 radius = base_radius * std::max(0.0f, p.spread);
    return radius + 4.0f;
}

// ── run_layer_mode (moved from apply_glow_effect) ────────────────────

void run_layer_mode(Framebuffer& fb, const GlowPipeline& p,
                    const std::optional<raster::BBox>& clip) {
    const i32 w = fb.width(), h = fb.height();
    const float extent = glow_effect_extent(p);
    auto effect_clip = expand_effect_clip(clip, w, h, extent);

    i32 x_min = 0, x_max = w;
    i32 y_min = 0, y_max = h;
    if (effect_clip) {
        x_min = std::max(0, effect_clip->x0);
        x_max = std::min(w, effect_clip->x1);
        y_min = std::max(0, effect_clip->y0);
        y_max = std::min(h, effect_clip->y1);
    }
    const float base_radius = std::max(0.0f, p.radius) * std::max(0.0f, p.spread);
    // Generous padding = ceil(radius * 4) to avoid rectangular clipping of the
    // glow blur at the framebuffer boundary. The old helper blur_padding_for_radius
    // returns only ceil(radius)+2, which is insufficient for multi-layer glow and
    // produces a visible rectangular cutoff (the #1 cause of the "fog box" look).
    const i32 blur_pad = std::max(
        blur_padding_for_radius(base_radius),
        static_cast<i32>(std::ceil(base_radius * 4.0f)) + 4
    );
    const i32 x_min_pad = std::max(0, x_min - blur_pad);
    const i32 x_max_pad = std::min(w, x_max + blur_pad);
    const i32 y_min_pad = std::max(0, y_min - blur_pad);
    const i32 y_max_pad = std::min(h, y_max + blur_pad);
    const i32 roi_w = x_max_pad - x_min_pad;
    const i32 roi_h = y_max_pad - y_min_pad;
    if (roi_w <= 0 || roi_h <= 0) return;

    auto source_fb = acquire_temp_framebuffer(roi_w, roi_h);
    source_fb->clear({0,0,0,0});
    // ── Source extraction with alpha threshold ──────────────────────────
    // Copy source pixels into the temp buffer, applying an alpha floor:
    // any pixel with 0 < alpha < 16/255 is zeroed out.  Text rasterisers
    // can leave sub-pixel alpha (~1-3/255) in the padding area around
    // glyph bounding boxes, which the blur would amplify into a visible
    // rectangular fog.  This threshold eliminates that without affecting
    // legitimate anti-aliased edges (≥ 32/255 at the glyph boundary).
    static constexpr float kAlphaFloor = 16.0f / 255.0f;
    for (i32 y = 0; y < roi_h; ++y) {
        const i32 sy = y + y_min_pad;
        const Color* src_row = fb.pixels_row(sy);
        Color* source_row = source_fb->pixels_row(y);
        for (i32 x = 0; x < roi_w; ++x) {
            Color c = src_row[x_min_pad + x];
            if (c.a > 0.0f && c.a < kAlphaFloor) {
                c = Color::transparent();
            }
            source_row[x] = c;
        }
    }

    // ── Debug: save source framebuffer ─────────────────────────────────
    if (std::getenv("CHRONON_DEBUG_GLOW")) {
        save_png(*source_fb, "output/debug_glow_source.png");
    }

    auto glow_acc_fb = acquire_temp_framebuffer(roi_w, roi_h);
    glow_acc_fb->clear({0,0,0,0});
    auto pass_fb = acquire_temp_framebuffer(roi_w, roi_h);
    pass_fb->clear({0,0,0,0});

    std::vector<GlowLayerPass> passes;
    if (!p.layers.empty()) {
        for (const auto& layer : p.layers) {
            passes.push_back({layer.radius, layer.opacity, std::clamp(layer.scale, 0.05f, 1.0f)});
        }
    } else {
        const float outer_downscale = std::clamp(p.outer_downscale, 0.05f, 1.0f);
        passes.push_back({0.10f * base_radius, std::max(0.0f, p.core_strength), 1.0f});
        passes.push_back({0.35f * base_radius, std::max(0.0f, p.aura_strength), 1.0f});
        passes.push_back({1.00f * base_radius, std::max(0.0f, p.bloom_strength), outer_downscale});
    }

    for (const auto& pass : passes) {
        const float r = (!p.layers.empty()) ? pass.radius_scale * std::max(0.0f, p.spread) : pass.radius_scale;
        if (r < 0.5f || pass.intensity_scale <= 0.0f) continue;

        if (pass.buffer_scale >= 0.999f) {
            pass_fb->clear({0,0,0,0});
            for (i32 y = 0; y < roi_h; ++y) {
                Color* pass_row = pass_fb->pixels_row(y);
                const Color* source_row = source_fb->pixels_row(y);
                for (i32 x = 0; x < roi_w; ++x) {
                    const float trigger = glow_trigger_from_pixel(source_row[x], p);
                    if (trigger > 0.0f) {
                        pass_row[x] = glow_color_from_trigger(trigger, p, pass.intensity_scale);
                    }
                }
            }

            apply_blur(*pass_fb, r, std::nullopt, 1);
            accumulate_glow_pass(*glow_acc_fb, *pass_fb, p);

            // Debug: save each glow pass before accumulation
            if (std::getenv("CHRONON_DEBUG_GLOW")) {
                static int pass_counter = 0;
                save_png(*pass_fb, "output/debug_glow_pass_" + std::to_string(pass_counter++) + ".png");
            }
        } else {
            const i32 small_w = std::max(1, static_cast<i32>(std::ceil(static_cast<float>(roi_w) * pass.buffer_scale)));
            const i32 small_h = std::max(1, static_cast<i32>(std::ceil(static_cast<float>(roi_h) * pass.buffer_scale)));
            auto small_fb = acquire_temp_framebuffer(small_w, small_h);
            small_fb->clear({0,0,0,0});

            for (i32 y = 0; y < small_h; ++y) {
                Color* small_row = small_fb->pixels_row(y);
                const float sy = (static_cast<float>(y) + 0.5f) / pass.buffer_scale;
                for (i32 x = 0; x < small_w; ++x) {
                    const float sx = (static_cast<float>(x) + 0.5f) / pass.buffer_scale;
                    const Color c = source_fb->sample(sx, sy, SamplingMode::Bilinear);
                    const float trigger = glow_trigger_from_pixel(c, p);
                    if (trigger > 0.0f) {
                        small_row[x] = glow_color_from_trigger(trigger, p, pass.intensity_scale);
                    }
                }
            }

            apply_blur(*small_fb, std::max(0.5f, r * pass.buffer_scale), std::nullopt, 1);
            accumulate_scaled_glow_pass(*glow_acc_fb, *small_fb, p, pass.buffer_scale);
        }
    }

    // ── Debug: save accumulated glow before compositing ────────────────
    if (std::getenv("CHRONON_DEBUG_GLOW")) {
        save_png(*glow_acc_fb, "output/debug_glow_accumulated.png");
    }

    for (i32 y = 0; y < roi_h; ++y) {
        const i32 dy = y + y_min_pad;
        if (dy < 0 || dy >= h) continue;
        Color* dst_row = fb.pixels_row(dy);
        const Color* glow_row = glow_acc_fb->pixels_row(y);
        const Color* source_row = source_fb->pixels_row(y);
        for (i32 x = 0; x < roi_w; ++x) {
            const i32 dx = x + x_min_pad;
            if (dx < 0 || dx >= w) continue;

            if (p.preserve_source) {
                dst_row[dx] = compositor::blend(source_row[x], glow_row[x], BlendMode::Normal);
            } else {
                dst_row[dx] = compositor::blend(glow_row[x], source_row[x], glow_blend_mode(p));
            }
        }
    }
}

// ── run_bloom_mode (moved from apply_bloom_effect) ───────────────────

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

} // anonymous namespace

} // namespace chronon3d::renderer

// ── from() converters (GlowPipeline lives in chronon3d, not renderer) ─

namespace chronon3d {

GlowPipeline GlowPipeline::from(const GlowParams& p) {
    GlowPipeline out;
    out.mode = GlowPipeline::Mode::Layer;
    out.color = p.color;
    out.radius = p.radius;
    out.intensity = p.intensity;
    out.threshold = p.threshold;
    out.spread = p.spread;
    out.softness = p.softness;
    out.falloff = p.falloff;
    out.core_strength = p.core_strength;
    out.aura_strength = p.aura_strength;
    out.bloom_strength = p.bloom_strength;
    out.outer_downscale = p.outer_downscale;
    out.preserve_source = p.preserve_source;
    out.additive = p.additive;
    out.blend = p.blend;
    out.layers = p.layers;
    out.quality = p.quality;
    return out;
}

GlowPipeline GlowPipeline::from(const BloomParams& p) {
    GlowPipeline out;
    out.mode = GlowPipeline::Mode::Bloom;
    out.radius = p.radius;
    out.intensity = p.intensity;
    out.threshold = p.threshold;
    out.color = Color{1, 1, 1, 1};   // bloom uses the source pixel colors
    out.preserve_source = true;       // additive on top of source
    return out;
}

GlowPipeline GlowPipeline::from(const DropShadowParams& p) {
    GlowPipeline out;
    out.mode = GlowPipeline::Mode::Shadow;
    out.color = p.color;
    out.radius = p.radius;
    out.intensity = 1.0f;
    // Shadow offset (Vec2) is not yet threaded through GlowPipeline.
    // apply_shadow_effect() remains in effect_shadow_impl.cpp until a
    // future refactor adds an offset field to GlowPipeline.
    return out;
}

} // namespace chronon3d

// ── run_glow_pipeline() — main entry point (lives in chronon3d::renderer)

namespace chronon3d::renderer {

void run_glow_pipeline(Framebuffer& fb, const GlowPipeline& p,
                       const std::optional<raster::BBox>& clip) {
    switch (p.mode) {
    case GlowPipeline::Mode::Layer:
        run_layer_mode(fb, p, clip);
        break;
    case GlowPipeline::Mode::Bloom:
        run_bloom_mode(fb, p, clip);
        break;
    case GlowPipeline::Mode::Shadow:
        // Reserved for future apply_shadow_effect routing. For now
        // apply_shadow_effect remains in effect_shadow_impl.cpp.
        break;
    }
}

} // namespace chronon3d::renderer
