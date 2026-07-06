// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode execution-stage helpers.
// See text_run_execution.hpp for the contract.

#include "text_run_execution.hpp"

#include <chronon3d/text/text_run_driver.hpp>   // update_text_run_shape_per_frame
#include <spdlog/spdlog.h>

namespace chronon3d::graph::text_run {

std::optional<NodeExecutionError> validate_execution(
    const RenderBackend* backend,
    std::string_view node_name
) {
    if (!backend) {
        // P0-3: surface via NodeExecutionError(InvalidInput), not a
        // cleared fb.  The diagnostic signature is preserved verbatim
        // for the regression lock in test_text_run_node_execute_error.cpp
        // ("backend is null" + node name).
        spdlog::error(
            "[text-run] node='{}' cannot render: backend is null; "
            "returning NodeExecutionError(InvalidInput) via execute().",
            node_name);
        return NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            std::string(node_name),
            "backend is null"
        };
    }
    if (!backend->capabilities().text_run) {
        // PR2 — capability gate short-circuits before any
        // backend->draw_text_run() call so the slow path never fires.
        spdlog::error(
            "[text-run] node='{}' backend does not support "
            "draw_text_run; returning "
            "NodeExecutionError(UnsupportedCapability) via execute().",
            node_name);
        return NodeExecutionError{
            RenderBackendErrorCode::UnsupportedCapability,
            std::string(node_name),
            "backend does not support draw_text_run"
        };
    }
    return std::nullopt;
}

#ifdef CHRONON3D_ENABLE_TEXT

TextRunShape prepare_per_frame_shape(
    const TextRunShape& source,
    chronon3d::SampleTime sample_time
) {
    // A6 immutability: shallow-copy the underlying layout (shared_ptr)
    // and deep-copy the mutable per-glyph vector.  Evaluating the
    // animator stack into the clone keeps `m_shape` (the compiled
    // immutable node input) read-only across calls.
    TextRunShape local = source;
    chronon3d::update_text_run_shape_per_frame(local, sample_time);

    // TICKET-104 DIAGNOSTIC — one-shot runtime log of the per-frame
    // shape contents.  *** REMOVE once root cause is confirmed. ***
    // This is the per-frame shape clone site for TextRunNode.  Combined
    // with the build_world_matrix and draw_text_run diagnostics, this
    // isolates whether the rasterization gap is in the per-frame shape
    // (animator transforming glyphs off-canvas), the BL bridge / coords
    // interpretation, or the framebuffer bounding-box check.
    static bool kTicket104PpfsOneShot = true;
    if (kTicket104PpfsOneShot) {
        const std::size_t n_glyphs = local.glyphs.size();
        const bool has_first = !local.glyphs.empty();
        const f32 first_lp_x  = has_first ? local.glyphs[0].layout_position.x : -1.0f;
        const f32 first_lp_y  = has_first ? local.glyphs[0].layout_position.y : -1.0f;
        const f32 first_pos_x = has_first ? local.glyphs[0].position.x       : -1.0f;
        const f32 first_pos_y = has_first ? local.glyphs[0].position.y       : -1.0f;
        const f32 first_opac  = has_first ? local.glyphs[0].opacity         : -1.0f;
        const bool has_layout  = static_cast<bool>(local.layout);
        const f32 bounds_w = has_layout ? local.layout->bounds.x : -1.0f;
        const f32 bounds_h = has_layout ? local.layout->bounds.y : -1.0f;
        spdlog::info(
            "[TICKET104:prepare_per_frame_shape] n_glyphs={} "
            "first.lp=({:.2f},{:.2f}) first.pos=({:.2f},{:.2f}) "
            "first.opacity={:.2f} "
            "has_layout={} layout.bounds=({:.2f},{:.2f})",
            n_glyphs,
            first_lp_x, first_lp_y,
            first_pos_x, first_pos_y,
            first_opac,
            has_layout ? 1 : 0,
            bounds_w, bounds_h
        );
        kTicket104PpfsOneShot = false;
    }

    return local;
}

#endif // CHRONON3D_ENABLE_TEXT

}  // namespace chronon3d::graph::text_run
