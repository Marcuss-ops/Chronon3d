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
// produce a fully-populated GlowPipeline, and the new `run_glow_pipeline()`
// function dispatches on `mode` and runs the appropriate code path.
//
// `draw_glow` (the 5 expanded-shape node primitive) intentionally stays
// outside this pipeline — it is a fast path with no blur, useful for small
// radii where the blur cost is not amortised.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <optional>
#include <vector>

namespace chronon3d {

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
};

} // namespace chronon3d

namespace chronon3d::renderer {

/// Run the glow pipeline. Dispatches on GlowPipeline::mode.
/// Currently supports Layer and Bloom. Shadow is a no-op placeholder.
void run_glow_pipeline(Framebuffer& fb, const GlowPipeline& p,
                       const std::optional<raster::BBox>& clip);

} // namespace chronon3d::renderer
