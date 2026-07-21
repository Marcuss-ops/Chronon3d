// SPDX-License-Identifier: MIT
//
// text_compile_internal.hpp — M1.5#5 internal header for the compile
// pipeline split.  Strictly INTERNAL.
//
// Lives under src/text/compiler/ — NOT include/chronon3d/, per
// AGENTS.md v0.1 cat-3 freeze.  These helpers are implementation
// details of the canonical `compile_text_layout()` (public entry
// point, declared in <chronon3d/text/text_run_builder.hpp>) and
// `text_internal::compile_text_document()` (internal accumulator).
// They are NOT installed with the SDK, NOT part of the public API
// surface, and NOT visible to external consumers.
//
// Pipeline stages (one canonical order, 7 stages):
//   1. validate_layout_request        (text_compile_validation.cpp)
//   2. check_paragraph_has_font        (text_run_shaping.cpp)
//   3. shape_paragraph_runs            (text_run_shaping.cpp)
//   4. validate_concatenated_run       (text_run_shaping.cpp)
//   5. compose_paragraph               (text_run_composition.cpp)
//   6. apply_composition_to_placed     (text_run_composition.cpp)
//   7. populate_font_spans             (text_font_span_builder.cpp)
//
// Each helper's body is a verbatim move from the historical
// inline body inside `text_run_builder.cpp::compile_text_layout`
// (pre-M1.5#5 monolith).  No behavioural mutations, no signature
// changes to the public API, no new public classes.

