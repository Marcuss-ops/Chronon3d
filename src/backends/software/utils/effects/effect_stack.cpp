// ---------------------------------------------------------------------------
// effect_stack.cpp — Effect stack dispatcher
//
// Orchestrates effect application by delegating to specialized implementations:
//   effect_glow_impl.cpp    — Glow (multi-pass blur + accumulation)
//   effect_shadow_impl.cpp  — DropShadow + node-level draw_shadow
//   effect_bloom_impl.cpp   — Bloom (bright-pass extract + blur + additive)
//   effect_blur.cpp         — Gaussian blur
//   effect_color.cpp        — Tint, Brightness, Contrast, Saturation, HueRotate, Invert, Vignette
//   effect_wave.cpp         — Fake3DWave
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "../../primitive_renderer.hpp"
#include "effects_internal.hpp"
#include "effect_helpers.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <cstring>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace renderer {

// ── Forward declarations ─────────────────────────────────────────────────────
void apply_focus_in_ladder(Framebuffer& fb, const FocusInLadderParams& p,
                            float time_seconds, const std::optional<raster::BBox>& clip);

void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                        float time_seconds, const std::optional<raster::BBox>& clip,
                        bool diagnostics_enabled) {
    using enum effects::EffectType;
    const auto stack_start = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    double blur_ms = 0.0;
    double tint_ms = 0.0;
    double brightness_ms = 0.0;
    double contrast_ms = 0.0;
    double glow_ms = 0.0;
    double shadow_ms = 0.0;
    double bloom_ms = 0.0;
    double fake3d_ms = 0.0;

    for (const auto& inst : stack) {
        if (!inst.enabled) continue;

        switch (inst.effect_type) {

        case Blur: {
            auto* p = std::get_if<BlurParams>(&inst.params);
            if (p && p->radius > 0.0f) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                auto effect_clip = expand_effect_clip(clip, fb.width(), fb.height(), p->radius);
                apply_blur(fb, p->radius, effect_clip);
                if (diagnostics_enabled) {
                    blur_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Tint: {
            auto* p = std::get_if<TintParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                LayerEffect e;
                e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    tint_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Brightness: {
            auto* p = std::get_if<BrightnessParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                LayerEffect e; e.brightness = p->value;
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    brightness_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Contrast: {
            auto* p = std::get_if<ContrastParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                LayerEffect e; e.contrast = p->value;
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    contrast_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Glow: {
            auto* p = std::get_if<GlowParams>(&inst.params);
            if (p && p->intensity > 0.0f) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                apply_glow_effect(fb, *p, clip);
                if (diagnostics_enabled) {
                    glow_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case DropShadow: {
            auto* p = std::get_if<DropShadowParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                apply_shadow_effect(fb, *p, clip, diagnostics_enabled);
                if (diagnostics_enabled) {
                    shadow_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Bloom: {
            auto* p = std::get_if<BloomParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                apply_bloom_effect(fb, *p, clip, diagnostics_enabled);
                if (diagnostics_enabled) {
                    bloom_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Fake3DWave: {
            auto* p = std::get_if<Fake3DWaveParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                apply_fake_3d_wave(fb, *p, time_seconds);
                if (diagnostics_enabled) {
                    fake3d_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Saturation: {
            auto* p = std::get_if<SaturationParams>(&inst.params);
            if (p) apply_saturation(fb, p->value, clip);
            break;
        }

        case HueRotate: {
            auto* p = std::get_if<HueRotateParams>(&inst.params);
            if (p) apply_hue_rotate(fb, p->degrees, clip);
            break;
        }

        case Invert: {
            auto* p = std::get_if<InvertParams>(&inst.params);
            if (p) apply_invert(fb, p->amount, clip);
            break;
        }

        case Vignette: {
            auto* p = std::get_if<VignetteParams>(&inst.params);
            if (p) apply_vignette(fb, p->radius, p->softness, p->amount, p->color, clip);
            break;
        }

        case FocusInLadder: {
            auto* p = std::get_if<FocusInLadderParams>(&inst.params);
            if (p && !p->levels.empty()) {
                apply_focus_in_ladder(fb, *p, time_seconds, clip);
            }
            break;
        }

        case Unknown:
            break;
        }
    }

    if (diagnostics_enabled) {
        const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stack_start).count();
        spdlog::info(
            "[EffectStack] total={:.2f}ms blur={:.2f} tint={:.2f} brightness={:.2f} contrast={:.2f} glow={:.2f} shadow={:.2f} bloom={:.2f} fake3d={:.2f}",
            total_ms, blur_ms, tint_ms, brightness_ms, contrast_ms, glow_ms, shadow_ms, bloom_ms, fake3d_ms
        );
    }
}

// ── Node-level glow primitive ────────────────────────────────────────────────

void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (node.glow.intensity <= 0.0f || node.glow.color.a <= 0.0f) return;

    const Color base    = node.glow.color.to_linear();
    const Mat4& model   = state.matrix;

    constexpr int LAYERS = 5;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 expand = node.glow.radius * t;
        const f32 alpha  = base.a * node.glow.intensity * (1.0f - t) * state.opacity;
        if (alpha > 0.0f)
            renderer::draw_transformed_shape(fb, node.shape, model, {base.r, base.g, base.b, alpha}, expand, &state, nullptr, node.corner_radius);
    }
}

// ===========================================================================
// Precomputed Blur Ladder (FocusInLadder)
//
// Pre-renders N blur levels on first frame and caches them in a
// thread_local map.  Each subsequent frame performs only a lightweight
// crossfade between two adjacent levels — zero blur operations.
//
// Cache key: address of the FocusInLadderParams (unique per layer instance)
// + framebuffer dimensions (stale check).
// ===========================================================================

namespace {

struct LadderCacheEntry {
    std::vector<std::shared_ptr<Framebuffer>> levels;
    int cached_width{0};
    int cached_height{0};
};

thread_local std::unordered_map<const FocusInLadderParams*, LadderCacheEntry> t_ladder_cache;

} // anonymous namespace

void apply_focus_in_ladder(Framebuffer& fb, const FocusInLadderParams& p,
                            float time_seconds, const std::optional<raster::BBox>& clip) {
    const int num_levels = static_cast<int>(p.levels.size());
    const int w = fb.width(), h = fb.height();

    // ── Compute animation progress ──────────────────────────────────────
    // time_seconds is the current frame number (float).
    const float duration_f = static_cast<float>(static_cast<int>(p.duration));
    const float raw_t = duration_f > 0.0f ? std::clamp(time_seconds / duration_f, 0.0f, 1.0f) : 1.0f;
    const float eased = p.easing.apply(raw_t);

    // Map eased [0..1] → level index.
    //   t=0 (start) → highest blur level → index 0
    //   t=1 (end)   → sharp (last level)
    const float level_pos = eased * static_cast<float>(num_levels - 1);
    const int idx_a = std::min(static_cast<int>(level_pos), num_levels - 1);
    const int idx_b = std::min(idx_a + 1, num_levels - 1);
    const float blend = (p.interpolate_between_levels && idx_a != idx_b)
                            ? (level_pos - static_cast<float>(idx_a))
                            : 0.0f;

    // ── Pre-render all levels (first frame or size change) ──────────────
    auto& cache = t_ladder_cache[&p];
    if (cache.levels.empty() || cache.cached_width != w || cache.cached_height != h) {
        cache.levels.clear();
        cache.levels.reserve(num_levels);
        cache.cached_width = w;
        cache.cached_height = h;

        // Snapshot the current framebuffer content (sharp source)
        auto sharp = acquire_temp_framebuffer(w, h);
        for (int y = 0; y < h; ++y)
            std::memcpy(sharp->pixels_row(y), fb.pixels_row(y),
                        static_cast<size_t>(w) * sizeof(Color));

        for (int i = 0; i < num_levels; ++i) {
            const float radius = p.levels[i];
            auto level_fb = acquire_temp_framebuffer(w, h);
            // Copy sharp source into this level
            for (int y = 0; y < h; ++y)
                std::memcpy(level_fb->pixels_row(y), sharp->pixels_row(y),
                            static_cast<size_t>(w) * sizeof(Color));
            // Blur at the requested radius
            if (radius > 0.5f) {
                apply_blur(*level_fb, radius, clip, 3);
            }
            cache.levels.push_back(std::move(level_fb));
        }
    }

    // ── Crossfade between adjacent levels into fb ───────────────────────
    if (blend < 0.001f || idx_a >= num_levels) {
        // Single level — fast path: copy directly
        const auto* src = cache.levels[idx_a]->data();
        const auto stride = cache.levels[idx_a]->allocated_width();
        for (int y = 0; y < h; ++y)
            std::memcpy(fb.pixels_row(y), src + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(w) * sizeof(Color));
    } else {
        // Crossfade: blend between idx_a and idx_b
        const auto* src_a = cache.levels[idx_a]->data();
        const auto* src_b = cache.levels[idx_b]->data();
        const auto stride_a = cache.levels[idx_a]->allocated_width();
        const auto stride_b = cache.levels[idx_b]->allocated_width();
        const float inv_blend = 1.0f - blend;

        tbb::parallel_for(tbb::blocked_range<int>(0, h), [&](const tbb::blocked_range<int>& ry) {
            for (int y = ry.begin(); y < ry.end(); ++y) {
                Color* dst = fb.pixels_row(y);
                const Color* row_a = src_a + static_cast<size_t>(y) * stride_a;
                const Color* row_b = src_b + static_cast<size_t>(y) * stride_b;
                for (int x = 0; x < w; ++x) {
                    dst[x] = Color{
                        row_a[x].r * inv_blend + row_b[x].r * blend,
                        row_a[x].g * inv_blend + row_b[x].g * blend,
                        row_a[x].b * inv_blend + row_b[x].b * blend,
                        std::max(row_a[x].a, row_b[x].a)
                    };
                }
            }
        });
    }
}

} // namespace renderer
} // namespace chronon3d
