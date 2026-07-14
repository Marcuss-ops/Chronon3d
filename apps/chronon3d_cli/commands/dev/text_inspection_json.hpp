// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_inspection_json.hpp — JSON helper re-exports (Step 9 §B, P1-7 slimmed)
//
// P1-7 Chore 1 (commit B): this header is RE-EXPORTING the canonical JSON
// helpers `json_escape` + `json_bbox(const TextAuditBBox&)` from
// `text_audit_helpers.hpp` (single canonical home).  The previously-hosted
// `audit_result_to_json()` function was deleted wholesale along with
// the legacy text-audit engine (Commit B scope) — the function is now
// orphaned with no callers and no consumers.  This header's raison d'être
// is now the `using`-declaration re-exports for callers that prefer the
// namespaced qualification (no behavioral change vs direct include).
//
// Callers that need `json_escape`/`json_bbox` can:
//   1. include `text_audit_helpers.hpp` directly (canonical path), OR
//   2. include this header + use `text_inspection_json::json_escape(...)`.
//
// Anti-duplication invariant: zero duplicate definitions in the codebase.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "text_audit_helpers.hpp"   // canonical json_escape + json_bbox(BBox)

#include <string>

namespace chronon3d::cli {

// Re-exported JSON helpers (single implementation lives in
// `text_audit_helpers.cpp`; these `using` declarations expose the symbols
// under the `text_inspection_json` sub-namespace for namespaced callers).
namespace text_inspection_json {

using ::chronon3d::cli::json_escape;
using ::chronon3d::cli::json_bbox;

} // namespace chronon3d::cli::text_inspection_json
} // namespace chronon3d::cli
