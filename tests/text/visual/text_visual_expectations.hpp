// ═══════════════════════════════════════════════════════════════════════════
// tests/text/visual/text_visual_expectations.hpp — Phase-2.1 P0 split
//
// VisualExpectation enum + expectation_name() + kVisibleMinPixels
// threshold + VR_TEXT_PRESET_GATE macro.  Mechanical split-off from
// tests/text/test_text_preset_visual.cpp (formerly 165–235 LOC in the  // drift-allow: stale-ref
// anonymous namespace).
//
// ── VisualExpectation (Blocco 2) ─────────────────────────────────────────
// Replaces the audit-flagged binary `expected_visible` (which conflated
// "intentionally transparent entrance" with "sub-threshold mid-animation"
// — both produced ink_pixels == 0 under the 0.05 alpha filter in
// compute_metrics). Four states, each sentinel declares its outcome once:
//
//   * Transparent       — frame is INTENTIONALLY blank (entrance
//                         opacity=0 at F000, etc.); ink_pixels == 0.
//   * PartialCoverage   — frame has SOME ink above the 0.05 alpha
//                         filter but is mid-animation (diluted alpha,
//                         vertically compressed copy): 0 < ink_pixels
//                         < kVisibleMinPixels.  Covers the audit-cited
//                         BlurIn F020 (focus_in blur) and
//                         MaskedLineReveal F020 (center_split scale_y).
//   * Visible           — frame has substantial text render;
//                         ink_pixels >= kVisibleMinPixels.
//   * GoldenReference   — PLACEHOLDER for Blocco 3 (PNG dump + hash
//                         capture + bbox + coverage comparison).  No
//                         current sentinel uses this.
//
// Threshold kVisibleMinPixels = 2500 was chosen empirically from the
// observed distribution on HEAD 3f14e101; minimum non-zero ink_pixels
// in current rendering is 2601; partial/mid-animation frames (F020
// BlurIn, F020 MaskedLineReveal) sit at ink_pixels == 0 because the
// 0.05 alpha filter in compute_metrics suppresses diluted pixels.  A
// future Blocco 3 PR will replace ink-based checks with hash capture
// and may retire the constant.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstddef>
#include <string_view>

#include <doctest/doctest.h>
#include "tests/text/visual/text_visual_metrics.cpp"   // for `auto gate_m = (metrics_expr);` pattern + is_reference_captured

enum class VisualExpectation {
    Transparent,
    PartialCoverage,
    Visible,
    GoldenReference,
};

inline std::string_view expectation_name(VisualExpectation e) {
    switch (e) {
        case VisualExpectation::Transparent:     return "Transparent";
        case VisualExpectation::PartialCoverage: return "PartialCoverage";
        case VisualExpectation::Visible:         return "Visible";
        case VisualExpectation::GoldenReference: return "GoldenReference";
    }
    return "Unknown";  // unreachable on well-formed enum; suppresses -Wswitch warning
}

// Empirical gap between Transparent/PartialCoverage and Visible.  See
// docs/baselines/ for the observed distribution that calibrates this
// value; do not duplicate here to avoid drift.
inline constexpr std::size_t kVisibleMinPixels = 2500;

// Sentinel-gate macro for Text Preset visual regression. 'short_label'
// is a stable token used by the grep-based capture workflow.  NOT
// auto-derived from a TEST_CASE name to avoid whitespace /
// special-character fragility.
//
// Branch semantics (kept intentionally lean — no body comments; see
// the VisualExpectation enum block above for the WHY behind each branch).
//
// TICKET-038 binding: macro-local declaration renamed `gate_m` so the
// RHS `auto gate_m = (metrics_expr)` unambiguously receives the
// caller's `m` (or fresh inline expression).
#define VR_TEXT_PRESET_GATE(short_label, kref, metrics_expr, expectation)   \
    do {                                                                   \
        auto gate_m = (metrics_expr);                                       \
        if (is_reference_captured(kref)) {                                  \
            REQUIRE(gate_m.hash == kref);                                   \
        } else {                                                            \
            MESSAGE("VR/Text/" << short_label                               \
                    << " unset; first hash to capture: " << gate_m.hash);   \
        }                                                                   \
        MESSAGE("VR/Text/" << short_label                                   \
                << " gate=" << expectation_name(expectation)                \
                << " ink_pixels=" << gate_m.ink_pixels                      \
                << " alpha_coverage=" << gate_m.alpha_coverage);            \
        if (expectation == VisualExpectation::Transparent) {               \
            CHECK(gate_m.ink_pixels == 0);                                  \
        } else if (expectation == VisualExpectation::PartialCoverage) {     \
            CHECK(gate_m.ink_pixels < kVisibleMinPixels);                   \
        } else if (expectation == VisualExpectation::Visible) {             \
            CHECK(gate_m.ink_pixels >= kVisibleMinPixels);                  \
        } else if (expectation == VisualExpectation::GoldenReference) {     \
            FAIL_CHECK("TODO Blocco 3 \u2014 GoldenReference matches PNG dump +"  \
                       " hash capture which are not yet implemented; tag"   \
                       " this sentinel with VisualExpectation::{Transparent,"\
                       "PartialCoverage,Visible} instead.");                  \
        }                                                                   \
    } while (0)
