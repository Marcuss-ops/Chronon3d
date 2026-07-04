// ═══════════════════════════════════════════════════════════════════════════
// src/backends/software/processors/text_run/text_run_processor/effects.cpp
// ═══════════════════════════════════════════════════════════════════════════
//
// M1.5#6 — Stage 3 of the draw_text_run pipeline: apply_text_run_effects.
//
// Owns ONLY the per-shape material apply (gradient / bevel / etc.) on the
// post-raster BLImage.  After this stage returns, the image is ready for
// the final composite onto the framebuffer (composite.cpp / Stage 4).
//
// FASE 3b NOTE: TextMaterial currently operates in display-referred (gamma)
// space.  Gradient/bevel computations should ideally happen in linear space
// with sRGB conversion only at the final framebuffer output, but that
// requires a pipeline-wide color management pass (NOT in scope of M1.5#6).
// Material effects continue to be applied to the (possibly supersampled
// then downsampled) PRGB32 image with correct premultiplied alpha.
//
// AGENTS.md v0.1 freeze Cat-3 internal — strictly internal.

#include "text_run_stages.hpp"
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>  // apply_text_material

namespace chronon3d::renderer::text_run_stages {

[[nodiscard]] graph::RenderOpResult apply_text_run_effects(
    const SoftwareProcessorContext& /* rctx */,
    const TextRunDrawParams&        params,
    TextRunStageState&              s
) {
    // Guard BEFORE apply_text_material: if raster returned Outcome{0}
    // (zero-glyph), it has already released s.img back to the pool; we
    // must NOT pass a released BLImage into apply_text_material — running
    // the material pass on stale pool state would be a use-after-release.
    if (s.silent_success_empty || s.glyphs_drawn == 0) {
        return graph::RenderOpResult(graph::RenderOpOutcome{0});
    }

    if (params.shape.material.enabled) {
        apply_text_material(s.img, params.shape.material);
    }

    // Material apply is a per-pixel mutation; does NOT alter glyphs_drawn
    // count.  Return the raster stage's outcome forward.
    return graph::RenderOpResult(graph::RenderOpOutcome{s.glyphs_drawn});
}

} // namespace chronon3d::renderer::text_run_stages
