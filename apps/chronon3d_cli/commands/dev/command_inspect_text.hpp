// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// command_inspect_text.hpp — §12 FU09 / TICKET-SIMPLICITY-INSPECT-TEXT
//
// Per-node TextRun audit with structured JSON output + exit code mapping:
//   0 = PASS    (all nodes pass critical invariants)
//   1 = FAIL    (composition not found, non-diagnostic build, or any node
//                has font_resolved=false / finite=false / shaping_succeeded=false)
//   2 = VIOLATION (any node has predicted_contains_world=false — FU04 trigger)
//
// The function is declared here, implemented in command_inspect_text.cpp.
// ═══════════════════════════════════════════════════════════════════════════
