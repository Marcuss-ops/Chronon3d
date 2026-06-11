#pragma once

// ---------------------------------------------------------------------------
// text_processor_helpers.hpp
//
// Internal utility functions for text shape processing:
// Blend2D color conversion, transform queries, cache management, hash utilities.
// Header-only (static inline) — shared across text_glow.cpp, text_shadow.cpp,
// and software_text_processor.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <blend2d.h>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <algorithm>

namespace chronon3d::renderer {

// ── Blend2D color conversion ───────────────────────────────────────

[[nodiscard]] static inline BLRgba32 to_bl_rgba(const Color& c) {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(c.a * 255.0f, 0.0f, 255.0f))
    );
}

// ── Transform utilities ────────────────────────────────────────────

[[nodiscard]] static inline bool is_affine_transform(const Mat4& m) {
    return
        std::abs(m[0][2]) < 1e-5f &&
        std::abs(m[1][2]) < 1e-5f &&
        std::abs(m[2][0]) < 1e-5f &&
        std::abs(m[2][1]) < 1e-5f &&
        std::abs(m[2][2] - 1.0f) < 1e-5f &&
        std::abs(m[2][3]) < 1e-5f &&
        std::abs(m[3][2]) < 1e-5f;
}

[[nodiscard]] static inline bool has_non_translation(const Mat4& m) {
    return
        std::abs(m[0][0] - 1.0f) > 1e-5f ||
        std::abs(m[0][1]) > 1e-5f ||
        std::abs(m[1][0]) > 1e-5f ||
        std::abs(m[1][1] - 1.0f) > 1e-5f;
}

// ── Cache management ───────────────────────────────────────────────

using CacheKey = u64;
using ShadowCache = cache::LruCache<CacheKey, std::shared_ptr<BLImage>>;

// Note: cache and mutex functions are `inline` (not `static inline`) to
// guarantee a single shared instance across translation units.
[[nodiscard]] inline ShadowCache& get_shadow_cache() {
    static ShadowCache cache(Config::get().shadow_cache_max_bytes, 4);
    return cache;
}

[[nodiscard]] inline ShadowCache& get_glow_cache() {
    static ShadowCache cache(Config::get().glow_cache_max_bytes, 4);
    return cache;
}

inline std::mutex& text_glow_cache_mutex() {
    static std::mutex m;
    return m;
}

inline std::mutex& text_shadow_cache_mutex() {
    static std::mutex m;
    return m;
}

// ── Hash utilities ─────────────────────────────────────────────────

using chronon3d::graph::hash_combine;
using chronon3d::graph::hash_value;
using chronon3d::graph::hash_string;
using chronon3d::graph::hash_text_style_full;

[[nodiscard]] static inline CacheKey hash_text_shape(const TextShape& text, float effective_size) {
    return hash_text_style_full(text, effective_size, 0);
}

[[nodiscard]] static inline CacheKey hash_glow_params(const RenderNode& node, float effective_size) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(node.glow.radius));
    seed = hash_combine(seed, hash_value(node.glow.intensity));
    seed = hash_combine(seed, hash_value(node.glow.color.r));
    seed = hash_combine(seed, hash_value(node.glow.color.g));
    seed = hash_combine(seed, hash_value(node.glow.color.b));
    seed = hash_combine(seed, hash_value(node.glow.color.a));
    return seed;
}

[[nodiscard]] static inline CacheKey hash_shadow_params(const RenderNode& node, float effective_size, size_t index) {
    CacheKey seed = hash_text_shape(node.shape.text, effective_size);
    seed = hash_combine(seed, hash_value(index));
    const auto& shadow = node.shape.text.style.shadows[index];
    seed = hash_combine(seed, hash_value(shadow.blur));
    seed = hash_combine(seed, hash_value(shadow.opacity));
    seed = hash_combine(seed, hash_value(shadow.color.r));
    seed = hash_combine(seed, hash_value(shadow.color.g));
    seed = hash_combine(seed, hash_value(shadow.color.b));
    seed = hash_combine(seed, hash_value(shadow.color.a));
    return seed;
}

} // namespace chronon3d::renderer
