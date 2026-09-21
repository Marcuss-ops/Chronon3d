// ---------------------------------------------------------------------------
// glow_pipeline.cpp — Unified glow/bloom pipeline
//
// This file is the single home for the Layer and Bloom mode implementations.
// Glow and bloom are rendered exclusively by the blur/accumulation pipeline
// implemented here: run_layer_mode and run_bloom_mode both live in this
// translation unit.
// ---------------------------------------------------------------------------

#include "render_effects_processor.hpp"
#include "effects_internal.hpp"
#include "effect_helpers.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace chronon3d::renderer {

// TBB grain size for the per-pass loops inside the glow pipeline.
// Smaller than effect_blur.cpp because the per-row work here is
// cheaper (one induction step per pixel) and we want enough tasks
// to keep every TBB worker busy on the typical SpecialName ROI.
static constexpr int kGlowTbbGrain = 32;

// Falloff LUT.  The single `inline constexpr int kFalloffLutSize = 257;`
// definition lives in include/chronon3d/effects/glow_pipeline.hpp
// (included above). build_glow_accumulator populates one per call (the
// falloff exponent is parameter-dependent) and passes its address into
// accumulate_glow_pass / accumulate_scaled_glow_pass.  256+1 entries
// give full 8-bit quantization of the trigger alpha (indices 0..255),
// plus the endpoint 256 used when we accidentally look at the top bit.
// (Redundant cpp-side definition removed — fc5db7e7 + PR1 824652a4 both
// dropped it as part of separate ODR-purpose refactors.)
// Build the per-call falloff LUT.  `falloff = 1.0f` is an identity map
// (the trigger is unchanged), and `falloff != 1.0f` produces the shaped
// alpha lookup we use inside the per-pixel accumulation loop.
//
// Exposed at namespace `chronon3d::renderer` scope (NOT anon) so benchmarks
// and golden tests can call it directly.  Do not use from production code.
void build_falloff_lut(float falloff, float (&lut)[kFalloffLutSize]) noexcept {
    // Exponents below one amplify low-alpha noise and turn the outer halo
    // into fog. Canonical glow falloff is monotonic and never boosts
    // coverage, so values below one resolve to the neutral exponent.
    const float safe_falloff = std::max(1.0f, falloff);
    for (int i = 0; i < kFalloffLutSize; ++i) {
        // Index 256 stays at 1.0 regardless, so a strict >255.0 packed alpha
        // can't accidentally index a different shape.
        lut[i] = std::pow(static_cast<float>(i) / 255.0f, safe_falloff);
    }
}



