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
#ifdef CHRONON3D_USE_BLEND2D
#include "../../utils/blend2d_paint.hpp"
#include <blend2d.h>
#endif
#include <cstdlib>
#include <memory>
#include <mutex>
#include <algorithm>

namespace chronon3d::renderer {

#ifdef CHRONON3D_USE_BLEND2D
// ── Blend2D color conversion ───────────────────────────────────────
//
// PR3: re-export the canonical `to_bl_rgba` from `blend2d_bridge::paint`
// so the previous `static inline` copy (which produced one ODR-trap
// instance per translation unit) is replaced by a single shared
// version.  Visible to downstream `using chronon3d::renderer::to_bl_rgba`
// imports that previously resolved to the legacy local definition.

using chronon3d::blend2d_bridge::paint::to_bl_rgba;
#endif

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

#ifdef CHRONON3D_USE_BLEND2D
using ShadowCache = cache::LruCache<CacheKey, std::shared_ptr<BLImage>>;
#endif

// Injected capacities — set once at startup by SoftwareRenderer.
// Static state in anonymous namespace (ODR-safe via `inline`).
inline size_t& shadow_cache_capacity_bytes() {
    static size_t cap = 0;
    return cap;
}
inline size_t& glow_cache_capacity_bytes() {
    static size_t cap = 0;
    return cap;
}

/// Set capacities once at startup (called by SoftwareRenderer).
/// Thread-safe via the singleton initialization of the static locals.
inline void set_shadow_cache_capacity(size_t max_bytes) {
    shadow_cache_capacity_bytes() = max_bytes;
}
inline void set_glow_cache_capacity(size_t max_bytes) {
    glow_cache_capacity_bytes() = max_bytes;
}

#ifdef CHRONON3D_USE_BLEND2D
// Note: cache and mutex functions are `inline` (not `static inline`) to
// guarantee a single shared instance across translation units.
[[nodiscard]] inline ShadowCache& get_shadow_cache() {
    static ShadowCache cache(
        shadow_cache_capacity_bytes() > 0 ? shadow_cache_capacity_bytes() : 64ULL * 1024ULL * 1024ULL,
        4);
    return cache;
}

[[nodiscard]] inline ShadowCache& get_glow_cache() {
    static ShadowCache cache(
        glow_cache_capacity_bytes() > 0 ? glow_cache_capacity_bytes() : 64ULL * 1024ULL * 1024ULL,
        4);
    return cache;
}
#endif

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

[[nodiscard]] static inline CacheKey hash_text_shape(const Shape& shape, float effective_size) {
    return hash_text_style_full(shape.text(), effective_size, 0);
}

[[nodiscard]] static inline CacheKey hash_glow_params(const RenderNode& node, float effective_size) {
    CacheKey seed = hash_text_shape(node.shape, effective_size);
    seed = hash_combine(seed, hash_value(node.glow.radius));
    seed = hash_combine(seed, hash_value(node.glow.intensity));
    seed = hash_combine(seed, hash_value(node.glow.color.r));
    seed = hash_combine(seed, hash_value(node.glow.color.g));
    seed = hash_combine(seed, hash_value(node.glow.color.b));
    seed = hash_combine(seed, hash_value(node.glow.color.a));
    seed = hash_combine(seed, hash_value(node.glow.core_strength));
    seed = hash_combine(seed, hash_value(node.glow.aura_strength));
    seed = hash_combine(seed, hash_value(node.glow.bloom_strength));
    seed = hash_combine(seed, hash_value(node.glow.spread));
    seed = hash_combine(seed, hash_value(node.glow.softness));
    seed = hash_combine(seed, hash_value(node.glow.threshold));
    seed = hash_combine(seed, hash_value(node.glow.falloff));
    seed = hash_combine(seed, hash_value(node.glow.outer_downscale));
    seed = hash_combine(seed, hash_value(static_cast<int>(node.glow.quality)));
    seed = hash_combine(seed, hash_value(static_cast<int>(node.glow.blend)));
    seed = hash_combine(seed, hash_value(node.glow.preserve_source ? 1 : 0));
    seed = hash_combine(seed, hash_value(node.glow.additive ? 1 : 0));
    seed = hash_combine(seed, hash_value(node.glow.layers.size()));
    for (const auto& layer : node.glow.layers) {
        seed = hash_combine(seed, hash_value(layer.radius));
        seed = hash_combine(seed, hash_value(layer.opacity));
        seed = hash_combine(seed, hash_value(layer.scale));
    }
    return seed;
}

[[nodiscard]] static inline CacheKey hash_shadow_params(const RenderNode& node, float effective_size, size_t index) {
    CacheKey seed = hash_text_shape(node.shape, effective_size);
    seed = hash_combine(seed, hash_value(index));
    const auto& shadow = node.shape.text().style.shadows[index];
    seed = hash_combine(seed, hash_value(shadow.blur));
    seed = hash_combine(seed, hash_value(shadow.opacity));
    seed = hash_combine(seed, hash_value(shadow.color.r));
    seed = hash_combine(seed, hash_value(shadow.color.g));
    seed = hash_combine(seed, hash_value(shadow.color.b));
    seed = hash_combine(seed, hash_value(shadow.color.a));
    return seed;
}

} // namespace chronon3d::renderer
