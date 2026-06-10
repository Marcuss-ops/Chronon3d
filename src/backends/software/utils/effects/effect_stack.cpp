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
#include <mutex>
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
    const auto stack_start = std::chrono::steady_clock::now();
    double blur_ms = 0.0;
    double tint_ms = 0.0;
    double brightness_ms = 0.0;
    double contrast_ms = 0.0;
    double glow_ms = 0.0;
    double shadow_ms = 0.0;
    double bloom_ms = 0.0;
    double fake3d_ms = 0.0;
    double focus_in_ladder_ms = 0.0;

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
                const auto t0 = std::chrono::steady_clock::now();
                apply_focus_in_ladder(fb, *p, time_seconds, clip);
                const auto elapsed_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
                focus_in_ladder_ms += elapsed_ms;
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
            "[EffectStack] total={:.2f}ms blur={:.2f} tint={:.2f} brightness={:.2f} contrast={:.2f} glow={:.2f} shadow={:.2f} bloom={:.2f} fake3d={:.2f} focus_in_ladder={:.2f}",
            total_ms, blur_ms, tint_ms, brightness_ms, contrast_ms, glow_ms, shadow_ms, bloom_ms, fake3d_ms, focus_in_ladder_ms
        );
    }

    // ── Per-effect wall-time counters (always emitted, not just diagnostics) ──
    {
        const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stack_start).count();
        if (auto* cnt = profiling::g_current_counters) {
            cnt->effect_stack_total_ms.fetch_add(
                static_cast<uint64_t>(std::llround(total_ms)), std::memory_order_relaxed);
            cnt->effect_focus_in_ladder_ms.fetch_add(
                static_cast<uint64_t>(std::llround(focus_in_ladder_ms)), std::memory_order_relaxed);
        }
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
// shared (static) map with mutex for thread safety.  Previously used
// thread_local, but that broke warmup priming because TBB's work-stealing
// scheduler may execute EffectStack on a different thread each frame.
//
// Cache key: content hash of levels + duration + easing + framebuffer dims.
// ===========================================================================

