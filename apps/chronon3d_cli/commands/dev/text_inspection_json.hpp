// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_inspection_json.hpp — Step 9 §B JSON serialisation layer
//
// SINGLE canonical JSON serialisation entry-point for both:
//   - the per-snapshot inspect-text JSON output
//   - the audit-engine `TextAuditResult` JSON dump (formerly `text_audit_json.cpp`)
//
// Extracted/merged from:
//   - `text_audit_json.cpp`    (absorbed `audit_result_to_json` here)
//   - `command_inspect_text.cpp` (anon-namespace `json_escape` + `json_bbox` for
//                                 the per-node output — different signatures,
//                                 kept TU-local in the command)
//
// Anti-duplication strategy (per user spec §B "nessun helper JSON duplicato"):
//   1. `json_escape(const std::string&)` — canonical home is
//      `text_audit_helpers.{hpp,cpp}`. This header re-exports the symbol
//      via `using` so callers can write `text_inspection_json::json_escape(...)`
//      OR continue to include `text_audit_helpers.hpp` directly. Zero
//      duplicate definitions: the function is implemented exactly once
//      in `text_audit_helpers.cpp` and re-exported here.
//   2. `json_bbox(const TextAuditBBox&)` — same pattern: canonical home is
//      `text_audit_helpers.{hpp,cpp}`, re-exported here.
//   3. The command's `json_bbox(const Rect&)` STAYS TU-local in
//      `command_inspect_text.cpp` because the signature differs (Rect vs
//      TextAuditBBox) and the JSON output format differs (object vs
//      array). The two functions are not interchangeable; consolidating
//      them would be a behavioural change, not a refactor.
//
// `audit_result_to_json(const TextAuditResult&)` — the audit-engine
// JSON dump. Absorbed from `text_audit_json.cpp` (which has been
// DELETED in Step 9). The single canonical home is this header; the
// call site in `command_text_audit.cpp` updates its include to point
// here (no namespace change, so the call signature is preserved).
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "text_audit_types.hpp"     // TextAuditResult
#include "text_audit_helpers.hpp"   // canonical json_escape + json_bbox(BBox)

// ── Re-export the audit-engine JSON helpers (anti-duplication) ────────
//
// `using` declarations: zero cost, zero symbol duplication. The single
// implementation lives in `text_audit_helpers.cpp`. Callers that include
// this header gain access via the `text_inspection_json::` namespace
// prefix without having to add a second `#include`.

#include <string>

// `audit_result_to_json()` is declared in the `chronon3d::cli::` namespace
// (NOT in the `text_inspection_json` sub-namespace) so the existing call
// sites in `command_text_audit.cpp` (which include `text_audit_engine.hpp`
// where the function is declared in the parent `chronon3d::cli::` namespace)
// keep working without any include change. The function body lives in
// `text_inspection_json.cpp` (the SINGLE canonical home after the
// `text_audit_json.cpp` absorption). The function is behaviour-preserving
// from the now-deleted `text_audit_json.cpp` (zero change in the JSON
// schema; the function body was moved verbatim).

namespace chronon3d::cli {

// Re-exported audit-engine JSON helpers (anti-duplication strategy per
// user spec §B "nessun helper JSON duplicato"). The single implementation
// lives in `text_audit_helpers.cpp`; these `using` declarations expose the
// symbols under the `text_inspection_json` sub-namespace for callers that
// prefer the namespaced qualification `text_inspection_json::json_escape(...)`.
// Callers that include `text_audit_helpers.hpp` directly continue to work.
namespace text_inspection_json {

using ::chronon3d::cli::json_escape;
using ::chronon3d::cli::json_bbox;

} // namespace chronon3d::cli::text_inspection_json
} // namespace chronon3d::cli
