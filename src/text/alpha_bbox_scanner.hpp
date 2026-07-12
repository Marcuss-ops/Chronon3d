// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// alpha_bbox_scanner.hpp — Step 9 §A extracted scanner
//
// REUSABLE alpha-bbox scanner. Walks the entire framebuffer top-to-bottom
// and left-to-right, returning the axis-aligned bounding box of every
// pixel above the alpha threshold. Extracted from
// `src/text/text_visibility_audit.cpp` (where it was a TU-local helper
// in the diagnostics-gated anon namespace) so it is now consumable from
// inspect-text, pipeline parity, glow acceptance, video acceptance, and
// golden test C++/Python consumers without dragging in the diagnostic
// gating macro.
//
// GATING: NONE. This header is unconditionally compiled. The function is
// a pure pixel walk (no allocations, no logging, no side effects). The
// production SDK binary pays only the cost of one tiny function symbol.
// Consumers that need the diagnostic audit (`audit_text_visibility()`,
// `verify_text_visibility()`) still pay the `CHRONON3D_BUILD_DIAGNOSTICS`
// gate at the call-site.
//
// Anti-duplication: replaces the prior parallel copy at
// `src/render_graph/executor/node_runner.cpp:412` (the
// TICKET-SIMPLICITY-CONSERVATIVE-BBOX post-render alpha scan). That copy
// can be dedup'd in a follow-up chore (TICKET-ALPHA-BBOX-SCANNER-DEDUP-EXECUTOR).
//
// Returns an empty `Rect{}` ONLY when the framebuffer has no visible ink
// above the threshold — a legitimate empty-result signal, not a sentinel
// placeholder.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/media/media_placement.hpp>    // chronon3d::Rect
#include <chronon3d/core/memory/framebuffer.hpp>   // full Framebuffer def

namespace chronon3d {

/// Alpha threshold for the scanner. Pixels with `color.a > kAlphaBBoxScanThreshold`
/// are considered "visible ink" by the scanner. Matches the legacy
/// inline `0.01f` constant in the pre-Step-9 `text_visibility_audit.cpp`
/// (a value far below typical per-glyph anti-aliased edge alphas to
/// avoid missing thin strokes). Public so consumers + tests can match
/// the exact threshold when comparing results.
inline constexpr float kAlphaBBoxScanThreshold = 0.01f;

/// `alpha_bbox_scan()` — canonical full-framebuffer alpha-bbox scan.
///
/// Walks every pixel in `fb` (top-to-bottom, left-to-right) and returns
/// the axis-aligned bounding box of the visible-ink region. The walk is
/// O(W*H) and never early-exits (Step 2 fix: multi-line, multi-TextRun,
/// and vertically-separated-glyph compositions rely on the full scan to
/// be measured correctly — a 1-pixel-strip clip composer with a
/// silhouette text would otherwise lose the second line to the early-exit
/// optimisation).
///
/// Returns an empty `Rect{}` when:
///   - the framebuffer has zero width or height, or
///   - no pixel's alpha exceeds `kAlphaBBoxScanThreshold` (no visible ink).
///
/// Both empty-result causes are legitimate signals (NOT sentinels); the
/// caller is responsible for inspecting the result before using it as a
/// measurement. A 0x0 framebuffer is treated as "no ink" by convention.
Rect alpha_bbox_scan(const Framebuffer& fb) noexcept;

} // namespace chronon3d
