#pragma once

// ---------------------------------------------------------------------------
// render_graph_context.hpp
//
// RenderGraphContext is the per-frame state passed to every graph node and
// the executor.  It used to be a "God object" with 30+ flat fields, so it
// has been split into 7 sub-structs by responsibility.  The top-level
// RenderGraphContext is now a thin composition:
//
//   struct RenderGraphContext {
//       RenderFrameInfo            frame;      // frame number, time, fps, width, height
//       RenderCameraContext        camera;     // 2D + 2.5D camera, projection, lighting
//       RenderResourceContext      resources;  // backend, caches, pool, registry, video
//       RenderOptimizationContext  options;    // optimization + structure-unchanged flags
//       RenderTelemetryContext     telemetry;  // counters, profiler, telemetry vectors
//       RenderTileContext          tile;       // tile size, clip rect, early-exit, dirty_rect
//       RenderScratchContext       scratch;    // transform/ping scratch + reusable inputs
//   };
//
// Naming notes:
//   - The sub-struct members use the same names as the top-level struct's old
//     fields (frame, camera, tile, etc.) so that the resulting paths read
//     naturally:  ctx.frame.frame  (= frame number)
//                 ctx.camera.camera (= 2D Camera)
//                 ctx.tile.tile_size
//     The inner `frame` / `camera` repeats are intentional — they keep the
//     inner-field names that were already in use everywhere in the codebase
//     and avoid a second rename wave.
// ---------------------------------------------------------------------------

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/core/memory/framebuffer_slot_view.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

// TICKET-007 - render_graph_context.hpp is a public SDK header consumed
// by graph-node TU families (transform_node, multi_source_node, etc.).
// We forward-declare DebugConfig so the new member
// `RenderOptimizationContext::debug_config` is a complete *pointer* type
// here.  Callers that actually dereference it (e.g. scene.cpp,
// glow_pipeline.cpp) MUST include <chronon3d/core/config.hpp>
// themselves.  This keeps the SDK header lightweight.
namespace chronon3d { class DebugConfig; }

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <span>
#include <atomic>

namespace chronon3d {
    class CompositionRegistry;
    struct RenderCounters;
    class TransformScratchBuffer;
    class Scene;
    class DebugConfig;  // TICKET-007: forward-declared above; re-export here so
                        // that downstream code using chronon3d::DebugConfig via
                        // this header finds it at the right namespace scope.
    class ExecutionScheduler;  // PR-B: thread-pool authority passed via Resources.
}

namespace chronon3d::media {
    class MediaFrameProvider;
}

namespace chronon3d::cache {
    class NodeCache;
    class FramebufferPool;
}

namespace chronon3d::effects {
    class EffectCatalog;
}

namespace chronon3d::graph {

class RenderBackend;
class RenderProfiler;
class CompiledGraphCache;
class RenderGraphNode;

// ── Per-frame immutable-ish info: frame number, time, dimensions ───────────
struct RenderFrameInfo {
    Frame frame{0};
    SampleTime sample_time{};         // continuous sub-frame time coordinate
    TemporalSampleKey temporal_key{};    // quantised cache key (frame + subframe_tick + version)
    float time_seconds{0.0f};         // wall-clock seconds = frame / fps
    float fps{30.0f};
    int width{0};
    int height{0};
    std::string assets_root;  // per-composition assets directory (thread-safe resolve)
};

// ── Camera + projection + lighting (everything geometry-related) ──────────
struct RenderCameraContext {
    Camera camera{};                                  // 2D camera
    Camera2_5D camera_2_5d{};                         // 2.5D camera
    bool has_camera_2_5d{false};
    renderer::ProjectionContext projection_ctx{};      // pre-built from camera_2_5d
    rendering::LightContext light_context{};
};

// Forward-declared for use in RenderResourceContext callback signatures.
struct RenderGraphContext;

// ── External resources (backend, caches, pools, registries) ───────────────
struct RenderResourceContext {
    RenderBackend* backend{nullptr};
    cache::NodeCache* node_cache{nullptr};
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool;
    CompiledGraphCache* compiled_graph_cache{nullptr};
    const CompositionRegistry* registry{nullptr};
    media::MediaFrameProvider* video_decoder{nullptr};

