// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/text_bidi_resolver.hpp — INTERNAL
//
// M1.5#8 — public surface declaration of the bidi resolver module.
// STRICTLY INTERNAL: lives under src/text/resolver/, NOT in
// include/chronon3d/, NOT installed with the SDK (Cat-3 freeze
// compliance — no new public SDK headers).
//
// Consumers in M1.5#8:
//   * text_run_resolver.cpp — calls emit_via_bidi (orchestrator).
//
// The body lives in src/text/resolver/text_bidi_resolver.cpp.

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/text_resolver.hpp>      // ResolvedTextRun, ParagraphRange
#include <chronon3d/text/text_document.hpp>

#include <cstddef>
#include <vector>

// Note: brace-pair to keep sed parser happy with the include.

#include "src/text/resolver/text_span_resolver.hpp"

#include <filesystem>

namespace chronon3d::text::resolver {

// ── emit_via_bidi — bidi-segment a font-homogeneous sub-range ──────────
//
// For one FontSubRange (already split by text_span_resolver), this helper
// segments the byte slice through the backend's `segment_bidi_runs`
// (src/backends/text/bidi_segmenter.cpp), resolves the font ONCE via
// FontResolver (text_font_resolver.cpp), and pushes the resulting
// ResolvedTextRun entries into `out`.
//
// `override_dir` is the paragraph-level TextDirection.  When
// TextDirection::Auto (the value used when no override is set on the
// paragraph), each run inherits its per-segment direction from the
// bidi segmentation.  When set explicitly to LTR or RTL, the helper
// uses override_dir for every run regardless of bidi segmentation
// signals (preserves the pre-split behaviour).

/// @return Number of codepoints/clusters in the sub-range not covered by
/// any font in the fallback stack (fail-loud audit count).
[[nodiscard]] std::size_t emit_via_bidi(
    std::vector<ResolvedTextRun>&           out,
    const TextDocument&                     doc,
    FontEngine&                             engine,
    const FontSubRange&                     sub,
    const ParagraphRange&                   para,
    TextDirection                           override_dir,
    const std::filesystem::path&            bundled_fonts_root
);

} // namespace chronon3d::text::resolver
