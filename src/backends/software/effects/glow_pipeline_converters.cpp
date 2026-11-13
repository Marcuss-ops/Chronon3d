// ---------------------------------------------------------------------------
// glow_pipeline_converters.cpp — effect params to GlowPipeline converters.
// ---------------------------------------------------------------------------

#include <chronon3d/effects/glow_pipeline.hpp>

namespace chronon3d {

GlowPipeline GlowPipeline::from(const GlowParams& p) {
    GlowPipeline out;
    out.color = p.color;
    out.color_b = p.color;
    out.radius = p.radius;
    out.intensity = p.intensity;
    out.threshold = 0.0f;
    out.falloff = 1.0f;
    return out;
}

GlowPipeline GlowPipeline::from(const GlowAdvancedParams& p) {
    GlowPipeline out;
    out.color = {p.color_r, p.color_g, p.color_b, p.color_a};
    out.color_b = {p.color_b_r, p.color_b_g, p.color_b_b, p.color_b_a};
    out.threshold = p.threshold;
    out.radius = p.radius;
    out.intensity = p.intensity;
    out.falloff = p.falloff;
    return out;
}

} // namespace chronon3d
