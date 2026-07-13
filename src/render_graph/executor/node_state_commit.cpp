#include "node_state_commit.hpp"

namespace chronon3d::graph {

// commit_node_state — implementation
//
// Writes all 5 per-node state slots in a single block.  Byte-equivalent to
// the inline 5-write pattern previously duplicated at:
//
//   Site 1 (cache-hit fast-path early return)
//     `node_runner.cpp::execute_single_node` lines 221-225 (was):
//       state.temp[id]                 = cache_eval.result;
//       state.resolved_key_digest[id]  = cache_eval.key.digest();
//       state.resolved_frame_dependent[id] = 0;     // hardcoded 0
//       state.resolved_cache_hit[id]       = 1;     // hardcoded 1
//       state.resolved_bboxes[id]       = predicted_bbox;
//
//   Site 3 (main tail after `emit_node_records`)
//     `node_runner.cpp::execute_single_node` lines 403-407:
//       state.temp[id] = cache_eval.result;
//       state.resolved_key_digest[id]  = cache_eval.key.digest();
//       state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;
//       state.resolved_cache_hit[id]       = (cache_eval.cache_status == "hit") ? 1 : 0;
//       state.resolved_bboxes[id]       = predicted_bbox;
//
// ── Byte-equivalence proof for Site 1 ──────────────────────────────────
// Site 1 (the cache_hit_fast_path early return) executes only if the guard
// at `node_runner.cpp::execute_single_node` line ~213 is true:
//
//     const bool cache_hit_fast_path =
//         cache_eval.result &&
//         cache_eval.cache_status == "hit" &&
//         !cache_eval.node_frame_dependent &&
//         !ctx.policy.tile_execution_enabled;
//
// Given those invariants:
//   - `cache_eval.cache_status == "hit"` ⇒ the ternary in the unified
//     helper writes `1` to `state.resolved_cache_hit[id]`, MATCHING the
//     hardcoded `1` in Site 1.
//   - `!cache_eval.node_frame_dependent` ⇒ the ternary in the unified
//     helper writes `0` to `state.resolved_frame_dependent[id]`,
//     MATCHING the hardcoded `0` in Site 1.
//
// Therefore Site 1's hardcoded `0`/`1` are sound under the helper derivation
// (the unified helper doesn't introduce any new branch — it uses the SAME
// `CacheEvalResult` fields the cache_hit_fast_path already reads).
//
// ── Byte-equivalence proof for Site 3 ──────────────────────────────────
// Site 3's existing pattern is structurally identical to the unified
// helper's body — the helper substitutes the `cache_eval`-based expressions
// verbatim (no additional branches, no additional field reads):
//
//   state.temp[id]                     = cache_eval.result;             // ✓
//   state.resolved_key_digest[id]      = cache_eval.key.digest();       // ✓
//   state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;  // ✓
//   state.resolved_cache_hit[id]       = (cache_eval.cache_status == "hit") ? 1 : 0; // ✓
//   state.resolved_bboxes[id]          = predicted_bbox;                // ✓
//
// ✓ on each line — Site 3 is a 1-to-1 substitution into the helper body.
//
// ── Excluded sites (deliberately left inline per Cat-3 anti-dup) ───────
// Site 2 (tile-skip branch at `node_runner.cpp:execute_single_node` lines
// ~270-279) writes `state.shared_transparent` + all-zeros — semantically
// different from the cache_eval-based pattern (it's the "fully transparent
// skip" branch, not a "cache_eval result propagation" branch). Extracting
// it would require a parallel `commit_skipped_state(state, id,
// predicted_bbox)` helper, but that's a one-off branch (used exactly once)
// so extracting it would be Cat-3 anti-duplication. Kept inline.
void commit_node_state(
    ExecutionState& state,
    GraphNodeId id,
    const CacheEvalResult& cache_eval,
    std::optional<raster::BBox> predicted_bbox
) {
    state.temp[id]                     = cache_eval.result;
    state.resolved_key_digest[id]      = cache_eval.key.digest();
    state.resolved_frame_dependent[id] = cache_eval.node_frame_dependent ? 1 : 0;
    state.resolved_cache_hit[id]       = (cache_eval.cache_status == "hit") ? 1 : 0;
    state.resolved_bboxes[id]          = std::move(predicted_bbox);
}

} // namespace chronon3d::graph
