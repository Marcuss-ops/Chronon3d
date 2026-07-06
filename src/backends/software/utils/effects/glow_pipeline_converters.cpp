// ---------------------------------------------------------------------------
// glow_pipeline_converters.cpp — GlowPipeline::from() converters
//
// FASE 7 Step 2 — extracted from glow_pipeline.cpp.
// Converts GlowParams / BloomParams / DropShadowParams into GlowPipeline.
// ---------------------------------------------------------------------------

#include <chronon3d/effects/glow_pipeline.hpp>

namespace chronon3d {

GlowPipeline GlowPipeline::from(const GlowParams& p) {
    GlowPipeline out;
    out.mode = GlowPipeline::Mode::Layer;
    out.color = p.color;
    out.radius = p.radius;
    out.intensity = p.intensity;
    out.threshold = p.threshold;
    out.spread = p.spread;
    out.softness = p.softness;
    out.falloff = p.falloff;
    out.core_strength = p.core_strength;
    out.aura_strength = p.aura_strength;
    out.bloom_strength = p.bloom_strength;
    out.outer_downscale = p.outer_downscale;
    out.preserve_source = p.preserve_source;
    out.additive = p.additive;
    out.blend = p.blend;
    out.layers = p.layers;
    out.quality = p.quality;
    return out;
}

GlowPipeline GlowPipeline::from(const BloomParams& p) {
    GlowPipeline out;
    out.mode = GlowPipeline::Mode::Bloom;
    out.radius = p.radius;
    out.intensity = p.intensity;
    out.threshold = p.threshold;
    out.color = Color{1, 1, 1, 1};   // bloom uses the source pixel colors
    out.preserve_source = true;       // additive on top of source
    return out;
}

GlowPipeline GlowPipeline::from(const DropShadowParams& p) {
    GlowPipeline out;
    out.mode = GlowPipeline::Mode::Shadow;
    out.color = p.color;
    out.radius = p.radius;
    out.intensity = 1.0f;
    // Shadow offset (Vec2) is not yet threaded through GlowPipeline.
    // apply_shadow_effect() remains in effect_shadow_impl.cpp until a
    // future refactor adds an offset field to GlowPipeline.
    return out;
}

} // namespace chronon3d
