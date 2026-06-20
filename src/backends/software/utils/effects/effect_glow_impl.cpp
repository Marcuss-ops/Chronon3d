// ---------------------------------------------------------------------------
// effect_glow_impl.cpp — Thin shim over the unified glow pipeline.
// The actual implementation lives in glow_pipeline.cpp (run_layer_mode).
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include <chronon3d/core/config.hpp>          // TICKET-007
#include <chronon3d/effects/glow_pipeline.hpp>

namespace chronon3d::renderer {

void apply_glow_effect(Framebuffer& fb, const GlowParams& p,
                       const std::optional<raster::BBox>& clip,
                       const chronon3d::DebugConfig* debug_cfg /* TICKET-007 */) {
    run_glow_pipeline(fb, GlowPipeline::from(p), clip,
                      /*source_is_alpha_mask=*/false, debug_cfg);
}

} // namespace chronon3d::renderer