#pragma once

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/text/font_engine.hpp>            // FontEngine, FontSpec, FontIdentity
#include <chronon3d/text/paragraph_style.hpp>       // ParagraphStyle
#include <chronon3d/text/single_line_composer.hpp>  // ParagraphLayout, compose_single_line_paragraph
#include <chronon3d/text/text_resolver.hpp>         // ResolvedTextRun
#include <chronon3d/text/text_run.hpp>              // PlacedGlyphRun, ShapedFontSpan, TextRunLayout
#include <chronon3d/text/text_run_builder.hpp>      // TextLayoutError, TextLayoutErrorKind, ShapingFailurePolicy, TextLayoutRequest, TextCompileServices

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::text_internal::compile {

// ═══════════════════════════════════════════════════════════════════════════
// Stage 1 — Validate request + services (text_compile_validation.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// Locks pointer NULL-checks and the EmptySource early-out.  Returns
// Err(MalformedLayout) on null request.doc OR null request.layout,
// Err(ShapingFailed) on null services.engine (intentional asymmetry:
// engine is a SHAPING concern, not a programmer error), and
// Err(EmptySource) when doc has neither utf8 NOR pre-split paragraphs.
//
// On Ok, binds the validated references so the orchestrator and
// downstream stages don't need to repeat pointer deref.  Tree-level
// guards (paragraph_index range, tree.paragraphs.empty) stay in the
// orchestrator because they depend on the resolved-tree reference
// passed to compile_text_layout.

struct ValidatedRefs {
    const TextDocument&   doc;
    const TextLayoutSpec& layout;
    FontEngine&           engine;
};

[[nodiscard]] Result<ValidatedRefs, TextLayoutError>
validate_layout_request(
    const TextLayoutRequest& request,
    TextCompileServices& services
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 2 — MissingFont pre-check + cache-key construction
//          (text_run_shaping.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// check_paragraph_has_font: returns true iff at least one run has a
// non-empty font_path OR non-empty font_family.  False → caller
// surfaces Err(MissingFont).  The check is BEFORE the cache lookup
// because cache entries are written from previously-successful
// compiles — a cache hit cannot have an empty font spec, so cache
// miss + no font spec is exactly the failure mode we surface.
//
// build_paragraph_text: concatenate ResolvedTextRun.text into the
// full paragraph source string used as the cache-key `text` field
// and as the source_text on TextRunLayout.  Order-sensitive: runs
// flow through the resolver in document order, so the resulting
// string is the resolver's view of the paragraph source.
//
// build_cache_key: TextLayoutCacheKey construction honoring
// TICKET-103a — direction, language, features now propagated from
// the request into the key.  Previous monolithic body force-cleared
// these fields; the post-TICKET-103a contract discriminates bidi /
// BCP-47 / OT-shaping-features at the cache layer.

[[nodiscard]] bool
check_paragraph_has_font(
    const std::vector<ResolvedTextRun>& para_runs
);

[[nodiscard]] std::string
build_paragraph_text(
    const std::vector<ResolvedTextRun>& para_runs
);

[[nodiscard]] TextLayoutCacheKey
build_cache_key(
    const std::string& full_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout,
    TextDirection direction,
    const std::string& language,
    const std::string& features,
    const std::string& variation_axes
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 3 — Shape every run (no policy gate; per-run results only)
//          (text_run_shaping.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// Walks para_runs, calls `shape_resolved_run` for each, returns a
// vector<Result<PlacedGlyphRun, TextLayoutError>> with one entry per
// run.  The presence of an Err entry encodes "this run shaped zero
// glyphs for its non-empty text" — that signal is consumed by
// Stage 4 (apply_failure_policy) to issue the configured policy.
//
// Crucially, this stage does NOT know the policy: it just shapes and
// returns the per-run truth.  The split keeps the user-visible 7-stage
// canon (validate → resolve → shape every run → apply failure policy →
// compose → build font spans → store immutable layout) honest.
//
// shape_resolved_run does not return Result (current contract is
// noexcept); when that changes, the per-run Err wrapping here becomes
// the natural seam for it.

[[nodiscard]] std::vector<Result<PlacedGlyphRun, TextLayoutError>>
shape_paragraph_runs(
    const std::vector<ResolvedTextRun>& para_runs,
    FontEngine& engine,
    float tracking,
    const std::string& features = {}
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 4 — Apply ShapingFailurePolicy to per-run results
//          (text_run_shaping.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// Walks the per-run result vector from Stage 3 and applies the
// configured policy.  Default FailWholeParagraph: any single-run Err
// collapses the whole paragraph into a single Err(PerRunShapingFailed)
// sum.  Future policies (FallbackFont, ReplacementGlyph,
// PlaceholderDiagnostic) are post-baseline extensions and live in
// this same function so the orchestrator does not need to know
// about the policy enum beyond passing it through.

[[nodiscard]] Result<std::vector<PlacedGlyphRun>, TextLayoutError>
apply_failure_policy(
    std::vector<Result<PlacedGlyphRun, TextLayoutError>> per_run_results,
    ShapingFailurePolicy policy
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 4.5 — Belt-and-suspenders all-run guard (text_run_shaping.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// After applying per-run policy, every run in placed_runs is guaranteed
// Ok.  This is the paranoid guard for the degenerate case where ALL
// runs had empty text (an empty paragraph with non-empty para_text
// should never happen, but the assumption is cheap to check).
//
// Returns `Result<bool, TextLayoutError>` (NOT `Result<void, …>`)
// because the underlying `chronon3d::Result<T, E>` template requires
// `sizeof(T)` / `alignof(T)` / `T&` accessors — instantiating with
// `T = void` is ill-formed.  The bool value is a marker (true = Ok,
// false = Err) with no other semantics; callers inspect via
// `if (!check) return check.error();`.

[[nodiscard]] Result<bool, TextLayoutError>
validate_concatenated_run(
    const PlacedGlyphRun& merged,
    const std::string& para_text
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 5 — Compose paragraph (text_run_composition.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// Thin wrapper around compose_single_line_paragraph so the orchestrator
// stays orchestrator-only.  Width pre-adjustment (left_indent +
// right_indent) happens in the orchestrator — this helper just
// composes with the already-adjusted `box_width`.

[[nodiscard]] ParagraphLayout
compose_paragraph(
    const PlacedGlyphRun& merged,
    float box_width,
    const ParagraphStyle& comp_style,
    const std::string& para_text
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 6 — Apply composition adjustments to placed glyph positions
//          (text_run_composition.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// In-place mutation.  Walks composed.lines, accumulates spacing,
// adjusts glyph x/y/advance per (line.baseline_y, letter_spacing,
// word_spacing, glyph_scale, left_overhang).  No return value.

void
apply_composition_to_placed(
    PlacedGlyphRun& placed,
    const ParagraphLayout& composed
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 6.5 — Concatenate multiple PlacedGlyphRuns into one continuous run
//            (text_run_composition.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// Lives in composition stage but cross-TU with orchestrator.  Adjusts
// each run's glyph positions so run[0].glyphs[0].x == 0 (unchanged),
// run[i].glyphs[0].x == sum of run[0..i-1].total_width — i.e. each run
// starts where the previous ended.  Cluster start_glyph/end_glyph
// offsets are rebased post-merge.  Aggregation: total_width, ascent,
// descent, baseline, font_size (first run).

[[nodiscard]] PlacedGlyphRun
concatenate_runs(
    std::vector<PlacedGlyphRun>& runs
);

// ═══════════════════════════════════════════════════════════════════════════
// Stage 7 — Populate TextRunLayout::font_spans
//          (text_font_span_builder.cpp)
// ═══════════════════════════════════════════════════════════════════════════
//
// Walks the shaped placed_runs vector (NOT the merged glyph run) so
// each run's pre-concatenation index range maps to glyph indices in
// the merged run via standard cursor arithmetic.  Emits one
// ShapedFontSpan per run with the corresponding FontIdentity.
//
// Diagnostic spdlog::warn on the multi-font anomaly
// (font_spans.empty() && para.runs.size() > 1) — Phase 1.4 surface.

void
populate_font_spans(
    TextRunLayout& text_layout,
    const std::vector<PlacedGlyphRun>& placed_runs,
    const std::vector<ResolvedTextRun>& para_runs
);

} // namespace chronon3d::text_internal::compile
