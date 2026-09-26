#pragma once

// ---------------------------------------------------------------------------
// effects/glow_pipeline.hpp
//
// Unified glow pipeline. Before this header the codebase had three parallel
// glow paths:
//   1. software effect processing (framebuffer-based, GlowParams/BloomParams)
//   2. text_glow.cpp             (BLImage-based, Glow fields on RenderNode)
//
// Both routes share the same conceptual pipeline:
//   - extract the source coverage mask (or text alpha)
//   - apply one colorized Gaussian blur
//   - composite the halo with the source
//
// GlowPipeline is a unified config struct that captures every parameter any
// of the four paths need. The existing param structs (GlowParams,
// BloomParams, DropShadowParams) keep their identity — `from()` converters
// produce a fully-populated GlowPipeline, and `run_glow_pipeline()`
// dispatches on `mode` and runs the appropriate code path.
//
// All glow authoring reaches this pipeline through GlowParams; there is no
// geometry-only glow fallback.
//
// New unified entry point (Item 17):
//   GlowPipeline::render(ctx, input) → GlowPipelineOutput
// Software effect processing and text_glow.cpp use this single entry point
// so that layer glow and text glow share the same blur/accumulate/
// composite code path.  Text glow still builds its own padded alpha mask
// (the text-specific step) and passes source_is_alpha_mask = true.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>
#include <optional>
#include <vector>
#include <algorithm>
#include <array>

namespace chronon3d::graph {
    struct RenderGraphContext;
}

namespace chronon3d { class DebugConfig; }   // TICKET-007: per-instance debug gating (replaces detail::g_debug_config)

namespace chronon3d {

// ── Input descriptor for GlowPipeline::render() ──────────────────────
struct GlowPipelineInput {
    /// Source framebuffer to glow.  When source_is_alpha_mask is true,
    /// only the alpha channel is used as the trigger; RGB is ignored.
    /// The framebuffer is NOT modified in-place — the composited result
    /// is returned as a new framebuffer.
    const Framebuffer* source{nullptr};

    /// Optional clip rect (in source coordinates).
    std::optional<raster::BBox> clip;

    /// Glow parameters (converted to GlowPipeline internally).
    GlowParams params;

    /// When true, the source is treated as a pre-built alpha mask
    /// (white-on-transparent).  The trigger is taken directly from
    /// source alpha instead of computing luminance-based threshold.
    /// Used by text_glow.cpp for its padded alpha mask.
    bool source_is_alpha_mask{false};
};

// ── Output descriptor for GlowPipeline::render() ─────────────────────
struct GlowPipelineOutput {
    /// The output framebuffer.
    /// - For standard callers: source + glow composited together.
    /// - When source_is_alpha_mask=true (text glow): contains ONLY the
    ///   accumulated glow light (no source compositing), so the caller
    ///   can composite it behind the text.
    /// Dimensions match the input source framebuffer.
    OwnedFB composited;

    /// Bounding box of the glow-affected region (source coordinates).
    raster::BBox affected_bounds;
};

struct GlowPipeline {
    // A glow is one tinted Gaussian halo.
    Color color{1, 1, 1, 1};
    f32 radius{24.0f};
    f32 intensity{0.25f};

    static GlowPipeline from(const GlowParams& p);

    /// Unified entry point (Item 17).  Renders glow for a source
    /// framebuffer and returns the composited result (source + glow
    /// already applied).
    ///
    /// Internally converts GlowParams → GlowPipeline and runs the same
    /// single-Gaussian halo used by software effect processing.
    static GlowPipelineOutput render(class graph::RenderGraphContext& ctx,
                                     const GlowPipelineInput& input);
};

/// Compute the maximum spatial extent (in pixels) that a glow effect
/// can reach beyond its source geometry.  Used for bounding-box prediction
/// and clipping decisions in the render graph.
[[nodiscard]] inline f32 glow_effect_extent(const GlowParams& p) {
    return effects::spatial_halo_extent(effects::effect_spatial_halo(p));
}

/// Overload for the single GlowPipeline configuration.
[[nodiscard]] inline f32 glow_effect_extent(const GlowPipeline& p) {
    return effects::spatial_halo_extent(effects::effect_spatial_halo(
        GlowParams{.radius = p.radius}));
}

} // namespace chronon3d

// ── Bench/test-exposed glow helper ────────────────────────────────────────────
// Accumulation is exposed for microbenchmarks; production callers use
// GlowPipeline::render or renderer::run_glow_pipeline.
namespace chronon3d::renderer {
void accumulate_glow_pass(Framebuffer& dst, const Framebuffer& src,
                          const GlowPipeline& p);
} // namespace chronon3d::renderer

namespace chronon3d::renderer {

/// Run one single-Gaussian Glow halo. Bloom remains a separate effect and
/// is handled by the dedicated Bloom processor.
/// When source_is_alpha_mask is true, the trigger is taken directly
/// from source alpha (used by text glow).
/// `debug_cfg` is the per-instance DebugConfig forwarded from the
/// owning RenderEngine / SoftwareRenderer (TICKET-007 — replaces the
/// removed process-wide `detail::g_debug_config`).  When nullptr,
/// per-pass debug artifacts (debug_glow_source.png / debug_glow_pass_*.png /
/// debug_glow_accumulated.png) are skipped.  See TICKET-007 for the
/// propagation path through RenderGraphContext::options::debug_config.
void run_glow_pipeline(Framebuffer& fb, const GlowPipeline& p,
                       const std::optional<raster::BBox>& clip,
                       bool source_is_alpha_mask = false,
                       const chronon3d::DebugConfig* debug_cfg = nullptr,
                       std::vector<float>* kernel_scratch = nullptr);

void run_bloom_effect(Framebuffer& fb, const BloomParams& params,
                      const std::optional<raster::BBox>& clip);

} // namespace chronon3d::renderer
