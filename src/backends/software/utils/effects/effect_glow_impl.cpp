// ---------------------------------------------------------------------------
// effect_glow_impl.cpp — Glow effect implementation
//
// Extracted from effect_stack.cpp to keep the dispatcher focused on
// orchestration. Contains glow-specific helpers and apply_glow_effect().
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "effects_internal.hpp"
#include "effect_helpers.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

namespace chronon3d {
namespace renderer {

namespace {

struct GlowLayerPass {
    float radius_scale;
    float intensity_scale;
    float buffer_scale;
};

[[nodiscard]] BlendMode glow_blend_mode(const GlowParams& params) {
    if (params.quality == GlowQuality::SkiaLike || !params.layers.empty()) {
        return params.blend;
    }
    return params.additive ? BlendMode::Add : BlendMode::Screen;
}

[[nodiscard]] float glow_trigger_from_pixel(const Color& c, const GlowParams& params) {
    if (c.a <= 0.0f) return 0.0f;

    float trigger = c.a;
    if (params.threshold > 0.0f) {
        const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        const float denom = std::max(1e-4f, 1.0f - params.threshold);
        trigger = (lum - params.threshold) / denom;
    }

    trigger = std::clamp(trigger, 0.0f, 1.0f);
    if (params.softness != 1.0f) {
        trigger = std::pow(trigger, std::max(0.01f, params.softness));
    }
    return trigger;
}

[[nodiscard]] Color glow_color_from_trigger(float trigger, const GlowParams& params, float intensity_scale) {
    const float amount = trigger * params.intensity * intensity_scale;
    return {
        params.color.r * amount,
        params.color.g * amount,
        params.color.b * amount,
        params.color.a * amount
    };
}

void accumulate_glow_pass(Framebuffer& dst, const Framebuffer& src, const GlowParams& params) {
    const float falloff = std::max(0.01f, params.falloff);
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

void accumulate_scaled_glow_pass(Framebuffer& dst, const Framebuffer& src, const GlowParams& params, float scale) {
    const float falloff = std::max(0.01f, params.falloff);
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

} // anonymous namespace

void apply_glow_effect(
    Framebuffer& fb,
    const GlowParams& p,
    const std::optional<raster::BBox>& clip
) {
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

} // namespace renderer
} // namespace chronon3d