namespace {

[[nodiscard]] inline float lookup_shaped(float alpha, const float* lut) noexcept {
    if (alpha >= 1.0f) return 1.0f;
    if (alpha <= 0.0f) return 0.0f;
    return lut[static_cast<int>(alpha * 255.0f)];
}

// ── Layer-mode helpers ──────────────────────────────────────────────

[[nodiscard]] BlendMode glow_blend_mode(const GlowPipeline& p) {
    return p.blend;
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

[[nodiscard]] Color glow_color_from_trigger(float trigger, const GlowPipeline& p, float intensity_scale, const Color& tint = {0,0,0,0}) {
    const Color& src = (tint.a > 0.0f) ? tint : p.color;
    const float amount = trigger * p.intensity * intensity_scale;
    return {
        src.r * amount,
        src.g * amount,
        src.b * amount,
        src.a * amount
    };
}

struct GlowLayerPass {
    float radius_scale;
    float intensity_scale;
    float buffer_scale;
    Color  tint{0, 0, 0, 0};  // per-layer color override (alpha > 0 means use this)
};

// High-quality glow blur. Framebuffer pixels use premultiplied RGB, so the
// filter first unpremultiplies only to perform the sRGB -> linear conversion,
// filters premultiplied linear RGB, then converts back to premultiplied sRGB.
// Keeping the result premultiplied is essential: transparent glyph edges must
// not leak dark RGB into the halo and the compositor must not multiply alpha
// a second time.
void apply_gaussian_blur_linear_premultiplied(Framebuffer& fb, float sigma) {
    const int w = fb.width();
    const int h = fb.height();
    if (w <= 0 || h <= 0 || sigma <= 0.0f) return;

    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
    std::vector<float> kernel(static_cast<std::size_t>(radius * 2 + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float weight = std::exp(-0.5f * (static_cast<float>(i * i) / (sigma * sigma)));
        kernel[static_cast<std::size_t>(i + radius)] = weight;
        sum += weight;
    }
    for (float& weight : kernel) weight /= sum;

    auto horizontal = acquire_temp_framebuffer(w, h);
    horizontal->clear(Color::transparent());
    tbb::parallel_for(tbb::blocked_range<int>(0, h, kGlowTbbGrain),
                      [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            Color* dst = horizontal->pixels_row(y);
            for (int x = 0; x < w; ++x) {
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = x + k;
                    if (sx < 0 || sx >= w) continue;
                    const Color c = fb.pixels_row(y)[sx];
                    const float weight = kernel[static_cast<std::size_t>(k + radius)];
                    const float alpha = std::clamp(c.a, 0.0f, 1.0f);
                    if (alpha <= 1.0e-8f) continue;

                    // c.rgb is premultiplied. Convert the unpremultiplied
                    // display-referred color to linear, then premultiply it
                    // again before accumulating.
                    const float inv_alpha = 1.0f / alpha;
                    const float srgb_r = std::max(0.0f, c.r * inv_alpha);
                    const float srgb_g = std::max(0.0f, c.g * inv_alpha);
                    const float srgb_b = std::max(0.0f, c.b * inv_alpha);
                    const auto srgb_to_linear = [](float v) {
                        return v <= 0.04045f ? v / 12.92f
                                             : std::pow((v + 0.055f) / 1.055f, 2.4f);
                    };
                    r += srgb_to_linear(srgb_r) * alpha * weight;
                    g += srgb_to_linear(srgb_g) * alpha * weight;
                    b += srgb_to_linear(srgb_b) * alpha * weight;
                    a += alpha * weight;
                }
                dst[x] = {r, g, b, a};
            }
        }
    });

    tbb::parallel_for(tbb::blocked_range<int>(0, h, kGlowTbbGrain),
                      [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            Color* dst = fb.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int sy = y + k;
                    if (sy < 0 || sy >= h) continue;
                    const Color c = horizontal->pixels_row(sy)[x];
                    const float weight = kernel[static_cast<std::size_t>(k + radius)];
                    r += c.r * weight;
                    g += c.g * weight;
                    b += c.b * weight;
                    a += c.a * weight;
                }
                if (a <= 1.0e-8f) {
                    dst[x] = Color::transparent();
                    continue;
                }
                const float inv_alpha = 1.0f / a;
                const auto linear_to_srgb = [](float value) {
                    value = std::max(0.0f, value);
                    return value <= 0.0031308f
                        ? value * 12.92f
                        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
                };
                const float out_a = std::clamp(a, 0.0f, 1.0f);
                // Convert the filtered premultiplied linear values back to
                // premultiplied sRGB, not straight RGB.
                dst[x] = {
                    linear_to_srgb(r * inv_alpha) * out_a,
                    linear_to_srgb(g * inv_alpha) * out_a,
                    linear_to_srgb(b * inv_alpha) * out_a,
                    out_a
                };
            }
        }
    });
}

// ── run_layer_mode ───────────────────────────────────────────────────

// ── Glow accumulator result ────────────────────────────────────────────
struct GlowAccumResult {
    std::shared_ptr<Framebuffer> glow;
    raster::BBox roi_bounds;
};

