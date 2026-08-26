#pragma once

// ---------------------------------------------------------------------------
// tile_execution_policy.hpp
//
// Encapsulates the canonical frame execution decision and produces a
// human-readable reason when the sparse path is disabled. Replaces a block
// of inline boolean logic in scene.cpp that
// had to be read end-to-end to understand why tile execution was skipped.
//
// Reasons produced (when disabled):
//   - "spatial_effect_detected"   : a layer has blur/glow/bloom/drop-shadow/distort/temporal
//   - "dirty_rects_not_active"    : dirty-rect tracking did not enable tile bitmask
//   - "dirty_ratio_too_high"      : dirty screen fraction > dirty.tile_dirty_ratio_threshold
//   - "missing_renderer_executor" : no SoftwareRenderer or no graph executor
//   - "no_dirty_tiles"            : no tile grid / mask available or no dirty tiles
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include "scene_internal.hpp"
#include "scene_fingerprint.hpp"
#include <chronon3d/render_graph/pipeline/execution_decision.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d { class SoftwareRenderer; }

namespace chronon3d::effects {
    class EffectCatalog;
}

namespace chronon3d { class Framebuffer; }

namespace chronon3d::graph {

/// Canonical execution paths selected before backend execution.
/// ExecutionResolver owns this choice; backends only execute the selected path.
enum class FrameExecutionPath : std::uint8_t {
    CopyGop,
    ReuseSurface,
    SparseTiles,
    SparseYuv,
    FullYuv,
    FullRgb,
};

/// Result of a tile-execution policy decision.  When `enabled` is false,
/// `reason_if_disabled` carries a stable snake_case token suitable for logs
/// and metrics.
struct TileDecision {
    bool enabled{false};
    FrameExecutionPath path{FrameExecutionPath::FullRgb};
    bool decode{false};
    bool composite{true};
    bool encode{false};
    std::string_view reason_if_disabled; // empty when enabled
};

/// Complete, backend-neutral execution contract for one frame.  It is the
/// only result consumed by execution coordinators; no backend selects a path.
struct FrameExecutionPlan {
    FrameExecutionPath path{FrameExecutionPath::FullRgb};

    bool decode{false};
    bool render{true};
    bool composite{true};
    bool convert_to_rgb{false};
    bool convert_to_yuv{false};
    bool encode{false};

    std::optional<raster::BBox> dirty_region;
    std::optional<raster::DirtyTileMask> dirty_tiles;

    // Canonical coalesced pixel-space regions produced by the resolver from
    // FrameDeltaCompiler's dirty tile mask. The executor must consume these
    // regions and never coalesce dirty_out a second time.
    std::vector<raster::BBox> dirty_regions;

    // SparseTiles renders into a fresh destination seeded from this previous
    // surface, preserving clean pixels outside dirty_regions. The surface is
    // backend-neutral; CpuRgbSurface is used by the current CPU fallback.
    std::shared_ptr<runtime::RenderSurface> previous_surface;
    std::shared_ptr<Framebuffer> previous_framebuffer;
    bool copy_previous_surface{false};

    // Packet-copy metadata for an untouched, boundary-aligned GOP. The
    // encoder/muxer consumes this only after the same eligibility gates have
    // been certified by ExecutionResolver.
    std::optional<CopyGopPlan> copy_gop_plan;

    std::string_view reason;

    // Whether Clear/executor may restore the previous surface for the selected
    // dirty execution. This is a resolver output, not a scene policy flag.
    bool use_dirty_region{false};
    bool force_full_frame_clear{false};

    // Backend-neutral surface selected by this plan. It is populated for
    // REUSE_SURFACE today; full/sparse paths acquire their concrete surface
    // during execution. `reuse_surface` remains as the CPU compatibility view.
    std::shared_ptr<runtime::RenderSurface> output_surface;

    // Set only for REUSE_SURFACE. The caller may return this surface without
    // building or executing a graph, while still performing a requested encode.
    std::shared_ptr<Framebuffer> reuse_surface;

    // Fingerprint state is carried forward from the early resolver phase so
    // state commit and graph-cache hints use the same canonical decision.
    FrameFingerprints frame_fingerprints{};
    bool scene_structure_unchanged{false};
    bool static_camera_changed{true};
    bool scene_is_static{false};
};

/// ExecutionResolver decides the complete frame execution path before the
/// backend runs it.  The implementation currently owns the tile/dirty-region
/// gates as well as the full-frame fallback decision; backend code must only
/// execute the returned decision.
class ExecutionResolver {
public:
    /// Resolve the initial execution decision directly from the canonical
    /// FrameDelta. This is intentionally limited to ReuseSurface and FullRgb;
    /// sparse/YUV/GOP choices remain in the complete plan resolver.
    [[nodiscard]] static ExecutionDecision resolve_initial(
        const detail::FrameDelta& delta) noexcept;

    /// Resolve the packet-copy fast path from canonical frame delta plus
    /// packet-level evidence. No codec/container checks are duplicated here.
    [[nodiscard]] static ExecutionDecision resolve_copy_gop(
        const detail::FrameDelta& delta,
        const CopyGopEligibility& eligibility) noexcept;

    /// Resolve the pre-dirty reuse candidates and carry their fingerprints into
    /// the final plan.  This owns the existing resolved-scene and static-scene
    /// fast paths; callers only inspect `reuse_surface`.
    static FrameExecutionPlan resolve_early_reuse(
        const RenderGraphContext& ctx,
        const Scene& scene,
        Frame frame,
        int width,
        int height,
        SoftwareRenderer* sw_renderer);

    /// Complete the plan after FrameDeltaCompiler has produced dirty output.
    /// This owns empty-dirty reuse, sparse-tile selection, and full-frame
    /// fallback, while preserving the existing policy gates and reasons.
    [[nodiscard]] static std::vector<raster::BBox> coalesce_dirty_regions(
        const raster::TileGrid& grid,
        const raster::DirtyTileMask& mask);

    static FrameExecutionPlan resolve(
        FrameExecutionPlan plan,
        const detail::LayerResolutionResult& resolved,
        const Scene& scene,
        const Camera2_5D& camera,
        const RenderSettings& settings,
        const detail::DirtyRectOutput& dirty_out,
        double dirty_ratio,
        SoftwareRenderer* sw_renderer,
        Frame frame,
        int width,
        int height,
        const effects::EffectCatalog* effect_catalog = nullptr,
        bool encode_requested = false,
        bool diagnostics_enabled = false,
        runtime::PixelFormat output_format = runtime::PixelFormat::Rgba32Float,
        const std::optional<CopyGopEligibility>& copy_gop = std::nullopt);

    /// Compatibility adapter for older tile-only callers.  New code should
    /// consume FrameExecutionPlan directly.
    static TileDecision decide(
        const detail::LayerResolutionResult& resolved,
        const RenderSettings& settings,
        const detail::DirtyRectOutput& dirty_out,
        double dirty_ratio,
        const SoftwareRenderer* sw_renderer,
        Frame frame,
        const effects::EffectCatalog* effect_catalog = nullptr
    );
};

/// Compatibility name for older pipeline call sites.  There is deliberately
/// no second policy implementation: ExecutionResolver is the sole owner of
/// the decision.
using TileExecutionPolicy = ExecutionResolver;



} // namespace chronon3d::graph
