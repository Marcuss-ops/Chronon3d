#pragma once

// ---------------------------------------------------------------------------
// render_graph_context.hpp
//
// TICKET-010 — `chronon3d::graph::RenderGraphContext` is the per-frame state
// passed to every graph node + the executor.  It used to be a "God object"
// with 30+ flat fields split into 7 nested sub-contexts
// (RenderFrameInfo / RenderCameraContext / RenderResourceContext /
// RenderOptimizationContext / RenderTelemetryContext / RenderTileContext /
// RenderScratchContext).  TICKET-010 reduces the 7 to exactly 4 typed
// sub-contexts grouped by responsibility:
//
//   struct RenderGraphContext {
//       FrameInput           frame_input;  // engine-generic per-frame data:
//                                          // frame id, time, dimensions,
//                                          // camera, projection, lighting
//       RenderPolicy         policy;       // user-tunable per-job flags:
//                                          // diagnostics, ssaa, modular,
//                                          // dirty-rect, skip-clear, tile
//                                          // size, program-cache tuning
//       RenderServices       services;     // service-locator pointers +
//                                          // catalog pointers + typed
//                                          // PrecompBuilderService (was
//                                          // precomp_build std::function)
//       NodeExecutionContext node_exec;    // per-frame/per-node mutable
//                                          // workspace: counters, profiler,
//                                          // telemetry vectors, tile clip
//                                          // rect, early-exit mask, dirty
//                                          // rect, transform scratch,
//                                          // ping-pong slot, reusable inputs
//   };
//
// The top-level RenderGraphContext is a thin composition holding these 4
// sub-contexts + facade methods (`acquire_owned_fb`, `acquire_framebuffer`,
// `clone_for_node_execution`, `resolve_asset`) that delegate to
// `services.framebuffer_pool` / `node_exec.scratch` as appropriate.
//
// Naming convention:
//   - The inner sub-context field NAMES give the path-natural access:
//       ctx.frame_input.frame          (= frame number)
//       ctx.frame_input.camera         (= 2D Camera)
//       ctx.policy.tile_size           (= tile size)
//       ctx.services.backend           (= RenderBackend*)
//       ctx.services.precomp_builder   (= typed builder; was std::function)
//       ctx.node_exec.telemetry        (= per-frame telemetry vectors)
//       ctx.node_exec.transform_scratch (= ping-pong scratch view)
//     The `frame_input.frame` two-step is intentional: it preserves the
//     `RenderFrameInfo::frame` member name (a `Frame` id type), avoiding a
//     second rename wave across graph-node and pipeline callers.
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

// TICKET-007 / TICKET-010 — render_graph_context.hpp is a public SDK header
// consumed by graph-node TU families (transform_node, multi_source_node,
// etc.).  DebugConfig is forward-declared here so the canonical sub-field
// `RenderPolicy::debug_config` is a complete *pointer* type.  Callers that
// actually dereference it (e.g. scene.cpp, glow_pipeline.cpp) MUST include
// <chronon3d/core/config.hpp> themselves.  This keeps the SDK header
// lightweight.
namespace chronon3d { class DebugConfig; }

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <span>
#include <atomic>
#include <chronon3d/render_graph/core/node_identity.hpp>

