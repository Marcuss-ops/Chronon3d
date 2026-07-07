// SPDX-License-Identifier: MIT
//
// src/text/compiler/text_run_shaping.cpp — M1.5#5 stages 2 / 3 / 4 / 4.5.
//
//   2.    check_paragraph_has_font           — MissingFont pre-check
//   2b.   build_paragraph_text                — paragraph source string
//   2c.   build_cache_key                     — TextLayoutCacheKey wrt TICKET-103a
//                                                 (direction + language + features)
//   3.    shape_paragraph_runs                — multi-run HarfBuzz shaping;
//                                                 no policy gate; returns per-run
//                                                 Ok/Err result vector
//   3.5/4 apply_failure_policy                — walks per-run results; applies
//                                                 ShapingFailurePolicy (default
//                                                 FailWholeParagraph)
//   4.5   validate_concatenated_run           — belt-and-suspenders all-run guard
//
// Verbatim body moves from the historical inline blocks inside
// `src/text/text_run_builder.cpp::compile_text_layout` (pre-M1.5#5
// monolith) and the anonymous namespace helpers `build_cache_key` and
// `paragraph_source_text`.  No behavioural mutations.
//
// Why a separate apply_failure_policy stage
// =========================================
//
// The user-requested canon (validate → resolve → shape every run →
// apply failure policy → compose → build font spans → store) keeps the
// "policy" decision visually separated from the "shape" step.
// Pre-M1.5#5, the policy switch inside `compile_text_layout` was
// interleaved with the per-run shaping loop — readable, but the
// orchestrator's linear narrative of stages was violated.
//
// M1.5#5 splits at the natural seam: shape_paragraph_runs just shapes
// and reports per-run results (vector<Result<PlacedGlyphRun, Err>>);
// apply_failure_policy walks that vector and applies the configured
// policy.  Future policies (FallbackFont, ReplacementGlyph, …) plug
// into apply_failure_policy without any orchestrator change.

#include "text_compile_internal.hpp"

#include <cstddef>
#include <utility>

