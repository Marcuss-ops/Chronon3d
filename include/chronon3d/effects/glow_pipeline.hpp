#pragma once

// ---------------------------------------------------------------------------
// effects/glow_pipeline.hpp
//
// Unified glow pipeline. Before this header the codebase had three parallel
// glow paths:
//   1. apply_glow_effect()    (framebuffer-based, GlowParams)
//   2. apply_bloom_effect()   (framebuffer-based, BloomParams)
//   3. text_glow.cpp          (BLImage-based, Glow fields on RenderNode)
//   4. draw_glow()            (5 expanded-shape passes, no blur fast path)
//
// All four share the same conceptual pipeline:
//   - extract trigger mask (alpha / brightness / text alpha)
//   - build N concentric colorized blur layers
//   - accumulate additively into a single buffer
//   - composite the accumulated buffer with the source
//
// GlowPipeline is a unified config struct that captures every parameter any
// of the four paths need. The existing param structs (GlowParams,
// BloomParams, DropShadowParams) keep their identity — `from()` converters
// produce a fully-populated GlowPipeline, and `run_glow_pipeline()`
// dispatches on `mode` and runs the appropriate code path.
//
// `draw_glow` (the 5 expanded-shape node primitive) intentionally stays
// outside this pipeline — it is a fast path with no blur, useful for small
// radii where the blur cost is not amortised.
//
// New unified entry point (Item 17):
//   GlowPipeline::render(ctx, input) → GlowPipelineOutput
// Both apply_glow_effect() and text_glow.cpp should use this single entry
// point so that layer glow and text glow share the same blur/accumulate/
// composite code path.  Text glow still builds its own padded alpha mask
// (the text-specific step) and passes source_is_alpha_mask = true.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <optional>
#include <vector>
#include <algorithm>

namespace chronon3d::graph {
    struct RenderGraphContext;
}

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
    /// Which implementation path to use.
    /// - Layer:  multi-layer glow (used by apply_glow_effect)
    /// - Bloom:  bright-pass + blur + additive (used by apply_bloom_effect)
    /// - Shadow: reserved for future apply_shadow_effect routing
    enum class Mode {
        Layer,
        Bloom,
        Shadow,
    };

    Mode mode{Mode::Layer};

    // ── Shared visual params ────────────────────────────────────────
    Color color{1,1,1,1};
    f32 radius{15.0f};
    f32 intensity{0.8f};
    f32 threshold{0.0f};
    f32 spread{1.0f};
    f32 softness{1.0f};
    f32 falloff{0.85f};
    f32 core_strength{0.70f};
    f32 aura_strength{0.35f};
    f32 bloom_strength{0.18f};
    f32 outer_downscale{0.25f};
    bool preserve_source{true};
    bool additive{true};
    BlendMode blend{BlendMode::Add};

    // ── Per-layer table (empty = use synthetic core/aura/bloom) ────
    std::vector<GlowLayer> layers;

    // ── Quality routing for SkiaLike vs standard multi-pass ─────────
    GlowQuality quality{GlowQuality::Standard};

    // ── Convex converters from existing param structs ───────────────
    static GlowPipeline from(const GlowParams& p);
    static GlowPipeline from(const BloomParams& p);
    static GlowPipeline from(const DropShadowParams& p);

    /// Unified entry point (Item 17).  Renders glow for a source
    /// framebuffer and returns the composited result (source + glow
    /// already applied).
    ///
    /// Internally converts GlowParams → GlowPipeline and dispatches
    /// to the same run_layer_mode / run_bloom_mode paths used by
    /// apply_glow_effect / apply_bloom_effect.
    static GlowPipelineOutput render(class graph::RenderGraphContext& ctx,
                                     const GlowPipelineInput& input);
};

/// Compute the maximum spatial extent (in pixels) that a glow effect
/// can reach beyond its source geometry.  Used for bounding-box prediction
/// and clipping decisions in the render graph.
[[nodiscard]] inline f32 glow_effect_extent(const GlowParams& p) {
    f32 base_radius = std::max(0.0f, p.radius);
    if (!p.layers.empty()) {
        f32 max_layer_r = 0.0f;
        for (const auto& l : p.layers) {
            max_layer_r = std::max(max_layer_r, l.radius);
        }
        base_radius = max_layer_r;
    }
    const f32 radius = base_radius * std::max(0.0f, p.spread);
    return radius + 4.0f;
}

/// Overload for GlowPipeline (uses the same logic — fields are already
/// converted from GlowParams via GlowPipeline::from()).
[[nodiscard]] inline f32 glow_effect_extent(const GlowPipeline& p) {
    f32 base_radius = std::max(0.0f, p.radius);
    if (!p.layers.empty()) {
        f32 max_layer_r = 0.0f;
        for (const auto& l : p.layers) {
            max_layer_r = std::max(max_layer_r, l.radius);
        }
        base_radius = max_layer_r;
    }
    const f32 radius = base_radius * std::max(0.0f, p.spread);
    return radius + 4.0f;
}

} // namespace chronon3d

// ── Bench/test-exposed glow helpers ───────────────────────────────────────────
//
// `accumulate_glow_pass` / `accumulate_scaled_glow_pass` /
// `build_falloff_lut` / `kFalloffLutSize` live in `chronon3d::renderer`
// (NOT in an anonymous namespace) so Google benchmarks (`tests/bench/micro_benchmarks.cpp`)
// and golden tests can call them directly.  Do not use these from production
// code paths — call `GlowPipeline::render` or `renderer::run_glow_pipeline`
// instead.
namespace chronon3d::renderer {
inline constexpr int kFalloffLutSize = 257;

inline void build_falloff_lut(float falloff,
                              float (&lut)[kFalloffLutSize]) noexcept;

void accumulate_glow_pass(Framebuffer& dst, const Framebuffer& src,
                          const GlowPipeline& p, const float* falloff_lut);

void accumulate_scaled_glow_pass(Framebuffer& dst, const Framebuffer& src,
                                 const GlowPipeline& p, float scale,
                                 const float* falloff_lut);
} // namespace chronon3d::renderer

namespace chronon3d::renderer {

/// Run the glow pipeline. Dispatches on GlowPipeline::mode.
/// Currently supports Layer and Bloom. Shadow is a no-op placeholder.
/// When source_is_alpha_mask is true, the trigger is taken directly
/// from source alpha (used by text glow).
void run_glow_pipeline(Framebuffer& fb, const GlowPipeline& p,
                       const std::optional<raster::BBox>& clip,
                       bool source_is_alpha_mask = false);

} // namespace chronon3d::renderer