namespace {

struct LadderKey {
    std::vector<f32> levels;
    Frame duration;
    EasingCurve easing;
    bool interpolate_between_levels;
    int cache_w{0};
    int cache_h{0};
    bool operator==(const LadderKey& other) const = default;
};

struct LadderKeyHash {
    size_t operator()(const LadderKey& k) const noexcept {
        size_t h = 0;
        for (f32 v : k.levels) {
            h ^= std::hash<f32>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        h ^= std::hash<int>{}(static_cast<int>(k.duration)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(k.easing.preset)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        if (k.easing.cubic.has_value()) {
            h ^= std::hash<f32>{}(k.easing.cubic->x1) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<f32>{}(k.easing.cubic->y1) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<f32>{}(k.easing.cubic->x2) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<f32>{}(k.easing.cubic->y2) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        h ^= std::hash<bool>{}(k.interpolate_between_levels) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.cache_w) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.cache_h) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct LadderCacheEntry {
    std::vector<std::shared_ptr<Framebuffer>> levels;
};

// Shared (non-thread_local) cache + mutex — survives across TBB worker
// threads so that warmup priming (on thread A) is visible to subsequent
// render-loop frames (which may execute on thread B via work-stealing).
static std::unordered_map<LadderKey, LadderCacheEntry, LadderKeyHash> s_ladder_cache;
static std::mutex s_ladder_cache_mutex;

} // anonymous namespace

void apply_focus_in_ladder(Framebuffer& fb, const FocusInLadderParams& p,
                            float time_seconds, const std::optional<raster::BBox>& clip) {
    const int num_levels = static_cast<int>(p.levels.size());
    const int fb_w = fb.width(), fb_h = fb.height();

    // ── Determine effective region: clip rect or full framebuffer ────
    // When a clip rect is available (e.g. image layer occupies only a
    // sub-region of the composition), restrict ALL blur ladder operations
    // to that region.  For MinimalistImageFocusIn (800×450 image in a
    // 1920×1080 frame), this reduces the per-level blur area by ~6×.
    int w = fb_w, h = fb_h;
    int dst_x0 = 0, dst_y0 = 0;
    bool has_clip = false;
    if (clip) {
        raster::BBox cl = *clip;
        cl.clip_to(fb_w, fb_h);
        if (!cl.is_empty()) {
            w = cl.x1 - cl.x0;
            h = cl.y1 - cl.y0;
            dst_x0 = cl.x0;
            dst_y0 = cl.y0;
            has_clip = true;
        }
    }

    if (w <= 0 || h <= 0) return;

    // ── Stabilize cache key dimensions ──────────────────────────────────
    // The clip rect may vary slightly per frame due to dirty-rect bbox
    // changes during opacity/scale animation.  Without stabilization,
    // every other frame triggers a full precompute.  Bucketing to
    // 64-aligned values collapses these fluctuations into fewer stable
    // cache entries than 32-aligned while keeping blur quality
    // indistinguishable (max ~64 px variance per bucket).  Combined
    // with consecutive warmup rendering, all dimension pairs the render
    // loop will encounter are precomputed during warmup.
    const int cache_w = ((w + 63) / 64) * 64;
    const int cache_h = ((h + 63) / 64) * 64;

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
    // Dimensions are part of the cache key so frames with different clip
    // sizes (e.g. frame 0: 960×576, frame 1+: 864×512 during focus-in)
    // each get their own precomputed blur ladder.  Without dimensions in
    // the key, the two sizes alternate and overwrite each other's cached
    // levels, causing a full precompute on every frame (thrashing).
    const LadderKey cache_key{
        p.levels, p.duration, p.easing, p.interpolate_between_levels,
        cache_w, cache_h};

    // Lock the shared cache for lookup/precompute.  We copy shared_ptrs
    // to local variables before releasing the lock so the crossfade loop
    // (which may involve TBB parallel_for) runs outside the critical
    // section.
    std::shared_ptr<Framebuffer> level_a;
    std::shared_ptr<Framebuffer> level_b;
    int stride_a = 0, stride_b = 0;
    {
        std::lock_guard<std::mutex> lock(s_ladder_cache_mutex);
        auto& cache = s_ladder_cache[cache_key];
        // Cache key now includes dimensions, so levels.empty() alone
        // tells us whether we've precomputed for this exact size.
        const bool need_precompute = cache.levels.empty();
        auto precompute_t0 = need_precompute ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (need_precompute) {
            cache.levels.clear();
            cache.levels.reserve(num_levels);
            auto sharp = acquire_temp_framebuffer(cache_w, cache_h);
            if (has_clip) {
                for (int y = 0; y < h; ++y)
                    std::memcpy(sharp->pixels_row(y),
                                fb.pixels_row(dst_y0 + y) + dst_x0,
                                static_cast<size_t>(w) * sizeof(Color));
            } else {
                for (int y = 0; y < h; ++y)
                    std::memcpy(sharp->pixels_row(y), fb.pixels_row(y),
                                static_cast<size_t>(w) * sizeof(Color));
            }

            for (int i = 0; i < num_levels; ++i) {
                const float radius = p.levels[i];
                auto level_fb = acquire_temp_framebuffer(cache_w, cache_h);
                for (int y = 0; y < cache_h; ++y)
                    std::memcpy(level_fb->pixels_row(y), sharp->pixels_row(y),
                                static_cast<size_t>(cache_w) * sizeof(Color));
                if (radius > 0.5f) {
                    apply_blur(*level_fb, radius, std::nullopt, 3);
                }
                cache.levels.push_back(std::move(level_fb));
            }

            if (auto* cnt = profiling::g_current_counters) {
                const auto precompute_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - precompute_t0).count();
                cnt->effect_focus_in_ladder_precompute_ms.fetch_add(
                    static_cast<uint64_t>(std::llround(precompute_ms)), std::memory_order_relaxed);
            }
        }

        // Copy shared_ptrs for crossfade while lock is held.  The shared_ptr
        // refcount ensures the framebuffers stay alive after we release the
        // lock and another thread starts a new precompute.
        level_a = cache.levels[idx_a];
        stride_a = level_a ? level_a->allocated_width() : 0;
        if (idx_b != idx_a) {
            level_b = cache.levels[idx_b];
            stride_b = level_b ? level_b->allocated_width() : 0;
        }
    } // lock released — crossfade runs unlocked

    // ── Write crossfade result back into the fb clip region ─────────────
    const auto xfade_t0 = std::chrono::steady_clock::now();
    if (blend < 0.001f || idx_a >= num_levels) {
        // Single level — fast path: copy directly into clip region
        const auto* src = level_a->data();
        if (has_clip) {
            for (int y = 0; y < h; ++y)
                std::memcpy(fb.pixels_row(dst_y0 + y) + dst_x0,
                            src + static_cast<size_t>(y) * stride_a,
                            static_cast<size_t>(w) * sizeof(Color));
        } else {
            for (int y = 0; y < h; ++y)
                std::memcpy(fb.pixels_row(y), src + static_cast<size_t>(y) * stride_a,
                            static_cast<size_t>(w) * sizeof(Color));
        }
    } else {
        // Crossfade: blend between idx_a and idx_b into clip region
        const auto* src_a = level_a->data();
        const auto* src_b = level_b->data();
        const float inv_blend = 1.0f - blend;

        if (has_clip) {
            tbb::parallel_for(tbb::blocked_range<int>(0, h, 16), [&](const tbb::blocked_range<int>& ry) {
                for (int y = ry.begin(); y < ry.end(); ++y) {
                    Color* dst = fb.pixels_row(dst_y0 + y) + dst_x0;
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
        } else {
            tbb::parallel_for(tbb::blocked_range<int>(0, h, 16), [&](const tbb::blocked_range<int>& ry) {
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

    if (auto* cnt = profiling::g_current_counters) {
        const auto xfade_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - xfade_t0).count();
        cnt->effect_focus_in_ladder_crossfade_ms.fetch_add(
            static_cast<uint64_t>(std::llround(xfade_ms)), std::memory_order_relaxed);
    }
}

void prune_focus_in_ladder_cache(size_t max_bytes) {
    std::lock_guard<std::mutex> lock(s_ladder_cache_mutex);

    if (s_ladder_cache.size() <= 2) return;

    // Collect entries with their byte sizes and pixel areas.
    struct EntryInfo {
        LadderKey key;
        size_t bytes{0};
        int area{0};
    };
    std::vector<EntryInfo> entries;
    entries.reserve(s_ladder_cache.size());

    for (const auto& [key, entry] : s_ladder_cache) {
        EntryInfo info;
        info.key = key;
        info.area = key.cache_w * key.cache_h;
        for (const auto& fb : entry.levels) {
            if (fb) info.bytes += fb->size_bytes();
        }
        entries.push_back(std::move(info));
    }

    // Sort by pixel area ascending: smallest entries correspond to earliest
    // animation frames (focus-in starts small, grows toward full size).
    // Keeping the smallest ensures the first render-loop frames hit the cache.
    std::sort(entries.begin(), entries.end(),
              [](const EntryInfo& a, const EntryInfo& b) {
                  return a.area < b.area;
              });

    // Calculate current total.
    size_t total_bytes = 0;
    for (const auto& e : entries) total_bytes += e.bytes;

    if (total_bytes <= max_bytes) return;

    // Evict from the end (largest area → latest frames) until within budget.
    // Always keep at least 2 entries (covers frames 0–1 of the render loop).
    size_t current = total_bytes;
    for (size_t i = entries.size(); i > 2; --i) {
        if (current <= max_bytes) break;
        current -= entries[i - 1].bytes;
        s_ladder_cache.erase(entries[i - 1].key);
    }
}

} // namespace renderer
} // namespace chronon3d