namespace chronon3d::text_internal::compile {

// ═══════════════════════════════════════════════════════════════════════════
// Stage 2 — MissingFont pre-check
// ═══════════════════════════════════════════════════════════════════════════

bool
check_paragraph_has_font(
    const std::vector<ResolvedTextRun>& para_runs
) {
    bool has_font = false;
    for (const auto& r : para_runs) {
        if (!r.font.font_path.empty() || !r.font.font_family.empty()) {
            has_font = true;
            break;
        }
    }
    return has_font;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 2b — Build paragraph source text
// ═══════════════════════════════════════════════════════════════════════════
//
// Lives in this TU for proximity to the cache-key (which uses the
// source text).  Verbatim from the pre-M1.5#5 anonymous-namespace
// `paragraph_source_text`.  Order-sensitive: resolved runs flow in
// document order, so the resulting string is the resolver's view.

std::string
build_paragraph_text(
    const std::vector<ResolvedTextRun>& para_runs
) {
    std::string text;
    for (const auto& r : para_runs) {
        text += r.text;
    }
    return text;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 2c — Build TextLayoutCacheKey (TICKET-103a honoring)
// ═══════════════════════════════════════════════════════════════════════════
//
// TICKET-103a — the 3 new TextLayoutRequest fields (direction, language,
// features) are now honored by the cache key directly.  The previous
// overrides that forced `direction = Auto` and `language.clear()` are
// GONE — bidi / BCP-47 / OT-shaping-features parity is what the cache
// key discriminates, so LTR vs RTL inputs MUST collide on different
// keys (graceful LTR-only behavior was a workaround-era cost; bidi
// callers now share a memory footprint with their actual cache contents).

TextLayoutCacheKey
build_cache_key(
    const std::string& full_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout,
    TextDirection direction,
    const std::string& language,
    const std::string& features,
    const std::string& variation_axes
) {
    TextLayoutCacheKey key;
    key.text         = full_text;
    key.font_path    = primary_font.font_path;
    key.font_family  = primary_font.font_family;   // P0-2
    key.font_weight  = primary_font.font_weight;
    key.font_style   = primary_font.font_style;
    key.font_size    = primary_font.font_size;
    key.tracking    = layout.tracking;
    key.box_width   = layout.box.x;
    key.wrap        = layout.wrap;
    key.direction   = direction;             // TICKET-103a — was: TextDirection::Auto
    key.language    = language;              // TICKET-103a — was: cleared string
    key.features    = features;              // TICKET-103a — new field
    key.variation_axes = variation_axes;     // M1.5#5
    key.paragraph   = layout.paragraph;

    // TICKET-TEXT-CLEANUP-6: include all visual-affecting TextLayoutSpec fields
    key.box_height      = layout.box.y;
    key.align           = static_cast<int>(layout.align);
    key.vertical_align  = static_cast<int>(layout.vertical_align);
    key.anchor          = static_cast<int>(layout.anchor);
    key.centering_mode  = static_cast<int>(layout.centering_mode);
    key.line_height     = layout.line_height;
    key.max_lines       = layout.max_lines;
    key.overflow        = static_cast<int>(layout.overflow);
    key.ellipsis        = layout.ellipsis;

    return key;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 3 — Shape every run (no policy gate)
// ═══════════════════════════════════════════════════════════════════════════
//
// Walks para_runs, calls `shape_resolved_run` for each, produces a
// per-run Result.  The Err branch reads "this run shaped zero glyphs
// for its non-empty text" — apply_failure_policy consumes that signal.
//
// No policy logic here — keep the orchestrator pipeline linear and
// preserve the canonical 7-stage visual narrative.

std::vector<Result<PlacedGlyphRun, TextLayoutError>>
shape_paragraph_runs(
    const std::vector<ResolvedTextRun>& para_runs,
    FontEngine& engine,
    float tracking
) {
    std::vector<Result<PlacedGlyphRun, TextLayoutError>> per_run_results;
    per_run_results.reserve(para_runs.size());

    for (std::size_t ri = 0; ri < para_runs.size(); ++ri) {
        const auto& run = para_runs[ri];
        PlacedGlyphRun placed = shape_resolved_run(run, engine, tracking);

        // ── Per-run failure detection ────────────────────────
        // A run whose text is non-empty but HarfBuzz produced zero
        // glyphs means the font rejected the input (e.g. missing
        // glyph coverage for a script).  Encode that as a per-run
        // Err; apply_failure_policy consumes it on the next stage.
        if (placed.glyphs.empty() && !run.text.empty()) {
            per_run_results.push_back(TextLayoutError{
                TextLayoutErrorKind::PerRunShapingFailed,
                "compile_text_layout: run " + std::to_string(ri)
                + " of " + std::to_string(para_runs.size())
                + " failed shaping (HarfBuzz produced zero glyphs"
                + " for non-empty text '" + run.text + "')"
            });
            continue;
        }

        per_run_results.push_back(std::move(placed));
    }

    return per_run_results;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 3.5 / 4 — Apply ShapingFailurePolicy to per-run results
// ═══════════════════════════════════════════════════════════════════════════
//
// Default FailWholeParagraph — the only policy currently shipped:
// any single-run Err collapses the whole paragraph into a single
// Err(PerRunShapingFailed).  This is the safe default: no text is
// ever silently dropped, and callers detect the failure mode at
// compile time rather than observing a zero-glyph render artifact.
//
// Future policies (FallbackFont, ReplacementGlyph, PlaceholderDiagnostic)
// are post-baseline extensions and plug into the switch here without
// orchestrator changes.

Result<std::vector<PlacedGlyphRun>, TextLayoutError>
apply_failure_policy(
    std::vector<Result<PlacedGlyphRun, TextLayoutError>> per_run_results,
    ShapingFailurePolicy policy
) {
    std::vector<PlacedGlyphRun> placed_runs;
    placed_runs.reserve(per_run_results.size());

    for (std::size_t ri = 0; ri < per_run_results.size(); ++ri) {
        auto& entry = per_run_results[ri];
        if (!entry) {
            switch (policy) {
            case ShapingFailurePolicy::FailWholeParagraph:
                return entry.error();
            }
        }
        placed_runs.push_back(std::move(entry.value()));
    }

    return placed_runs;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 4.5 — Belt-and-suspenders all-run guard
// ═══════════════════════════════════════════════════════════════════════════

Result<bool, TextLayoutError>
validate_concatenated_run(
    const PlacedGlyphRun& merged,
    const std::string& para_text
) {
    if (merged.glyphs.empty() && !para_text.empty()) {
        return TextLayoutError{
            TextLayoutErrorKind::ShapingFailed,
            "compile_text_layout: HarfBuzz produced no glyphs for non-empty paragraph"
        };
    }
    return true;  // Ok — bool marker (Result<void, E> is ill-formed in this project)
}

} // namespace chronon3d::text_internal::compile
