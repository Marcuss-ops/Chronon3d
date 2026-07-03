// SPDX-License-Identifier: MIT
//
// M1.5#1 — internal split of TextRunNode::execute().
// These free functions are NEVER part of the public API: the public
// surface of TextRunNode (NodeExecResult, predicted_bbox, cache_key,
// `name()`, `kind()`, ...) is unchanged by this refactor.  The split
// exists purely to make `execute()` a short orchestrator and to
// concentrate the per-stage logic in single-responsibility units that
// are easier to audit and to test in isolation.
//
// Conventions:
//   - namespace `chronon3d::graph::text_run`
//   - all helpers are stateless; no hidden caches, no singletons.
//   - any surface that could be `M1.5#N` (N > 1) lives in src/, not in
//     include/chronon3d/.

#pragma once

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run.hpp>
#endif

#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::graph::text_run {

/// M1.5#1 — validate the pre-dispatch invariants of
/// `TextRunNode::execute()`.
///
/// Returns `std::nullopt` when:
///   - `backend != nullptr`  (Fase A4: passed); AND
///   - `backend->capabilities().text_run == true`  (PR2 gate: passed).
///
/// Returns a typed `NodeExecutionError` describing the failure to
/// surface through the `NodeExecResult` channel (NOT a fallback
/// framebuffer — that was the pre-P0-1 false-success anti-pattern
/// that this helper, with the help of the orchestrator, closes).
std::optional<NodeExecutionError> validate_execution(
    const RenderBackend* backend,
    std::string_view node_name
);

#ifdef CHRONON3D_ENABLE_TEXT

/// M1.5#1 (A6 immutability) — produce a per-frame clone of the
/// immutable `TextRunShape` with the animator stack evaluated at
/// `sample_time`.
///
/// Per-frame overhead is `O(N)` with `N = num_glyphs` (typically
/// < 1000).  The underlying layout (`shared_ptr<const TextRunLayout>`)
/// remains shared across frames: only the mutable per-glyph vector is
/// copied.  Two concurrent frames on different worker threads can
/// therefore never race on the same glyph data (Fase A6).
TextRunShape prepare_per_frame_shape(
    const TextRunShape& source,
    chronon3d::SampleTime sample_time
);

#endif // CHRONON3D_ENABLE_TEXT

}  // namespace chronon3d::graph::text_run