/// Build ONLY the accumulated glow light from the source, without compositing
/// it back.  Caller decides how to composite.
/// Returns std::nullopt when the ROI is empty.
/// `debug_cfg` is the per-instance DebugConfig forwarded from RenderGraphContext
/// (TICKET-007 — replaces the removed process-wide `detail::g_debug_config`).
/// When nullptr, the per-pass debug PNG artifacts are skipped.
[[nodiscard]] std::optional<GlowAccumResult> build_glow_accumulator(
    const Framebuffer& fb,
    const GlowPipeline& p,
    const std::optional<raster::BBox>& clip,
    bool source_is_alpha_mask,
    const chronon3d::DebugConfig* debug_cfg = nullptr) {
    const i32 w = fb.width(), h = fb.height();
    int debug_pass_counter = 0;
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
    float authored_radius = std::max(0.0f, p.radius);
    if (!p.layers.empty()) {
        for (const auto& layer : p.layers) {
            authored_radius = std::max(authored_radius, std::max(0.0f, layer.radius));
        }
    }
    const float base_radius = authored_radius * std::max(0.0f, p.spread);
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
    if (roi_w <= 0 || roi_h <= 0) return std::nullopt;

    auto source_fb = acquire_temp_framebuffer(roi_w, roi_h);
    source_fb->clear({0,0,0,0});
    // ── Source extraction ───────────────────────────────────────────────
    // Preserve the complete glyph coverage mask.  Thresholding low-alpha
    // pixels here creates a hard contour around anti-aliased glyph edges and
    // makes the glow look cut out; transparent padding is already excluded
    // by alpha==0 and the ROI is derived from the actual source bounds.
    tbb::parallel_for(tbb::blocked_range<int>(0, roi_h, kGlowTbbGrain),
                      [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            const int sy = y + y_min_pad;
            const Color* src_row = fb.pixels_row(sy);
            Color* source_row = source_fb->pixels_row(y);
            for (int x = 0; x < roi_w; ++x) {
                Color c = src_row[x_min_pad + x];
                source_row[x] = c;
            }
        }
    });

    // ── Build per-call falloff LUT once.  Used by both accumulate_glow_pass
    // and accumulate_scaled_glow_pass to replace std::pow per pixel.
    float falloff_lut[kFalloffLutSize];
    build_falloff_lut(p.falloff, falloff_lut);

    // ── Debug: save source framebuffer (per-instance debug_cfg — TICKET-007) ──
    if (debug_cfg && debug_cfg->glow()) {
        save_png(*source_fb, "output/debug_glow_source.png");
    }

    auto glow_acc_fb = acquire_temp_framebuffer(roi_w, roi_h);
    glow_acc_fb->clear({0,0,0,0});
    auto pass_fb = acquire_temp_framebuffer(roi_w, roi_h);
    pass_fb->clear({0,0,0,0});

    std::vector<GlowLayerPass> passes;
    if (!p.layers.empty()) {
        for (const auto& layer : p.layers) {
            passes.push_back({layer.radius, layer.opacity, std::clamp(layer.scale, 0.05f, 1.0f), layer.color});
        }
    } else {
        const float outer_downscale = p.quality == GlowQuality::High
            ? 1.0f
            : std::clamp(p.outer_downscale, 0.05f, 1.0f);
        passes.push_back({0.10f * base_radius, std::max(0.0f, p.core_strength), 1.0f});
        passes.push_back({0.35f * base_radius, std::max(0.0f, p.aura_strength), 1.0f});
        passes.push_back({1.00f * base_radius, std::max(0.0f, p.bloom_strength), outer_downscale});
    }

    for (const auto& pass : passes) {
        const float r = (!p.layers.empty()) ? pass.radius_scale * std::max(0.0f, p.spread) : pass.radius_scale;
        if (r < 0.5f || pass.intensity_scale <= 0.0f) continue;

        if (pass.buffer_scale >= 0.999f) {
            pass_fb->clear({0,0,0,0});
            tbb::parallel_for(tbb::blocked_range<int>(0, roi_h, kGlowTbbGrain),
                              [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y < range.end(); ++y) {
                    Color* pass_row = pass_fb->pixels_row(y);
                    const Color* source_row = source_fb->pixels_row(y);
                    for (int x = 0; x < roi_w; ++x) {
                        const float trigger = source_is_alpha_mask
                                ? source_row[x].a
                                : glow_trigger_from_pixel(source_row[x], p);
                        if (trigger > 0.0f) {
                            pass_row[x] = glow_color_from_trigger(trigger, p, pass.intensity_scale, pass.tint);
                        }
                    }
                }
            });

            // 2-pass box filter is ~33% cheaper than 3-pass with the same
            // effective sigma, since two_pass_equivalent_radius already
            // rescales the kernel radius to compensate.  Visually identical
            // to within 1-2/255 per pixel for typical SpecialName-style
            // glow radii (18-100px).
            if (p.quality == GlowQuality::High) {
                // Radius is the visual lobe extent; 3 sigma covers the
                // Gaussian contribution without a hard rectangular edge.
                apply_gaussian_blur_linear_premultiplied(*pass_fb,
                                                         std::max(0.5f, r / 3.0f));
            } else {
                apply_blur(*pass_fb, r, std::nullopt, 2);
            }
            accumulate_glow_pass(*glow_acc_fb, *pass_fb, p, falloff_lut);

            // Debug: save each glow pass before accumulation (per-instance debug_cfg — TICKET-007)
            if (debug_cfg && debug_cfg->glow()) {
                save_png(*pass_fb, "output/debug_glow_pass_" + std::to_string(debug_pass_counter++) + ".png");
            }
        } else {
            const i32 small_w = std::max(1, static_cast<i32>(std::ceil(static_cast<float>(roi_w) * pass.buffer_scale)));
            const i32 small_h = std::max(1, static_cast<i32>(std::ceil(static_cast<float>(roi_h) * pass.buffer_scale)));
            auto small_fb = acquire_temp_framebuffer(small_w, small_h);
            small_fb->clear({0,0,0,0});
            const float inv_buffer_scale = 1.0f / pass.buffer_scale;

            tbb::parallel_for(tbb::blocked_range<int>(0, small_h, kGlowTbbGrain),
                              [&](const tbb::blocked_range<int>& range) {
                for (int y = range.begin(); y < range.end(); ++y) {
                    Color* small_row = small_fb->pixels_row(y);
                    const float sy = (static_cast<float>(y) + 0.5f) * inv_buffer_scale;
                    for (int x = 0; x < small_w; ++x) {
                        const float sx = (static_cast<float>(x) + 0.5f) * inv_buffer_scale;
                        const Color c = source_fb->sample(sx, sy, SamplingMode::Bilinear);
                        const float trigger = source_is_alpha_mask
                                ? c.a
                                : glow_trigger_from_pixel(c, p);
                        if (trigger > 0.0f) {
                            small_row[x] = glow_color_from_trigger(trigger, p, pass.intensity_scale, pass.tint);
                        }
                    }
                }
            });

            if (p.quality == GlowQuality::High) {
                apply_gaussian_blur_linear_premultiplied(
                    *small_fb,
                    std::max(0.5f, r * pass.buffer_scale / 3.0f));
            } else {
                apply_blur(*small_fb, std::max(0.5f, r * pass.buffer_scale),
                           std::nullopt, 2);
            }
            accumulate_scaled_glow_pass(*glow_acc_fb, *small_fb, p, pass.buffer_scale, falloff_lut);
        }
    }

    // ── Debug: save accumulated glow before compositing (per-instance debug_cfg — TICKET-007) ──
    if (debug_cfg && debug_cfg->glow()) {
        save_png(*glow_acc_fb, "output/debug_glow_accumulated.png");
    }

    return GlowAccumResult{
        glow_acc_fb,
        raster::BBox{x_min_pad, y_min_pad, x_max_pad, y_max_pad}
    };
}

