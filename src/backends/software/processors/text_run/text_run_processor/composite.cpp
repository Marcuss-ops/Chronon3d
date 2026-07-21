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
#include <limits>

namespace chronon3d::renderer::text_run_stages {

namespace {

// Transform the local geometric ink bbox (prepared in prepare_text_run via
// compute_text_run_visual_bounds) into canvas-space using the same model
// matrix + run-local offset that composite uses. This replaces the prior
// alpha-scan of the scratch image so that placement never depends on a
// pixel scan; the scratch-image alpha scan is retained only as a debug /    // audit helper (see alpha_bbox_scan).
std::optional<raster::BBox> compute_canvas_ink_bbox(
    const Mat4& model_matrix,
    float offset_x,
    float offset_y,
    float min_x,
    float min_y,
    float max_x,
    float max_y)
{
    // Degenerate local bounds mean no visible ink.
    if (min_x >= max_x || min_y >= max_y) {
        return std::nullopt;
    }

    // Build the same full_model used to composite s.img onto the framebuffer.
    Mat4 full_model = model_matrix;
    full_model = glm::translate(full_model,
        Vec3(offset_x, offset_y, 0.0f));

    const glm::vec4 corners[4] = {
        {min_x, min_y, 0.0f, 1.0f},
        {max_x, min_y, 0.0f, 1.0f},
        {min_x, max_y, 0.0f, 1.0f},
        {max_x, max_y, 0.0f, 1.0f},
    };

    float canvas_min_x = std::numeric_limits<float>::infinity();
    float canvas_min_y = std::numeric_limits<float>::infinity();
    float canvas_max_x = -std::numeric_limits<float>::infinity();
    float canvas_max_y = -std::numeric_limits<float>::infinity();

    for (const auto& c : corners) {
        const glm::vec4 wc = full_model * c;
        canvas_min_x = std::min(canvas_min_x, wc.x);
        canvas_min_y = std::min(canvas_min_y, wc.y);
        canvas_max_x = std::max(canvas_max_x, wc.x);
        canvas_max_y = std::max(canvas_max_y, wc.y);
    }

    return raster::BBox{
        static_cast<i32>(std::floor(canvas_min_x)),
        static_cast<i32>(std::floor(canvas_min_y)),
        static_cast<i32>(std::ceil(canvas_max_x)),
        static_cast<i32>(std::ceil(canvas_max_y))};
}

} // namespace

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

    // Compute the intrinsic ink bbox BEFORE releasing s.img — node_runner
    // uses this to avoid a full-framebuffer alpha scan during post-render
    // reconciliation. Use the geometric local-space bounds computed in
    // prepare_text_run rather than scanning rendered pixels, so placement
    // is deterministic and independent of rasterization.
    std::optional<raster::BBox> actual_ink_bbox = compute_canvas_ink_bbox(
        params.model_matrix, s.offset_x, s.offset_y,
        s.min_x, s.min_y, s.max_x, s.max_y);

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
    return graph::RenderOpResult(graph::RenderOpOutcome{s.glyphs_drawn, actual_ink_bbox});
}

} // namespace chronon3d::renderer::text_run_stages
