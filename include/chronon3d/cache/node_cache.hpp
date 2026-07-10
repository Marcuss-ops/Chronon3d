#pragma once

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d::cache {

struct NodeCacheKey {
    std::string scope;
    Frame       frame{0};
    i32         width{0};
    i32         height{0};
    u64         params_hash{0};
    u64         source_hash{0};
    u64         input_hash{0};

    /// Sub-frame temporal key for motion blur / temporal supersampling.
    /// Static nodes share the same key (frame=0, tick=0) to avoid
    /// re-rendering across motion-blur sub-samples.
    TemporalSampleKey temporal_key{0, 0, 0};

    // Tile-based cache differentiation (Branch 4).
    // Defaults (-1, -1, 0, 0) produce the same digest as before,
    // so non-tile cache keys are unaffected.
    i32         tile_x{-1};
    i32         tile_y{-1};
    i32         tile_size{0};
    u64         tile_hash{0};

    [[nodiscard]] u64 digest() const;
    bool operator==(const NodeCacheKey&) const = default;
};

// =============================================================================
// TICKET-ae-cam-hash-collision Soluzione B — camera-aware cache-key folding
// =============================================================================
//
// `fold_camera_into_params_hash(key, cam)` and `camera_fingerprint_digest(cam)`
// mixer the EVALUATED 2.5D camera state into `key.params_hash` so AE_CAM_02
// (cam.zoom 500/1000/1500), AE_CAM_04 (cam.position.z -600→-1400 with constant
// zoom), and AE_CAM_08 (cam.dof.focus_distance anim) each produce DISTINCT
// per-frame cache keys. Without this blend, the render-graph's framebuffer
// cache returns a stale FB from a previous zoom/Z/dof state and the rendered
// framebuffer is byte-identical across frames (root cause documented in
// `docs/tickets/TICKET-ae-cam-hash-collision.md`).
//
// AGENTS.md v0.1 Cat-3 caveat: these 3 inline symbols constitute a small
// surface-area extension under `chronon3d::cache::`. They are public-by-
// include-path (this header is in the SDK umbrella) but functionally
// internal-by-usage: only the 7 render-graph propagation sites
// (multi_source_node.cpp + source_node.cpp + TextRunNode.cpp + their 4
// refresh/builder pass sites) call them. A future Cat-3 reconciliation
// may relocate these to `src/cache/include_private/chronon3d/cache/
// node_cache_camera_fingerprint.hpp` (currently include_private is PRIVATE
// for `chronon3d_cache` itself only). Until that ADR, downstream
// consumers should NOT depend on the stability of these 3 symbols.
// =============================================================================

/// Standard boost::hash_combine mixer (matches `HashBuilder::mix` recipe in
/// `chronon3d/core/hash/hash_builder.hpp`). Inlined here so callers can blend
/// an external digest into an existing `NodeCacheKey::params_hash` without
/// pulling in `render_graph_hashing.hpp` transitively.
inline u64 mix_params_hash(u64 seed, u64 value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

/// Compute a deterministic u64 digest of the evaluated Camera2_5D state.
/// Includes: cam.zoom bit-pattern, cam.position.z bit-pattern, cam.fov_deg,
/// cam.point_of_interest bytes (if enabled), cam.parent_name bytes (if
/// non-empty), and cam.dof.focus_distance (if cam.dof.enabled). O(1).
/// Empty parent_name contributes NOTHING (parent.is_null sentinel);
/// cam.dof.disabled also contributes nothing (DOF lock for AE_CAM_02 + 04).
[[nodiscard]] inline u64 camera_fingerprint_digest(const ::chronon3d::Camera2_5D& cam) {
    ::chronon3d::core::hash::HashBuilder hb{};
    hb.add(cam.zoom);
    hb.add(cam.position.z);
    hb.add(cam.fov_deg);
    if (cam.point_of_interest_enabled) {
        hb.add_bytes(&cam.point_of_interest, sizeof(::chronon3d::Vec3));
    }
    if (!cam.parent_name.empty()) {
        hb.add_bytes(cam.parent_name.data(), cam.parent_name.size());
    }
    if (cam.dof.enabled) {
        hb.add(cam.dof.focus_distance);
    }
    return hb.finish();
}

/// Fold the evaluated camera state into `key.params_hash` in place.
/// Safe to skip when `ctx.frame_input.has_camera_2_5d == false`.
inline void fold_camera_into_params_hash(
    NodeCacheKey& key,
    const ::chronon3d::Camera2_5D& cam) noexcept {
    key.params_hash = mix_params_hash(key.params_hash, camera_fingerprint_digest(cam));
}

struct NodeCacheKeyHash {
    size_t operator()(const NodeCacheKey& key) const noexcept {
        return static_cast<size_t>(key.digest());
    }
};

using FramebufferCache = LruCache<NodeCacheKey, std::shared_ptr<Framebuffer>, NodeCacheKeyHash>;

class NodeCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    explicit NodeCache(size_t capacity_bytes = 2048ULL * 1024 * 1024);
    ~NodeCache();
    NodeCache(NodeCache&&) noexcept = default;
    NodeCache& operator=(NodeCache&&) noexcept = default;

    [[nodiscard]] Value get(const NodeCacheKey& key);
    void store(const NodeCacheKey& key, Value value);
    
    [[nodiscard]] bool contains(const NodeCacheKey& key) const;
    void clear();
    
    [[nodiscard]] LruCache<NodeCacheKey, Value, NodeCacheKeyHash>::Stats stats() const { return m_cache.stats(); }
    [[nodiscard]] size_t size() const { return m_cache.stats().current_size; }
    
    void set_capacity(size_t capacity_bytes);

    bool erase(const NodeCacheKey& key);

private:
    CacheDiagnostics::Handle m_diag_handle;
    std::atomic<bool> m_diag_alive{true};
    FramebufferCache m_cache;
    // Pre-existing pre-rewrite leftover: lifetime guard for the lambdas
    // stored in CacheDiagnostics via m_diag_handle.  Set false in
    // ~NodeCache() before m_diag_handle's destructor releases the
    // lambdas, so any late call from the diagnostics thread short-circuits
    // to a no-op instead of touching freed m_cache.
    std::atomic<bool> m_diag_alive{true};
};

} // namespace chronon3d::cache