/// `debug_cfg` is forwarded to `build_glow_accumulator` (TICKET-007 — replaces
/// the removed process-wide `detail::g_debug_config`).  When nullptr, debug
/// overlays are skipped.
void run_layer_mode(Framebuffer& fb, const GlowPipeline& p,
                    const std::optional<raster::BBox>& clip,
                    bool source_is_alpha_mask,
                    const chronon3d::DebugConfig* debug_cfg = nullptr) {
    const i32 w = fb.width(), h = fb.height();
    auto acc = build_glow_accumulator(fb, p, clip, source_is_alpha_mask, debug_cfg);
    if (!acc) return;

    // Composite the accumulated glow back into the source framebuffer.
    // Both buffers use premultiplied RGB. Preserve-source mode places the
    // glow behind the original glyph without multiplying its alpha twice.
    const auto& rb = acc->roi_bounds;
    const i32 g_w = rb.x1 - rb.x0;
    const i32 g_h = rb.y1 - rb.y0;
    for (i32 y = 0; y < g_h; ++y) {
        const i32 dy = y + rb.y0;
        if (dy < 0 || dy >= h) continue;
        Color* dst_row = fb.pixels_row(dy);
        const Color* glow_row = acc->glow->pixels_row(y);
        for (i32 x = 0; x < g_w; ++x) {
            const i32 dx = x + rb.x0;
            if (dx < 0 || dx >= w) continue;
            const Color src_c = dst_row[dx];
            const Color glow_c = glow_row[x];
            if (p.preserve_source) {
                // glow behind source: source OVER glow, in premultiplied
                // form. Opaque glyph cores therefore remain unchanged.
                const float inv_source_a = 1.0f - std::clamp(src_c.a, 0.0f, 1.0f);
                dst_row[dx] = {
                    src_c.r + glow_c.r * inv_source_a,
                    src_c.g + glow_c.g * inv_source_a,
                    src_c.b + glow_c.b * inv_source_a,
                    src_c.a + glow_c.a * inv_source_a
                };
            } else {
                dst_row[dx] = compositor::blend(glow_c, src_c, glow_blend_mode(p));
            }
        }
    }
}

