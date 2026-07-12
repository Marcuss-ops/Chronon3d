// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_inspection_collector.hpp — Step 9 §B extracted collector
//
// Sits between `command_inspect_text.cpp` (arg parsing + render + exit
// dispatch) and `text_inspection_json.{hpp,cpp}` (JSON serialisation).
// The collector is the canonical bridge:
//   - consumes the graph builder's `TextRunAuditSnapshot` vector
//     (`renderer.text_audit_snapshots()`)
//   - audits each via `audit_text_visibility()`
//   - maps the audit's `(status, predicted_contains_world)` pair to the
//     JSON status string + exit code per the §12 spec
//   - returns the audit + status mapping in a single collector record
//
// Extracted from `command_inspect_text.cpp` so the command itself is a
// thin orchestrator (~150-200 LoC per user spec).
//
// All entry-points are gated by `CHRONON3D_BUILD_DIAGNOSTICS` because
// the underlying `TextVisibilityStatus` enum and `TextVisibilityAudit`
// struct are gated by that macro upstream. When diagnostics=OFF, the
// collector emits the same `diagnostics_disabled` error JSON + exit 1
// that the command previously inlined.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <chronon3d/text/text_visibility_audit.hpp>  // TextVisibilityAudit

// Forward decls (avoid pulling in heavy graph-builder + software-renderer
// headers in this lightweight collector header).
namespace chronon3d {
    class Framebuffer;
}

namespace chronon3d::cli {

/// Per-snapshot collector result. Returned by `collect_audit_for_snapshot()`
/// for each TextRun node the graph builder captured. The command (or
/// JSON layer) reads `audit` for measurement data + `status` for the
/// exit-code mapping.
struct InspectionRecord {
    TextVisibilityAudit audit;     // audit-computed bboxes + invariants
    std::string         node_name; // the graph-builder snapshot's name
    std::string         font_path; // "<unknown>" when layout absent
    std::size_t         glyph_count{0};
    int                 frame{0};
};

/// `(json_status_string, exit_code)` mapping per the §12 spec:
///   PASS                 → "PASS"     → 0
///   FAIL                 → "FAIL"     → 1
///   VIOLATION (predicted_contains_world=false) → "VIOLATION" → 2
///   INDETERMINATE        → "FAIL"     → 1   (fail-closed: audit not run)
struct StatusMapping {
    const char* json_status;
    int         exit_code;
};

/// `map_status_for_node()` — canonical `(TextVisibilityStatus, predicted_contains_world)`
/// → `(json_status_string, exit_code)` mapping. Public so the JSON layer
/// (`text_inspection_json.cpp`) can format the per-snapshot JSON entry
/// without duplicating the mapping logic.
StatusMapping map_status_for_node(TextVisibilityStatus s,
                                  bool predicted_contains_world) noexcept;

/// `audit_for_snapshot()` — audit a single graph-builder snapshot.
///
/// The snapshot carries the producer-side view: real `TextRunShape` +
/// real per-node world matrix + producer-supplied predicted/clip bboxes.
/// For null handles (graph builder captured a node whose text pipeline
/// hadn't wired the TextRunShape yet) we pass a default-constructed
/// empty shape so `font_resolved` evaluates to false → FAIL (exit 1).
/// This preserves the §12 spec's per-node FAIL semantic for unresolvable
/// fonts.
///
/// The `frame` is recorded on the resulting record so the JSON layer can
/// emit the `"frame": <int>` field without recomputing it.
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
InspectionRecord audit_for_snapshot(
    const struct TextRunAuditSnapshot& snap,
    const Framebuffer*                 rendered_output,
    int                                frame
) noexcept;
#endif

/// `map_to_status()` — convenience wrapper: audit a snapshot, return the
/// (StatusMapping, InspectionRecord) pair. Saves the JSON layer from
/// re-implementing the audit→status mapping. When diagnostics=OFF,
/// returns a hard-coded {"FAIL", 1} mapping.
struct StatusRecordPair {
    StatusMapping    mapping;
    InspectionRecord record;  // audit is zero-initialised when diagnostics=OFF
};

StatusRecordPair map_to_status(
    const struct TextRunAuditSnapshot& snap,
    const Framebuffer*                 rendered_output,
    int                                frame
) noexcept;

} // namespace chronon3d::cli