    /// Factory: builds + compiles a nested scene program on cache miss.
    /// Populated by graph_pipeline (register_pipeline_nodes).
    /// Signature: (scene, ctx) -> unique_ptr<CompiledSceneProgram>
    std::function<std::unique_ptr<class CompiledSceneProgram>(
        const chronon3d::Scene&, RenderGraphContext&)> precomp_build;

    /// Node catalog for creating graph nodes by id (e.g. source.precomp).
    /// Populated by register_pipeline_nodes, consumed by graph builder.
    const class GraphNodeCatalog* node_catalog{nullptr};

    /// Effect catalog for creating effect nodes by type.
    /// Populated at pipeline init, consumed by graph builder + dirty safety.
    const class effects::EffectCatalog* effect_catalog{nullptr};

    // ── PR-B: ExecutionScheduler * *scheduler ────────────────────────────
    /// Non-owning pointer to the lifetime-bound ExecutionScheduler that
    /// owns the tbb::task_arena for the current render process.  Populated
    /// by `src/render_graph/pipeline/scene.cpp` from the owning
    /// SoftwareRenderer so that PrecompNode (which has no direct access to
    /// the renderer) can route its inner execute() through the same arena.
    /// Required for nested graph execution (Precomp); null is permitted
    /// for diagnostic-only graphs that never execute.
    class chronon3d::ExecutionScheduler* scheduler{nullptr};
};

// ── Optimization flags + structure-unchanged hints ───────────────────────
struct RenderOptimizationContext {
    /// TICKET-007 — per-instance DebugConfig pointer forwarded from
    /// the owning SoftwareRenderer (populated in
    /// `src/render_graph/pipeline/scene.cpp`).  Replaces the removed
    /// process-wide `detail::g_debug_config`.  When nullptr, debug
    /// overlays (text-bbox, glow debug artefacts, future ink-bounds
    /// overlays) are skipped — matching the safe default for
    /// non-software backends and shared tests that build a context
    /// without populating it.
    const chronon3d::DebugConfig* debug_config{nullptr};

    bool cache_enabled{true};
    bool diagnostics_enabled{false};
    float ssaa_factor{1.0f};
    bool modular_coordinates{false};

    // In-place composition / dirty-rect / framebuffer reuse / skip-clear
    bool optimize_compositing{true};
    bool dirty_rects_enabled{false};
    bool reuse_prev_framebuffer{false};
    bool skip_initial_clear{false};

    // Graph topology / DOF tracking hints
    bool graph_structure_unchanged{false};
    bool track_dof_depth{false};

    // SceneProgramCache capacity for nested Precomp nodes
    size_t program_cache_capacity{0};

    // Auto-tuning for SceneProgramCache
    bool   program_cache_tune{false};
    size_t program_cache_tune_interval{30};
    size_t program_cache_tune_min_capacity{2};
    size_t program_cache_tune_max_capacity{128};
};

// ── Counters, profiler, telemetry record vectors, DOF depth map ───────────
struct RenderTelemetryContext {
    chronon3d::RenderCounters* counters{nullptr};
    RenderProfiler* profiler{nullptr};

    std::vector<chronon3d::telemetry::NodeTelemetryRecord>  node_telemetry;
    std::vector<chronon3d::telemetry::LayerTelemetryRecord> layer_telemetry;

    // Per-pixel DOF depth tracking (mirrors options.track_dof_depth)
    std::vector<float> dof_depth;
};

// ── Tile size, clip rects, early-exit mask, dirty-rect ────────────────────
struct RenderTileContext {
    int tile_size{0};
    std::optional<raster::BBox> clip_rect;