namespace chronon3d {
    class CompositionRegistry;
    struct RenderCounters;
    class Scene;
    class DebugConfig;  // re-exported; see forward-declare above
    class ExecutionScheduler;  // TICKET-011 / PR-B — typed scheduler ptr on RenderServices
    struct RenderSession;  // PR-5 — session ptr on RenderServices for PrecompNode inner exec
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

// WP-8 PR 8.0 — typed engine-local asset resolver lives on the per-
// engine RenderRuntime.  Forward-declared at FILE SCOPE here so the
// `RenderServices::asset_resolver` pointer below can hold a non-owning
// reference; the full definition is in <chronon3d/assets/asset_resolver.hpp>.
// Callers that dereference the pointer MUST include that header
// themselves; the SDK header stays lightweight.
//
// IMPORTANT: this declaration MUST stay OUTSIDE `namespace
// chronon3d::graph { ... }`.  C++17 nested-namespace-declaration syntax
// (`namespace A::B {}`) is shorthand for `namespace A { namespace B {} }`,
// so writing `namespace chronon3d::assets {}` inside `chronon3d::graph {}`
// would create `chronon3d::graph::chronon3d::assets`, opening a new inner
// `chronon3d` namespace that SHADOWS the global one for the rest of the
// enclosing block — breaking every subsequent `chronon3d::X` lookup
// (the 44-error `in namespace chronon3d::graph::chronon3d does not name a
// type` cascade).
namespace chronon3d::assets { class AssetResolver; }

namespace chronon3d::graph {

class RenderBackend;
class RenderProfiler;
class CompiledGraphCache;
class CompiledSceneProgram;
class RenderGraphNode;
class PrecompBuilderService;

// ── Per-frame input: frame id, time, dimensions, camera, projection ─────────
// Engine-generic, immutable-ish per frame.  Replaces the 7-substruct pair
// (RenderFrameInfo + RenderCameraContext) folded together.
struct FrameInput {
    Frame frame{0};
    SampleTime sample_time{};
    TemporalSampleKey temporal_key{};
    float time_seconds{0.0f};
    float fps{30.0f};
    int width{0};
    int height{0};
    std::string assets_root;  // per-composition assets directory

    // Camera + projection + lighting (folded RenderCameraContext).
    Camera camera{};
    Camera2_5D camera_2_5d{};
    bool has_camera_2_5d{false};
    renderer::ProjectionContext projection_ctx{};
    rendering::LightContext light_context{};
};

// ── Per-job policy: flags + optimization shortcuts + tile plan ─────────────
// Replaces RenderOptimizationContext + the `tile_size`/`tile_execution_enabled`
// pair from RenderTileContext.
struct RenderPolicy {
    /// TICKET-007 — per-instance DebugConfig pointer forwarded from the
    /// owning SoftwareRenderer (populated in
    /// src/render_graph/pipeline/scene.cpp).  Replaces the removed
    /// process-wide `detail::g_debug_config`.  When nullptr, debug
    /// overlays (text-bbox, glow debug artefacts, future ink-bounds
    /// overlays) are skipped.
    const chronon3d::DebugConfig* debug_config{nullptr};

    bool cache_enabled{true};
    bool diagnostics_enabled{false};
    float ssaa_factor{1.0f};
    bool modular_coordinates{false};

    // Composition / dirty-rect / framebuffer-reuse / skip-clear
    bool optimize_compositing{true};
    bool dirty_rects_enabled{false};
    bool reuse_prev_framebuffer{false};
    bool skip_initial_clear{false};

    // Graph topology / DOF tracking hints
    bool graph_structure_unchanged{false};
    bool track_dof_depth{false};

    // SceneProgramCache tuning (passed to nested Precomp nodes)
    size_t program_cache_capacity{0};
    bool   program_cache_tune{false};
    size_t program_cache_tune_interval{30};
    size_t program_cache_tune_min_capacity{2};
    size_t program_cache_tune_max_capacity{128};

    // Tile plan (was RenderTileContext::tile_size + tile_execution_enabled).
    int  tile_size{0};
    bool tile_execution_enabled{false};
};

// ── Service locator: long-lived backends, caches, catalogs, builder ─────────
// Replaces RenderResourceContext.  The legacy std::function field
// `precomp_build` is replaced by a typed `precomp_builder*` pointer to a
// `PrecompBuilderService` (typically DefaultPrecompBuilder) owned by
// `PipelineCatalogs`.

struct RenderServices {
    RenderBackend* backend{nullptr};
    cache::NodeCache* node_cache{nullptr};
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool;
    CompiledGraphCache* compiled_graph_cache{nullptr};
    const CompositionRegistry* registry{nullptr};
    media::MediaFrameProvider* video_decoder{nullptr};

    /// Node catalog — populated by `wire_catalog_pointers`, consumed by
    /// graph builder (source.precomp factory lookup).
    const class GraphNodeCatalog* node_catalog{nullptr};

