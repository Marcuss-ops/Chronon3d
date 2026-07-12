// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_inspection_collector.cpp — Step 9 §B implementation
//
// Bridges the graph-builder's `TextRunAuditSnapshot` vector to the
// audit + status-mapping pair consumed by `command_inspect_text.cpp` and
// `text_inspection_json.cpp`.
// ═══════════════════════════════════════════════════════════════════════════

#include "text_inspection_collector.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>     // full Framebuffer
#include <chronon3d/text/text_run_shape.hpp>        // full TextRunShape

#include <chronon3d/backends/software/software_renderer.hpp>
// Forward-decl the TextRunAuditSnapshot struct (defined in
// `chronon3d/backends/software/software_renderer.hpp`). Including the
// full header here would cascade graph-builder + software-renderer
// types into every TU that includes the collector. The forward decl
// is sufficient because the function is templated on the snapshot type
// only by reference.

namespace chronon3d::cli {

StatusMapping map_status_for_node(TextVisibilityStatus s,
                                  bool predicted_contains_world) noexcept {
    if (predicted_contains_world == false) {
        // FU04 violation: predicted bbox does not contain world ink.
        // This is the trigger for the §9 FU04 violation response.
        return {"VIOLATION", 2};
    }
    switch (s) {
        case TextVisibilityStatus::PASS:
            return {"PASS", 0};
        case TextVisibilityStatus::FAIL:
            return {"FAIL", 1};
        case TextVisibilityStatus::INDETERMINATE:
            // Default-initialized struct (audit not yet evaluated). Treat
            // as FAIL to fail-closed: callers that see this know the
            // audit did not run.
            return {"FAIL", 1};
    }
    return {"FAIL", 1};
}

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

InspectionRecord audit_for_snapshot(
    const struct TextRunAuditSnapshot& snap,
    const Framebuffer*                 rendered_output,
    int                                frame
) noexcept {
    // The audit takes a `const TextRunShape&`; for null handles
    // (graph builder captured a node whose text pipeline hadn't
    // wired the TextRunShape yet) we pass a default-constructed
    // empty shape so `font_resolved` evaluates to false → FAIL
    // (exit 1).  This preserves the §12 spec's per-node FAIL
    // semantic for unresolvable fonts.
    const TextRunShape* shape_ptr = snap.shape.get();
    TextRunShape        empty_shape{};
    const TextRunShape& shape = shape_ptr ? *shape_ptr : empty_shape;

    // Audit call.  `audit.local_ink_bbox` and `audit.world_ink_bbox`
    // are computed inside `audit_text_visibility()` via the
    // canonical `compute_text_run_visual_bounds()` (the audit
    // header is the single source of truth for these — see
    // `src/text/text_visibility_audit.cpp` §FU02).
    const TextVisibilityAudit audit = audit_text_visibility(
        shape, snap.world_matrix,
        snap.predicted_bbox, snap.clip_rect,
        rendered_output, /*effect_padding=*/0.0f);

    // Real font + glyph_count come from the materialised layout,
    // not from a hard-coded placeholder.  Falls back to
    // "<unknown>" / 0 when the shape has no resolved layout
    // (i.e. the font resolution failed upstream).
    // `audit.glyph_count` is computed the same way inside the
    // audit; we mirror the same source for JSON consistency.
    std::string font_str = "<unknown>";
    if (shape.layout && !shape.layout->font.font_path.empty()) {
        font_str = shape.layout->font.font_path;
    }

    InspectionRecord r;
    r.audit       = audit;
    r.node_name   = snap.name;
    r.font_path   = std::move(font_str);
    r.glyph_count = audit.glyph_count;
    r.frame       = frame;
    return r;
}

#endif // CHRONON3D_BUILD_DIAGNOSTICS

StatusRecordPair map_to_status(
    const struct TextRunAuditSnapshot& snap,
    const Framebuffer*                 rendered_output,
    int                                frame
) noexcept {
    StatusRecordPair pair{};
    pair.record.frame = frame;
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    pair.record = audit_for_snapshot(snap, rendered_output, frame);
    pair.mapping = map_status_for_node(
        pair.record.audit.status,
        pair.record.audit.predicted_contains_world);
#else
    // Non-diagnostic build: no audit ran. Hard-code FAIL to fail-closed.
    pair.mapping = {"FAIL", 1};
    pair.record.node_name   = snap.name;
    pair.record.font_path   = "<unknown>";
    pair.record.glyph_count = 0;
#endif
    return pair;
}

} // namespace chronon3d::cli
