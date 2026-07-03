// SPDX-License-Identifier: MIT
//
// M1.5#1 — internal diagnostic helpers for TextRunNode.
// Concentrates every spdlog emit triggered by the node's render
// path, so the orchestrator stays free of string literals and the
// diagnostic signatures are testable from a single unit.  The exact
// diagnostic strings (node-name + error code names + "draw_text_run
// failed" / "does not support" / "backend is null") are preserved
// verbatim so the existing regression lock in
// tests/render_graph/nodes/test_text_run_node_execute_error.cpp
// continues to pass without edits.
// Internal — NOT in include/chronon3d/.

#pragma once

#include <chronon3d/render_graph/render_backend.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace chronon3d::graph::text_run {

/// spdlog::error("[text-run] node='...' draw_text_run failed: [code] msg").
/// Called when `backend->draw_text_run()` returns an error result.
/// `code_name` is the canonical string from
/// `render_backend_error_code_name(code)` (centralised in
/// render_backend.hpp).
void report_failure(
    std::string_view node_name,
    RenderBackendErrorCode code,
    std::string_view code_name,
    std::string_view message
);

/// spdlog::debug("[text-run] node='...' shape_hash=0x... glyphs=...
/// drew=... opacity=... tx=.. ty=..").  Called when
/// `ctx.policy.diagnostics_enabled == true` after a successful draw.
///
/// `shape_hash_fn` is provided as `std::optional`: the text-enabled
/// variant (`CHRONON3D_ENABLE_TEXT`) supplies
/// `chronon3d::hash_text_run_shape(eval_shape, frame)`; the
/// non-text fallback supplies `std::nullopt`.  The diagnostic string
/// switches format branch based on whether the hash is present —
/// machine-verifiable by regression contract.
void report_diagnostic(
    std::string_view node_name,
    const chronon3d::TextRunShape& eval_shape,
    std::size_t items_drawn,
    float opacity,
    const glm::mat4& world_matrix,
    std::optional<std::uint64_t> shape_hash
);

}  // namespace chronon3d::graph::text_run
