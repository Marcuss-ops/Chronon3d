// ---------------------------------------------------------------------------
// effect_bloom_impl.cpp — Thin shim over the unified glow pipeline.
// The actual implementation lives in glow_pipeline.cpp (run_bloom_mode).
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include <chronon3d/effects/glow_pipeline.hpp>

namespace chronon3d::renderer {

void apply_bloom_effect(
    Framebuffer& fb,
    const BloomParams& p,
    const std::optional<raster::BBox>& clip,
    bool /*diagnostics_enabled*/
) {
    run_glow_pipeline(fb, GlowPipeline::from(p), clip);
}

} // namespace renderer
} // namespace chronon3d
