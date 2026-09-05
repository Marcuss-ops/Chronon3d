#pragma once

// =============================================================================
// node_cache.hpp — ContentCache: inter-frame rendered node output cache
//
// Cache family: ContentCache (see cache/cache_taxonomy.hpp).
//
// Keys: content-derived (NodeCacheKey: scope, frame, dimensions, params_hash,
//   source_hash, input_hash, temporal_key).  Same key ⇒ same output, always.
//   Only frame-INVARIANT results enter the cache; frame-dependent results
//   bypass (they are shared within the frame via ExecutionState::temp).
// =============================================================================

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cache {

using ContentVersion = u64;

struct NodeCacheKey {
    std::string scope;
    Frame       frame{0};
    i32         width{0};
    i32         height{0};
    u64         params_hash{0};
    u64         source_hash{0};
    u64         input_hash{0};

    // Version-based dependency inputs. Zero preserves the legacy hash-only
    // contract; non-zero versions let callers invalidate without hashing
    // framebuffer contents.
    ContentVersion params_version{0};
    ContentVersion source_version{0};
    ContentVersion input_version{0};

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

    // HOT-PATH TAX D — digest memoized once per key.  `evaluate_cache` is
    // the single funnel that finishes a runtime key (input_hash/temporal/
    // tile fields are all assigned there) and then calls `finalize_digest()`
    // BEFORE the key reaches any unordered_map (lookup/store) or the state
    // publication.  After finalize the key is read-only, so digest() is a
    // single load instead of re-hashing 17 fields.  Keys never finalized
    // (e.g. compile-time static_key baking) fall back to a pure on-demand
    // computation.  Do NOT mutate key fields after finalize_digest() — the
    // memo would go stale.  These members sit at the END so positional
    // aggregate inits and designated initializers keep compiling.
    u64  precomputed_digest{0};
    bool digest_finalized{false};

    /// Memoize the digest.  Idempotent; safe to call more than once.
    void finalize_digest() noexcept {
        if (!digest_finalized) {
            precomputed_digest = compute_digest();
            digest_finalized = true;
        }
    }

    [[nodiscard]] u64 digest() const;
    /// Pure digest computation over the current fields (no memo read).
    [[nodiscard]] u64 compute_digest() const;
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
// `docs/tickets/archive/TICKET-ae-cam-hash-collision.md`).
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
/// Includes every field that can affect the view or projection. In
/// particular, position.x/y and rotation are not optional: omitting them
/// lets an animated 2.5D camera reuse a framebuffer produced by a different
/// camera pose, which presents as intermittent text flicker/pop-in.
/// O(1).
/// Empty parent_name contributes NOTHING (parent.is_null sentinel);
/// cam.dof.disabled also contributes nothing (DOF lock for AE_CAM_02 + 04).
[[nodiscard]] inline u64 camera_fingerprint_digest(const ::chronon3d::Camera2_5D& cam) {
    ::chronon3d::core::hash::HashBuilder hb{};
    hb.add(static_cast<std::uint8_t>(cam.enabled));
    hb.add(static_cast<std::uint8_t>(cam.is_animated));
    hb.add_bytes(&cam.position, sizeof(::chronon3d::Vec3));
    hb.add_bytes(&cam.rotation, sizeof(::chronon3d::Vec3));
    hb.add(static_cast<std::uint8_t>(cam.optics_mode));
    hb.add(cam.zoom);
    hb.add(cam.fov_deg);
    if (cam.point_of_interest_enabled) {
        hb.add_bytes(&cam.point_of_interest, sizeof(::chronon3d::Vec3));
    }
    if (!cam.parent_name.empty()) {
        hb.add_bytes(cam.parent_name.data(), cam.parent_name.size());
    }
    if (!cam.target_name.empty()) {
        hb.add_bytes(cam.target_name.data(), cam.target_name.size());
    }
    hb.add(static_cast<std::uint8_t>(cam.hierarchy_baked));
    hb.add(cam.lens.focal_length);
    hb.add(cam.lens.f_stop);
    hb.add(cam.lens.close_focus);
    hb.add(cam.lens.sensor_width);
    hb.add(cam.lens.sensor_height);
    hb.add(static_cast<std::uint8_t>(cam.lens.gate_fit));
    hb.add(cam.lens.pixel_aspect);
    hb.add(cam.lens.anamorphic_squeeze);
    hb.add(static_cast<std::uint8_t>(cam.motion_blur.mode));
    hb.add(cam.motion_blur.samples);
    hb.add(cam.motion_blur.shutter_angle_deg);
    hb.add(cam.motion_blur.shutter_phase_deg);
    hb.add(static_cast<std::uint8_t>(cam.motion_blur.pattern));
    hb.add(static_cast<std::uint8_t>(cam.motion_blur.filter));
    hb.add(cam.motion_blur.jitter_seed);
    if (cam.dof.enabled) {
        hb.add(static_cast<std::uint8_t>(cam.dof.enabled));
        hb.add(cam.dof.focus_z);
        hb.add(cam.dof.focus_distance);
        hb.add(cam.dof.aperture);
        hb.add(cam.dof.max_blur);
        hb.add(cam.dof.use_physical_model);
        hb.add(cam.dof.near_bokeh_radius);
        hb.add(cam.dof.far_bokeh_radius);
        hb.add(cam.lens.focal_length);
        hb.add(cam.lens.f_stop);
        hb.add(cam.lens.sensor_width);
        hb.add(cam.lens.sensor_height);
        hb.add(cam.lens.pixel_aspect);
        hb.add(cam.lens.anamorphic_squeeze);
        hb.add(static_cast<std::uint8_t>(cam.lens.gate_fit));
    } else {
        hb.add(static_cast<std::uint8_t>(cam.dof.enabled));
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

/// Diagnostics-only description of one resident node-cache entry.
/// `bytes` is the physical framebuffer backing size; the logical dimensions
/// come from the cache key and the allocated dimensions from the framebuffer.
struct NodeCacheEntrySnapshot {
    NodeCacheKey key{};
    std::size_t logical_width{0};
    std::size_t logical_height{0};
    std::size_t allocated_width{0};
    std::size_t allocated_height{0};
    std::size_t bytes{0};
};

using FramebufferCache = LruCache<NodeCacheKey, std::shared_ptr<Framebuffer>, NodeCacheKeyHash>;

class NodeCache {
public:
    using Value = std::shared_ptr<Framebuffer>;

    /// P1-10 — `diag` is the nullable observer (defaults to nullptr =
    /// no-op registration).  Positioned LAST so existing call sites that
    /// pass `capacity_bytes` continue to work unchanged.  The runtime
    /// passes `&m_diagnostics` via `set_diagnostics()` after default
    /// construction (the value-member caches are default-constructed
    /// before `m_diagnostics` is wired into the init list).
    explicit NodeCache(size_t capacity_bytes   = 2048ULL * 1024 * 1024,
                       CacheDiagnostics* diag = nullptr);
    ~NodeCache();
    /// P1-10 — re-registers with the new diagnostics.  Used by
    /// `RenderRuntime` to wire its per-instance diagnostics into the
    /// value-member caches after default construction.  Replaces the
    /// no-op handle (the old handle is RAII-destroyed; since the old
    /// handle was default-constructed with no entry, the destroy is a
    /// no-op).
    void set_diagnostics(CacheDiagnostics& diag);
    NodeCache(NodeCache&&) noexcept = default;
    NodeCache& operator=(NodeCache&&) noexcept = default;

    [[nodiscard]] Value get(const NodeCacheKey& key);
    void store(const NodeCacheKey& key, Value value);
    
    [[nodiscard]] bool contains(const NodeCacheKey& key) const;
    void clear();
    
    [[nodiscard]] LruCache<NodeCacheKey, Value, NodeCacheKeyHash>::Stats stats() const { return m_cache.stats(); }
    [[nodiscard]] size_t size() const { return m_cache.stats().current_size; }
    [[nodiscard]] size_t capacity() const noexcept { return m_cache.capacity(); }

    /// Return the largest resident entries by physical framebuffer weight.
    /// Intended for benchmark/diagnostic reports; resident-entry hit counts
    /// are intentionally not synthesized because LruCache tracks hits only
    /// at aggregate level.
    [[nodiscard]] std::vector<NodeCacheEntrySnapshot>
    top_entries_by_weight(std::size_t limit = 10) const {
        std::vector<NodeCacheEntrySnapshot> entries;
        if (limit == 0) return entries;
        m_cache.for_each([&entries](const NodeCacheKey& key, const Value& value, std::size_t) {
            if (!value) return;
            entries.push_back(NodeCacheEntrySnapshot{
                .key = key,
                .logical_width = key.width > 0 ? static_cast<std::size_t>(key.width) : 0,
                .logical_height = key.height > 0 ? static_cast<std::size_t>(key.height) : 0,
                .allocated_width = static_cast<std::size_t>(value->allocated_width()),
                .allocated_height = static_cast<std::size_t>(value->allocated_height()),
                .bytes = value->size_bytes(),
            });
        });
        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.bytes != rhs.bytes) return lhs.bytes > rhs.bytes;
            return lhs.key.digest() < rhs.key.digest();
        });
        if (entries.size() > limit) entries.resize(limit);
        return entries;
    }
    
    void set_capacity(size_t capacity_bytes);

    bool erase(const NodeCacheKey& key);

private:
    CacheDiagnostics::Handle m_diag_handle;
    FramebufferCache m_cache;
    // Lifetime guard for the lambdas stored in CacheDiagnostics via
    // m_diag_handle.  Set false in ~NodeCache() before m_diag_handle's
    // destructor releases the lambdas, so any late call from the
    // diagnostics thread short-circuits to a no-op instead of touching
    // freed m_cache.
    std::atomic<bool> m_diag_alive{true};
};

} // namespace chronon3d::cache
