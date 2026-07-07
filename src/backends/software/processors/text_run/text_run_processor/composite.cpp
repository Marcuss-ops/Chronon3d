// ═══════════════════════════════════════════════════════════════════════════
// src/backends/software/processors/text_run/text_run_processor/composite.cpp
// ═══════════════════════════════════════════════════════════════════════════
//
// M1.5#6 — Stage 4 of the draw_text_run pipeline: composite_text_run.
//
// Owns the final BLImage → params.fb composite call.  After this stage
// the scratch pool owns `s.img` again (explicit release) so subsequent
// calls hit the pool's surface cache (improves acquire hit-rate).
//
// The `text_glyphs_rasterized` counter is updated here too — the
// pre-M1.5#6 monolith did it post-composite; preserve that order so
// callers that listen on the counter observe the same "n_glyph drawn"
// value at the same point in the call.
//
// AGENTS.md v0.1 freeze Cat-3 internal — strictly internal.

#include "text_run_stages.hpp"
#include "../../../utils/blend2d_bridge.hpp"

#include <cstdint>

namespace chronon3d::renderer::text_run_stages {

[[nodiscard]] graph::RenderOpResult composite_text_run(
    const SoftwareProcessorContext& rctx,
    TextRunDrawParams&              params,
    TextRunStageState&              s
) {
    // TICKET-TEXT-CLEANUP-4: silent_success_empty guard removed.
    if (s.glyphs_drawn == 0) {
        // Already released in raster tail when zero-glyph.  Just return.
        return graph::RenderOpResult(graph::RenderOpOutcome{s.glyphs_drawn});
    }

    // ── Stage 8 — Composite MAIN onto framebuffer (full model_matrix) ────
    //
    // Compose the user's model with the run-local translate so the image
    // content fills the framebuffer correctly under rotation / scale /
    // shear.  composite_bl_image_transformed has a fast path for
    // simple-translation matrices so the common case remains cheap.
    Mat4 full_model = params.model_matrix;
    full_model = glm::translate(full_model,
        Vec3(s.offset_x, s.offset_y, 0.0f));

    chronon3d::blend2d_bridge::composite_bl_image_transformed(
        params.fb, s.img, full_model,
        params.opacity, BlendMode::Normal);

    // perf(text): explicit release back to the per-call pool on the
    // SUCCESS path, not just on early-out.  The image data has been
    // transferred to params.fb via the composite above.
    release_surface(s, std::move(s.img));

    if (rctx.counters) {
        rctx.counters->text_glyphs_rasterized.fetch_add(
            static_cast<std::uint64_t>(s.glyphs_drawn),
            std::memory_order_relaxed
        );
    }

    // PR2: no diagnostic_mode — caller (TextRunNode / multi_source_node)
    // owns diagnostics via ctx.policy.diagnostics_enabled.
    return graph::RenderOpResult(graph::RenderOpOutcome{s.glyphs_drawn});
}

} // namespace chronon3d::renderer::text_run_stages