    /// Effect catalog — populated by `wire_catalog_pointers`, consumed by
    /// graph builder + dirty safety.
    const class effects::EffectCatalog* effect_catalog{nullptr};

    /// TICKET-010 — typed alternative to the legacy `precomp_build`
    /// std::function.  Set by `wire_catalog_pointers(ctx, catalogs)`
    /// from `catalogs.precomp_builder.get()`.  When nullptr, callers
    /// MUST treat the build as unavailable and return empty output.
    const class PrecompBuilderService* precomp_builder{nullptr};

    /// TICKET-010 / TICKET-011 — PR-B scheduler accessor.  PrecompNode
    /// dereferences `*ctx.services.scheduler` to route its inner execute()
    /// through the same arena as the parent graph.  Set by
    /// `wire_catalog_pointers` from `SoftwareRenderer::scheduler()`
    /// (which forwards to `RenderRuntime::scheduler()`).  Lifetime is
    /// pinned to the owning runtime; never dereference past shutdown.
    chronon3d::ExecutionScheduler* scheduler{nullptr};

    /// PR-5 / PR-2-rewire — parent render session pointer.  PrecompNode
    /// borrows the session's arena (via `session->arena()`) for inner
    /// graph execution PMR allocations, the session's `services.executor`
    /// for inner executor calls, and the session's `program_store` for
    /// cache lookups.  Set by scene.cpp when a SoftwareRenderer is
    /// available.  Null in test paths without a wired session.
    chronon3d::RenderSession* session{nullptr};

    /// PR-9 — opaque sidecar pointer to the owning SoftwareRenderer.
    /// Set by scene.cpp / composition.cpp alongside `session` so that
    /// render-graph nodes (ClearNode, etc.) can reach software-specific
    /// resources (buffer_ring) without pulling SoftwareRenderer into
    /// the public SDK header.  Null when no software renderer is active.
    /// Callers must static_cast to SoftwareRenderer* after including
    /// the full software_renderer.hpp header.
    void* sw_renderer_sidecar{nullptr};

    /// WP-8 PR 8.0 — typed engine-local asset path resolver (non-owning
    /// pointer into the owning RenderRuntime's resolver).  Set by
    /// `preflight.cpp::debug_preflight_render_graph` (and any future
    /// graph-building surface that needs to resolve relative asset
    /// paths through the same instance the renderer uses).  Null when
    /// no resolver has been wired into the context — callers MUST
    /// null-check before dereferencing.
    /// NOTE: callers that dereference `asset_resolver` MUST
    /// `#include <chronon3d/assets/asset_resolver.hpp>` themselves;
    /// the SDK header stays lightweight (forward decl only above).
    chronon3d::assets::AssetResolver* asset_resolver{nullptr};
};

// ── Per-frame / per-node mutable workspace ──────────────────────────────────
// Replaces RenderTelemetryContext + the tile-clip+dirty_rect subset of
// RenderTileContext + the full RenderScratchContext.
struct NodeExecutionContext {
    // ── Telemetry (was RenderTelemetryContext) ─────────────────────────
    chronon3d::RenderCounters* counters{nullptr};
    RenderProfiler* profiler{nullptr};

    std::vector<chronon3d::telemetry::NodeTelemetryRecord>  node_telemetry;
    std::vector<chronon3d::telemetry::LayerTelemetryRecord> layer_telemetry;

    // Per-pixel DOF depth tracking (mirrors policy.track_dof_depth).
    std::vector<float> dof_depth;

    // ── Tile clip / early-exit / dirty rect (was RenderTileContext) ───
    std::optional<raster::BBox> clip_rect;

    // Tile-based execution mode (Branch 3+): when true, the executor
    // includes tile coordinates in cache keys to prevent cross-tile
    // staleness.
    std::optional<raster::BBox> active_tile_clip;

    // Per-node "skip" mask (set during build, consumed by executor).
    std::vector<bool> early_exit_skip;

