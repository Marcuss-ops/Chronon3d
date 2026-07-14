// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// text_inspection_json.cpp — Step 9 §B implementation (P1-7 Commit B slimmed)
//
// P1-7 Chore 1 (commit B): the previously-hosted `audit_result_to_json()`
// has been DELETED wholesale along with the legacy text-audit engine
// (`text_audit_engine.cpp` + `text_audit_typewriter.cpp` +
// `command_text_audit.cpp` + their headers).  This TU remains so that:
//   1. The header chain (`text_inspection_json.hpp` still exists as a
//      thin re-export façade for `json_escape` + `json_bbox`) has at
//      least ONE .cpp counterpart to honour the
//      `chronon3d_cli_dev` target_sources(static-lib) inclusion in
//      `apps/chronon3d_cli/CMakeLists.txt`.
//   2. Future compile-time regressions (e.g. header-side ABI lock) have
//      a place to land their compile-time tests without re-creating the
//      deleted JSON body.
//
// If a Cat-3 audit later flags this TU as dead code in full, the proper
// fix is to delete the .cpp + .hpp pair AND remove the target_sources
// entry — not to leave the file present-but-empty.
// ═══════════════════════════════════════════════════════════════════════════

#include <string>
