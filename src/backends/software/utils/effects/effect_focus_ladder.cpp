// ---------------------------------------------------------------------------
// effect_focus_ladder.cpp — Precomputed Blur Ladder (FocusInLadder)
//
// Pre-renders N blur levels on first frame and caches them in a
// shared (static) map with mutex for thread safety.  Previously used
// thread_local, but that broke warmup priming because TBB's work-stealing
// scheduler may execute EffectStack on a different thread each frame.
//
// Cache key: levels + duration + easing + framebuffer dims + source_hash + scale params.
//
// Extracted from effect_stack.cpp to keep the dispatcher focused on
// orchestration rather than implementation details.
// ---------------------------------------------------------------------------

#include "effect_focus_ladder.hpp"
#include "effect_helpers.hpp"
#include "effects_internal.hpp"
#include "../render_effects_processor.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <cstring>

namespace chronon3d {
namespace renderer {

// ===========================================================================
// Precomputed Blur Ladder (FocusInLadder)
// ===========================================================================

namespace {

struct LadderKey {
    std::vector<f32> levels;
    Frame duration;
    EasingCurve easing;
    bool interpolate_between_levels;
    int cache_w{0};
    int cache_h{0};
    u64 source_hash{0};
    f32 scale_start{1.04f};
    f32 scale_end{1.0f};
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
        h ^= std::hash<u64>{}(k.source_hash) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<f32>{}(k.scale_start) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<f32>{}(k.scale_end) + 0x9e3779b9 + (h << 6) + (h >> 2);
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
    const int cache_w = ((w + 63) / 64) * 64;
    const int cache_h = ((h + 63) / 64) * 64;

    // ── Compute animation progress ──────────────────────────────────────
    const float duration_f = static_cast<float>(static_cast<int>(p.duration));
    const float raw_t = duration_f > 0.0f ? std::clamp(time_seconds / duration_f, 0.0f, 1.0f) : 1.0f;
    const float eased = p.easing.apply(raw_t);

    const float level_pos = eased * static_cast<float>(num_levels - 1);
    const int idx_a = std::min(static_cast<int>(level_pos), num_levels - 1);
    const int idx_b = std::min(idx_a + 1, num_levels - 1);
    const float blend = (p.interpolate_between_levels && idx_a != idx_b)
                            ? (level_pos - static_cast<float>(idx_a))
                            : 0.0f;

    // ── Pre-render all levels (first frame or size change) ──────────────
    const LadderKey cache_key{
        p.levels, p.duration, p.easing, p.interpolate_between_levels,
        cache_w, cache_h, fb.key_digest(), p.scale_start, p.scale_end};

    std::shared_ptr<Framebuffer> level_a;
    std::shared_ptr<Framebuffer> level_b;
    int stride_a = 0, stride_b = 0;
    {
        std::lock_guard<std::mutex> lock(s_ladder_cache_mutex);
        auto& cache = s_ladder_cache[cache_key];
        const bool need_precompute = cache.levels.empty();
        auto precompute_t0 = need_precompute ? profiling::now() : std::chrono::steady_clock::time_point{};
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
                const auto precompute_ms = profiling::duration_ms(
                    precompute_t0, profiling::now());
                cnt->effect_focus_in_ladder_precompute_ms.fetch_add(
                    static_cast<uint64_t>(std::llround(precompute_ms)), std::memory_order_relaxed);
            }
        }

        level_a = cache.levels[idx_a];
        stride_a = level_a ? level_a->allocated_width() : 0;
        if (idx_b != idx_a) {
            level_b = cache.levels[idx_b];
            stride_b = level_b ? level_b->allocated_width() : 0;
        }
    } // lock released — crossfade runs unlocked

    // ── Write crossfade result back into the fb clip region ─────────────
    const auto xfade_t0 = profiling::now();
    if (blend < 0.001f || idx_a >= num_levels) {
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
        const auto xfade_ms = profiling::duration_ms(
            xfade_t0, profiling::now());
        cnt->effect_focus_in_ladder_crossfade_ms.fetch_add(
            static_cast<uint64_t>(std::llround(xfade_ms)), std::memory_order_relaxed);
    }
}

void prune_focus_in_ladder_cache(size_t max_bytes) {
    std::lock_guard<std::mutex> lock(s_ladder_cache_mutex);

    if (s_ladder_cache.size() <= 2) return;

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

    std::sort(entries.begin(), entries.end(),
              [](const EntryInfo& a, const EntryInfo& b) {
                  return a.area < b.area;
              });

    size_t total_bytes = 0;
    for (const auto& e : entries) total_bytes += e.bytes;

    if (total_bytes <= max_bytes) return;

    size_t current = total_bytes;
    for (size_t i = entries.size(); i > 2; --i) {
        if (current <= max_bytes) break;
        current -= entries[i - 1].bytes;
        s_ladder_cache.erase(entries[i - 1].key);
    }
}

} // namespace renderer
} // namespace chronon3d