    // Tile-based execution mode (Branch 3+): when true, the executor should
    // include tile coordinates in cache keys to prevent cross-tile staleness.
    bool tile_execution_enabled{false};
    std::optional<raster::BBox> active_tile_clip;

    // Per-node "skip" mask (set during build, consumed by executor)
    std::vector<bool> early_exit_skip;

    // Union of all dirty bboxes for this frame.
    std::optional<raster::BBox> dirty_rect;
};

// ── Transform scratch + ping-pong write slot + reusable-inputs cache ──────
struct RenderScratchContext {
    // ── Transform scratch buffer ────────────────────────────────────────
    // Persistent buffer reused by TransformNode across frames.  See
    // SoftwareRenderer for ownership details.  Mutable because the const
    // acquire methods need to mark the scratch as "in use" while an
    // OwnedFB is alive; the deleter restores it when released.
    mutable FramebufferSlotView transform_scratch;

    // ── Ping-pong framebuffers ───────────────────────────────────────────
    // See SoftwareRenderer for the ping-pong protocol.  Same mutability
    // rationale as transform_scratch.
    mutable FramebufferSlotView ping_write;

    // Track reusable inputs to allow in-place swap optimization during node
    // execution.  Mutable for the same reason as the scratch pointers.
    mutable std::vector<Framebuffer*> reusable_inputs;
};

struct RenderGraphContext {
    RenderFrameInfo            frame{};
    RenderCameraContext        camera{};
    RenderResourceContext      resources{};
    RenderOptimizationContext  options{};
    RenderTelemetryContext     telemetry{};
    RenderTileContext          tile{};
    RenderScratchContext       scratch{};

    /// Acquire a framebuffer as an OwnedFB (unique_ptr with pool deleter).
    OwnedFB acquire_owned_fb(
        int w,
        int h,
        bool clear = true,
        std::optional<raster::BBox> bounds = std::nullopt,
        std::atomic<uint64_t>* specific_clear_ms = nullptr
    ) const;

    /// Copy an existing framebuffer into a new OwnedFB.
    OwnedFB acquire_owned_fb(const Framebuffer& other);

    /// Adopt a uniquely-owned shared_ptr<Framebuffer> into an OwnedFB without copying pixel data.
    OwnedFB acquire_owned_fb(std::shared_ptr<Framebuffer>&& src);

    /// Acquire the transform scratch buffer as an OwnedFB.
    OwnedFB acquire_scratch_fb(
        int w,
        int h,
        bool clear = true,
        std::optional<raster::BBox> bounds = std::nullopt
    ) const;

    /// Convert an OwnedFB to a shared_ptr for cache storage.
    static CachedFB own_to_cache(OwnedFB& owned, cache::FramebufferPool* pool);

    /// Legacy shared_ptr acquire — still needed for cache interaction.
    std::shared_ptr<Framebuffer> acquire_framebuffer(
        int w,
        int h,
        bool clear = true,
        std::optional<raster::BBox> bounds = std::nullopt,
        std::atomic<uint64_t>* specific_clear_ms = nullptr
    ) const;

    std::shared_ptr<Framebuffer> acquire_framebuffer(const Framebuffer& other) const;

    /// Lightweight copy for per-node execution.  Skips copying large vectors
    /// (telemetry.node_telemetry, telemetry.layer_telemetry, telemetry.dof_depth,
    /// tile.early_exit_skip) that are not read during node.execute(),
    /// reducing per-node copy overhead from O(n) heap allocations to a
    /// handful of pointer/POD copies.
    RenderGraphContext clone_for_node_execution() const;

    /// Resolve a relative asset path using the per-context assets root.
    /// Uses ctx.frame.assets_root; returns the relative_path unchanged if
    /// empty, absolute, or if no assets_root is set.
    [[nodiscard]] std::string resolve_asset(const std::string& relative_path) const;
};

} // namespace chronon3d::graph
