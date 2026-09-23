// ---------------------------------------------------------------------------
// glow_pipeline_converters.cpp — GlowParams to single GlowPipeline converter.
// ---------------------------------------------------------------------------

#include <chronon3d/effects/glow_pipeline.hpp>

namespace chronon3d {

GlowPipeline GlowPipeline::from(const GlowParams& p) {
    GlowPipeline out;
    out.color = p.color;
    out.radius = p.radius;
    out.intensity = p.intensity;
    return out;
}

} // namespace chronon3d