// ── run_bloom_mode (re-merged from glow_bloom.cpp) ──────────────────────
//
// FASE 7 gave this function its own translation unit and header; it has a
// single caller (run_glow_pipeline below), so it lives beside run_layer_mode
// again and keeps internal linkage.  Behaviour is unchanged.
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
                // Bloom is composited as a layer effect.  Its RGB contribution
                // must carry coverage when it lands outside the source alpha;
                // otherwise SourceOver discards the halo as transparent even
                // though the blurred RGB values are present.
                dst_row[dx].a = std::max(dst_row[dx].a,
                                         std::clamp(b.a * p.intensity, 0.0f, 1.0f));
            }
        }
    }
}

} // anonymous namespace

// ── Public per-pass accumulate helpers (renderer-ns scope) ────────────────
//
// These are exposed at chronon3d::renderer scope so external callers
// (Google Benchmarks, golden tests) can call them directly.  Anonymous-
// namespace callers (build_glow_accumulator) now find these via enclosing
// namespace lookup (deduplicated in FASE 7 Step 3).
// Do not use from production code — call GlowPipeline::render or
// renderer::run_glow_pipeline instead.

void accumulate_glow_pass(Framebuffer& dst, const Framebuffer& src,
                          const GlowPipeline& p, const float* falloff_lut) {
    const float falloff = std::max(1.0f, p.falloff);
    const int w = dst.width();
    const int h = dst.height();
    const bool use_lut = (falloff != 1.0f) && (falloff_lut != nullptr);
    tbb::parallel_for(tbb::blocked_range<int>(0, h, kGlowTbbGrain),
                      [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            Color* dst_row = dst.pixels_row(y);
            const Color* src_row = src.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                Color g = src_row[x];
                if (g.a <= 0.0f) continue;

                float shaped;
                if (use_lut) {
                    shaped = lookup_shaped(g.a, falloff_lut);
                } else {
                    shaped = g.a;
                }
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
                // RGB is accumulated additively for HDR glow energy. Coverage
                // follows a bounded union, not max(alpha), so separate lobes
                // do not leave a flat outer alpha skirt.
                acc.a = std::clamp(acc.a + g.a, 0.0f, 1.0f);
            }
        }
    });
}

void accumulate_scaled_glow_pass(Framebuffer& dst, const Framebuffer& src,
                                 const GlowPipeline& p, float scale,
                                 const float* falloff_lut) {
    const float falloff = std::max(1.0f, p.falloff);
    const int w = dst.width();
    const int h = dst.height();
    const bool use_lut = (falloff != 1.0f) && (falloff_lut != nullptr);
    tbb::parallel_for(tbb::blocked_range<int>(0, h, kGlowTbbGrain),
                      [&](const tbb::blocked_range<int>& range) {
        for (int y = range.begin(); y < range.end(); ++y) {
            Color* dst_row = dst.pixels_row(y);
            const float sy = (static_cast<float>(y) + 0.5f) * scale;
            for (int x = 0; x < w; ++x) {
                const float sx = (static_cast<float>(x) + 0.5f) * scale;
                Color g = src.sample(sx, sy, SamplingMode::Bilinear);
                if (g.a <= 0.0f) continue;

                float shaped;
                if (use_lut) {
                    shaped = lookup_shaped(g.a, falloff_lut);
                } else {
                    shaped = g.a;
                }
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
                // RGB is accumulated additively for HDR glow energy. Coverage
                // follows a bounded union, not max(alpha), so separate lobes
                // do not leave a flat outer alpha skirt.
                acc.a = std::clamp(acc.a + g.a, 0.0f, 1.0f);
            }
        }
    });
}

} // namespace chronon3d::renderer

// ── Forward declarations ───────────────────────────────────────────────
namespace chronon3d::renderer {
[[nodiscard]] std::optional<raster::BBox> run_glow_accumulate(
    Framebuffer& glow_dst,
    const Framebuffer& source,
    const GlowPipeline& p,
    const std::optional<raster::BBox>& clip,
    bool source_is_alpha_mask,
    const chronon3d::DebugConfig* debug_cfg);  // default = nullptr lives on defn
}

// ── GlowPipeline::render() — unified entry point (Item 17) ────────────