    // Union of all dirty bboxes for this frame.
    std::optional<raster::BBox> dirty_rect;

    // ── Scratch (was RenderScratchContext) ─────────────────────────────
    // Mutable because the const acquire methods need to mark the scratch
    // as "in use" while an OwnedFB is alive; the deleter restores it
    // when released.
    mutable FramebufferSlotView transform_scratch;
    mutable FramebufferSlotView ping_write;
    mutable std::vector<Framebuffer*> reusable_inputs;

    // ── Zero-copy bottom-input ownership transfer (composite hot-path) ──
    // Populated by `execute_single_node` for the FIRST input (j==0,
    // i.e. the "bottom" in CompositeNode terminology) when reusable
    // conditions are met: `consumer_remaining == 1 && state.temp[input_id].use_count() == 1`.
    //
    // Consumed by `acquire_owned_fb(const Framebuffer&)` to perform a
    // 1×1-placeholder pixel swap with the ORIGINAL PoolFbDeleter from
    // `state.temp[input_id]`, instead of an ~8 MB pool acquire + memcpy.
    // The cache_evaluator's CompositeNode cache-skip ensures this slot
    // is reliably populated for chained composites (no node_cache ref
    // would otherwise inflate use_count above 1).
    //
    // Default-constructed (null); reset to {} at the start of each
    // node's reusable_inputs loop in execute_single_node.
    //
    // Note: NOT declared `mutable` because `acquire_owned_fb(const FB&)`
    // is non-const (unlike `reusable_inputs` which is read from the
    // const acquire methods).
    CachedFB reusable_bottom;

    // ── WP 4.3 — current node identity ─────────────────────────────────
    // Set by `GraphExecutor::execute_single_node` immediately before
    // delegating to `node->execute(...)`.  Identity is the
    // `(GraphInstanceId, StableNodeId)` pair the executing node was
    // compiled with, so production code paths can read a stable
    // identity from the execution context (instead of reaching back
    // into the compiled graph's node metadata).
    //
    // Default value is `kInvalid*Id` for both halves, which PrecompNode
    // uses as the "no executor-was-here" fallback (the assertion is
    // relaxed for test paths that drive a PrecompNode without first
    // running it through the full GraphExecutor).
    NodeIdentity current_identity{kInvalidGraphInstanceId, kInvalidStableNodeId};
};

// Forward-declared for use in RenderServices callback signatures.
struct RenderGraphContext;

// TICKET-010 — backward-compat type aliases for the legacy 7-substruct names.
// The runtime node-base `OpacityEvaluator` and a handful of older test
// helpers still pass `RenderFrameInfo` by const-ref.  `FrameInput` is a
// strict superset of the old `RenderFrameInfo` fields (frame id, sample
// time, temporal key, time_seconds, fps, width, height, assets_root) plus
// the folded-in camera + projection + lighting fields.  The alias keeps
// pre-existing code compiling without a synchronous rename wave; callers
// that want the canonical name should use `FrameInput`.
using RenderFrameInfo = FrameInput;

struct RenderGraphContext {
    FrameInput           frame_input{};
    RenderPolicy         policy{};
    RenderServices       services{};
    NodeExecutionContext node_exec{};

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

    /// Adopt a uniquely-owned shared_ptr<Framebuffer> into an OwnedFB without
    /// copying pixel data.
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

    /// Lightweight copy for per-node execution.  Skips copying large
    /// vectors (node_exec.node_telemetry, node_exec.layer_telemetry,
    /// node_exec.dof_depth, node_exec.early_exit_skip,
    /// node_exec.reusable_inputs) that are not read during node.execute();
    /// reduces per-node copy overhead from O(n) heap allocations to
    /// a handful of pointer/POD copies.
    RenderGraphContext clone_for_node_execution() const;

    /// Resolve a relative asset path using ctx.frame_input.assets_root.
    /// Returns relative_path unchanged if empty, absolute, or no
    /// assets_root is set.
    [[nodiscard]] std::string resolve_asset(const std::string& relative_path) const;
};

} // namespace chronon3d::graph
