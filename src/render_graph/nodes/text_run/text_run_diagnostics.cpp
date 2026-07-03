// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode diagnostic helpers.
// Diagnostic strings match the regression contract in
// tests/render_graph/nodes/test_text_run_node_execute_error.cpp:
//   §1 — "draw_text_run failed" + node name + code name
//   §2 — "does not support"      + node name
//   §3 — "backend is null"       + node name
// Diagnostic-mode string match in
// test_protected_core_contracts.cpp::test_text_run_node_execute_error
// relies on the shape_hash/glyphs/drew/opacity/tx/ty fields being
// emitted in order.

#include "text_run_diagnostics.hpp"

// NOTE: the .hpp only forward-uses `chronon3d::TextRunShape` (it's a
// function-parameter reference type — incomplete-type semantics are
// permitted by C++ here).  The .cpp needs the FULL type because it
// accesses `eval_shape.glyphs.size()`.  Keeping the heavy include out
// of the .hpp keeps the diagnostics header light.
#include <chronon3d/text/text_run.hpp>

#include <spdlog/spdlog.h>

namespace chronon3d::graph::text_run {

void report_failure(
    std::string_view node_name,
    RenderBackendErrorCode code,
    std::string_view code_name,
    std::string_view message
) {
    spdlog::error(
        "[text-run] node='{}' draw_text_run failed: [{}] {}",
        node_name,
        code_name,
        message);
}

void report_diagnostic(
    std::string_view node_name,
    const chronon3d::TextRunShape& eval_shape,
    std::size_t items_drawn,
    float opacity,
    const glm::mat4& world_matrix,
    std::optional<std::uint64_t> shape_hash
) {
    if (shape_hash.has_value()) {
        // CHRONON3D_ENABLE_TEXT path: emit shape_hash so the
        // diagnostic matches the actual cache key the executor used
        // (PR 10 — AnimatedTextDocument fold at the integral frame).
        spdlog::debug(
            "[text-run] node='{}' shape_hash=0x{:016x} glyphs={} "
            "drew={} opacity={:.3f} tx={:.1f} ty={:.1f}",
            node_name,
            *shape_hash,
            eval_shape.glyphs.size(),
            items_drawn,
            opacity,
            world_matrix[3][0],
            world_matrix[3][1]
        );
    } else {
        // No-text fallback: identical to the historical branch in the
        // pre-refactor TextRunNode.cpp.
        spdlog::debug(
            "[text-run] node='{}' glyphs={} drew={} "
            "opacity={:.3f} tx={:.1f} ty={:.1f}",
            node_name,
            eval_shape.glyphs.size(),
            items_drawn,
            opacity,
            world_matrix[3][0],
            world_matrix[3][1]
        );
    }
}

}  // namespace chronon3d::graph::text_run
