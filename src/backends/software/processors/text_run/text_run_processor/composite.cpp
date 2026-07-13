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

#include <chronon3d/backends/text/text_rasterizer_ink.hpp>

#include <cstdint>
#include <limits>

namespace chronon3d::renderer::text_run_stages {

namespace {

// Transform a local image-space ink bbox (from s.img) into canvas-space
// using the same model matrix + run-local offset that composite uses.
std::optional<raster::BBox> compute_canvas_ink_bbox(
    BLImage& img,
    const Mat4& model_matrix,
    float offset_x,
    float offset_y)
{
    int ink_left = 0, ink_right = 0, ink_top = 0, ink_bottom = 0;
    float ink_w = 0.0f;
    const float ink_center = compute_text_ink_bbox(
        img, 0, /*box_enabled=*/true, /*font_size=*/0.0f,
        ink_left, ink_right, ink_top, ink_bottom, ink_w);

    // compute_text_ink_bbox returns -1.0f when no visible ink is found.
    if (ink_center < 0.0f || ink_left >= ink_right || ink_top >= ink_bottom) {
        return std::nullopt;
    }

    // Build the same full_model used to composite s.img onto the framebuffer.
    Mat4 full_model = model_matrix;
    full_model = glm::translate(full_model,
        Vec3(offset_x, offset_y, 0.0f));

    // Local bbox is inclusive pixel indices; convert to exclusive float bbox.
    const glm::vec4 corners[4] = {
        {static_cast<float>(ink_left),     static_cast<float>(ink_top),     0.0f, 1.0f},
        {static_cast<float>(ink_right) + 1.0f, static_cast<float>(ink_top),     0.0f, 1.0f},
        {static_cast<float>(ink_left),     static_cast<float>(ink_bottom) + 1.0f, 0.0f, 1.0f},
        {static_cast<float>(ink_right) + 1.0f, static_cast<float>(ink_bottom) + 1.0f, 0.0f, 1.0f},
    };

    float min_x = std::numeric_limits<float>::infinity();
    float min_y = std::numeric_limits<float>::infinity();
    float max_x = -std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();

    for (const auto& c : corners) {
        const glm::vec4 wc = full_model * c;
        min_x = std::min(min_x, wc.x);
        min_y = std::min(min_y, wc.y);
        max_x = std::max(max_x, wc.x);
        max_y = std::max(max_y, wc.y);
    }

    return raster::BBox{
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)),
        static_cast<i32>(std::ceil(max_y))};
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
    // reconciliation.
    std::optional<raster::BBox> actual_ink_bbox = compute_canvas_ink_bbox(
        s.img, params.model_matrix, s.offset_x, s.offset_y);

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