namespace chronon3d {

GlowPipelineOutput GlowPipeline::render(graph::RenderGraphContext& ctx,
                                        const GlowPipelineInput& input) {
    const i32 w = input.source->width();
    const i32 h = input.source->height();

    // TICKET-007: per-instance DebugConfig propagated through
    // RenderGraphContext::options::debug_config.  Forwarded into both
    // run_glow_accumulate (alpha-mask text-glow path) and
    // run_glow_pipeline (standard compositing path) so the
    // debug_*_glow_*.png artifacts toggle correctly per engine.
    const chronon3d::DebugConfig* debug_cfg = ctx.policy.debug_config;

    // Build internal pipeline config from GlowParams.
    GlowPipeline p = GlowPipeline::from(input.params);

    // Acquire from pool or allocate fresh.
    OwnedFB result;
    if (ctx.framebuffer_pool) {
        result = ctx.framebuffer_pool->acquire_owned(w, h, false);
    } else {
        profiling::FramebufferAllocationScope allocation_scope(
            profiling::FramebufferAllocationCategory::Glow);
        result = OwnedFB(new Framebuffer(w, h), PoolFbDeleter{});
    }
    auto* work_fb = result.get();
    work_fb->clear(Color::transparent());

    // Compute affected bounds from clip + glow extent.
    const f32 extent = glow_effect_extent(p);
    auto effect_clip = renderer::expand_effect_clip(input.clip, w, h, extent);
    raster::BBox bbox = effect_clip.value_or(raster::BBox{0, 0, w, h});
    // Clamp to source dimensions
    bbox.x0 = std::max(0, bbox.x0);
    bbox.y0 = std::max(0, bbox.y0);
    bbox.x1 = std::min(w, bbox.x1);
    bbox.y1 = std::min(h, bbox.y1);

    if (input.source_is_alpha_mask) {
        // Glow-only path: return accumulated glow WITHOUT compositing
        // into the source.  Used by text_glow.cpp which composites the
        // glow behind the text.
        auto glow_only = renderer::run_glow_accumulate(*work_fb, *input.source, p, input.clip, true, debug_cfg);
        if (glow_only) {
            bbox = *glow_only;
        }
    } else {
        // Standard path: copy source, composite glow over it.
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = input.source->pixels_row(y);
            Color* dst_row = work_fb->pixels_row(y);
            std::copy_n(src_row, w, dst_row);
        }
        renderer::run_glow_pipeline(*work_fb, p, input.clip, false, debug_cfg);
    }

    return {std::move(result), bbox};
}

} // namespace chronon3d

// ── run_glow_pipeline() — main entry point (lives in chronon3d::renderer)

namespace chronon3d::renderer {

void run_glow_pipeline(Framebuffer& fb, const GlowPipeline& p,
                       const std::optional<raster::BBox>& clip,
                       bool source_is_alpha_mask,
                       const chronon3d::DebugConfig* debug_cfg) {
    switch (p.mode) {
    case GlowPipeline::Mode::Layer:
        run_layer_mode(fb, p, clip, source_is_alpha_mask, debug_cfg);
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

/// Build glow-only accumulator into a clean framebuffer (no source compositing).
/// glow_dst must be cleared to transparent before calling.
/// Reads source from the provided framebuffer and writes accumulated glow
/// into glow_dst.  Returns the ROI bounds or std::nullopt on empty ROI.
/// `debug_cfg` is forwarded to `build_glow_accumulator` (TICKET-007).
[[nodiscard]] std::optional<raster::BBox> run_glow_accumulate(
    Framebuffer& glow_dst,
    const Framebuffer& source,
    const GlowPipeline& p,
    const std::optional<raster::BBox>& clip,
    bool source_is_alpha_mask,
    const chronon3d::DebugConfig* debug_cfg = nullptr) {
    auto acc = build_glow_accumulator(source, p, clip, source_is_alpha_mask, debug_cfg);
    if (!acc) return std::nullopt;

    // Copy accumulated glow into glow_dst (same size, positioned at ROI)
    const auto& rb = acc->roi_bounds;
    const i32 g_w = acc->glow->width();
    const i32 g_h = acc->glow->height();
    const i32 dst_w = glow_dst.width();
    const i32 dst_h = glow_dst.height();
    for (i32 y = 0; y < g_h; ++y) {
        const i32 dy = y + rb.y0;
        if (dy < 0 || dy >= dst_h) continue;
        Color* dst_row = glow_dst.pixels_row(dy);
        const Color* glow_row = acc->glow->pixels_row(y);
        for (i32 x = 0; x < g_w; ++x) {
            const i32 dx = x + rb.x0;
            if (dx < 0 || dx >= dst_w) continue;
            dst_row[dx] = glow_row[x];
        }
    }

    return rb;
}

} // namespace chronon3d::renderer
