#pragma once

// ===========================================================================
// node_state_commit.hpp — canonical 5-slot state commit helper
//
// P1 step 3 refactor — extracted from `node_runner.cpp::execute_single_node`
// (was: 2 duplicate 5-write blocks at the cache-hit fast-path and the main
// tail).  The unified helper writes all 5 per-node state slots produced by
// `CacheEvalResult` in a single block.  Both Sites had IDENTICAL byte-level
// semantics (the fast-path invariants force the derived values to match the
// hardcoded `0`/`1` they replace); see byte-equivalence proof in
// `node_state_commit.cpp` and `node_runner.cpp::execute_single_node`.
//
// SCOPE (deliberately narrow, per user spec):
//   - Sites 1+3 only (cache_eval-based 5-write pattern).
//   - Site 2 (tile-skip path at `node_runner.cpp:275-279`) stays INLINE:
//     it writes `state.shared_transparent` + all-zeros, a semantically
//     different "skipped" state that bypasses `CacheEvalResult`.  Extracting
//     it would introduce a single-use `commit_skipped_state` function —
//     Cat-3 anti-dup violation for a one-off branch (clarity > symmetry).
//   - Telemetry emission (`emit_node_records`) is NOT touched: the
//     orchestrator (`node_runner.cpp`) still calls it between the
//     `cache_eval.result` computation and the new `commit_node_state`
//     call.  Per user spec, telemetry is a separate concern.
//
// CAT-3 minimal-surface:
//   - Zero new singletons, registry, cache, resolver, service locator.
//   - Zero new SDK API (function is `chronon3d::graph`-scoped, internal to
//     the executor module).
//   - No default args (per AGENTS.md `### C++ default-arg uniqueness per TU`).
//   - No `<msdfgen>`, `<libtess2>`, `<unicode[/...]>` includes (Cat-5 fence).
//
// NAMESPACE: `chronon3d::graph` (matches the surrounding executor code:
// `node_runner.cpp`, `node_skip_policy.hpp`, `level_timings.hpp`, etc.).
// ===========================================================================

#include "execution_state.hpp"  // ExecutionState + CacheEvalResult + the 5 slot types

#include <chronon3d/internal/render_graph/render_graph.hpp>  // GraphNodeId
#include <chronon3d/math/raster_utils.hpp>                   // raster::BBox

#include <optional>

namespace chronon3d::graph {

// commit_node_state — write the 5 per-node state slots produced by
// `execute_single_node` at the end of its main paths.
//
// Equivalent (byte-level) to the inline pattern previously duplicated in
// `node_runner.cpp::execute_single_node` at:
//   - the cache-hit fast-path early return (was: lines 221-225)
//   - the main tail after `emit_node_records`              (was: lines 403-407)
//
// `state`        — per-frame mutable executor state (5 vectors indexed by id).
// `id`           — node index into the 5 vectors.
// `cache_eval`   — result of `evaluate_cache(...)`; the helper reads 4 of its
//                  6 fields (`result`, `key.digest()`, `cache_status`,
//                  `node_frame_dependent`).
// `predicted_bbox`  — pre-computed predicted bbox (passed by value: ~20 byte
//                  small-trivially-copyable optional + 4-int struct).
//
// EXCLUDED from this helper:
//   - `emit_node_records(...)` — telemetry stays in the orchestrator
//     (`node_runner.cpp::execute_single_node`), per user spec.
//   - The 5-write block at the tile-skip branch (Site 2 at
//     `node_runner.cpp:275-279`) — different semantics
//     (`state.shared_transparent` + all-zeros), stays inline per
//     Cat-3 anti-dup (one-off branch > extract-function symmetry).
void commit_node_state(
    ExecutionState& state,
    GraphNodeId id,
    const CacheEvalResult& cache_eval,
    std::optional<raster::BBox> predicted_bbox
);

} // namespace chronon3d::graph
