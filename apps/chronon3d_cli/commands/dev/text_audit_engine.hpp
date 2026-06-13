#pragma once

// ── Text Audit Engine ─────────────────────────────────────────────────────
//
// Evaluates a composition at specified frames, extracts text nodes,
// computes layout + rasterization, and runs geometric/typographic checks.
//
// Used by the `text-audit` CLI command and by CI golden tests.
// ========================================================================

#include "text_audit_types.hpp"
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <string>
#include <vector>

namespace chronon3d::cli {

/// Audit a single text shape at a specific frame.
/// Rasterizes the text, runs all geometric and typographic checks,
/// and returns a per-frame result.
TextAuditFrameResult audit_single_text(
    const TextShape& text,
    int canvas_width,
    int canvas_height,
    int frame,
    const TextAuditPolicy& policy,
    const TextAuditFrameResult* prev_frame = nullptr
);

/// Audit a composition across multiple frames.
/// Compiles the scene at each frame, walks nodes to find text layers,
/// and runs the full audit pipeline.
TextAuditResult audit_composition(
    const CompositionRegistry& registry,
    const std::string& comp_id,
    const std::vector<int>& frames,
    const TextAuditPolicy& policy
);

/// Serialize an audit result to a JSON string.
std::string audit_result_to_json(const TextAuditResult& result);

/// Determine the exit code from the accumulated audit result.
int audit_exit_code(const TextAuditResult& result);

} // namespace chronon3d::cli
