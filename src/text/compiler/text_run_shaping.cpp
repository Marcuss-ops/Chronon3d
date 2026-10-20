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

#include <chronon3d/backends/text/text_unicode_grapheme.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <unicode/utf8.h>
#include <unicode/uscript.h>

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
    float tracking,
    const std::string& features,
    const std::string& variation_axes
) {
    std::vector<Result<PlacedGlyphRun, TextLayoutError>> per_run_results;
    per_run_results.reserve(para_runs.size());

    const auto can_shape_together = [](const ResolvedTextRun& a,
                                       const ResolvedTextRun& b) {
        return a.direction == b.direction &&
            a.font == b.font && a.paragraph_style.language == b.paragraph_style.language &&
            a.byte_offset + a.byte_len == b.byte_offset;
    };
    const auto strong_script = [](UScriptCode script) {
        return script != USCRIPT_COMMON && script != USCRIPT_INHERITED &&
               script != USCRIPT_UNKNOWN;
    };
    const auto script_segments = [&](std::string_view text) {
        std::vector<std::pair<std::size_t, std::size_t>> segments;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
        const int32_t length = static_cast<int32_t>(text.size());
        int32_t cursor = 0, start = 0;
        UScriptCode current = USCRIPT_UNKNOWN;
        while (cursor < length) {
            const int32_t char_start = cursor;
            UChar32 cp = 0;
            U8_NEXT(bytes, cursor, length, cp);
            if (cp < 0) continue;
            UErrorCode error = U_ZERO_ERROR;
            const auto script = uscript_getScript(cp, &error);
            if (U_FAILURE(error) || !strong_script(script)) continue;
            if (!strong_script(current)) { current = script; continue; }
            if (script == current) continue;
            segments.emplace_back(static_cast<std::size_t>(start),
                                  static_cast<std::size_t>(char_start));
            start = char_start;
            current = script;
        }
        if (start < length)
            segments.emplace_back(static_cast<std::size_t>(start), text.size());
        if (segments.empty() && !text.empty()) segments.emplace_back(0, text.size());
        return segments;
    };
    const auto shape_scripted = [&](const ResolvedTextRun& source) {
        const auto segments = source.direction == TextDirection::LTR
            ? script_segments(source.text)
            : std::vector<std::pair<std::size_t, std::size_t>>{{0, source.text.size()}};
        if (segments.size() <= 1)
            return shape_resolved_run(source, engine, tracking, features, variation_axes);

        PlacedGlyphRun result;
        float pen = 0.0f;
        for (const auto [start, end] : segments) {
            auto segment = source;
            segment.text = source.text.substr(start, end - start);
            segment.byte_offset += start;
            segment.byte_len = segment.text.size();
            auto shaped = shape_resolved_run(segment, engine, tracking, features,
                                             variation_axes);
            if (shaped.glyphs.empty() && !segment.text.empty()) return PlacedGlyphRun{};
            if (result.glyphs.empty()) {
                result.ascent = shaped.ascent;
                result.descent = shaped.descent;
                result.baseline = shaped.baseline;
                result.total_height = shaped.total_height;
                result.font_size = shaped.font_size;
            }
            const auto glyph_base = result.glyphs.size();
            for (auto glyph : shaped.glyphs) {
                glyph.x += pen;
                glyph.byte_offset += start;
                result.glyphs.push_back(std::move(glyph));
            }
            for (auto cluster : shaped.clusters) {
                cluster.start_glyph += glyph_base;
                cluster.end_glyph += glyph_base;
                cluster.byte_offset += start;
                result.clusters.push_back(std::move(cluster));
            }
            pen += shaped.total_width;
        }
        result.total_width = pen;
        return result;
    };
    for (std::size_t begin = 0; begin < para_runs.size();) {
        std::size_t end = begin + 1;
        while (end < para_runs.size() &&
               can_shape_together(para_runs[end - 1], para_runs[end])) ++end;
        if (end - begin == 1 &&
            (para_runs[begin].direction != TextDirection::LTR ||
             script_segments(para_runs[begin].text).size() <= 1)) {
            const auto& run = para_runs[begin];
            auto placed = shape_scripted(run);
            if (placed.glyphs.empty() && !run.text.empty()) {
                per_run_results.push_back(make_text_layout_error(
                    TextLayoutErrorKind::PerRunShapingFailed,
                    "compile_text_layout: run " + std::to_string(begin) + " of " +
                    std::to_string(para_runs.size()) + " failed shaping (HarfBuzz produced zero glyphs for non-empty text '" + run.text + "')"));
            } else {
                per_run_results.push_back(std::move(placed));
            }
            begin = end;
            continue;
        }

        ResolvedTextRun combined_run = para_runs[begin];
        combined_run.text.clear();
        std::vector<std::size_t> boundaries{0};
        for (std::size_t index = begin; index < end; ++index) {
            combined_run.text += para_runs[index].text;
            boundaries.push_back(combined_run.text.size());
        }
        combined_run.byte_len = combined_run.text.size();
        auto combined = shape_scripted(combined_run);
        if (combined.glyphs.empty() && !combined_run.text.empty()) {
            for (std::size_t index = begin; index < end; ++index)
                per_run_results.push_back(make_text_layout_error(
                    TextLayoutErrorKind::PerRunShapingFailed,
                    "compile_text_layout: combined paint chunk failed shaping"));
            begin = end;
            continue;
        }
        bool boundaries_are_cluster_edges = true;
        bool boundary_splits_grapheme = false;
        const auto is_grapheme_boundary = [&](std::size_t byte) {
            const auto count = detail::grapheme_cluster_count(combined_run.text);
            for (std::size_t index = 0; index <= count; ++index) {
                if (detail::grapheme_byte_offset_at(combined_run.text, index) == byte)
                    return true;
            }
            return false;
        };
        for (std::size_t boundary = 1; boundary + 1 < boundaries.size(); ++boundary) {
            const auto byte = boundaries[boundary];
            if (!is_grapheme_boundary(byte)) boundary_splits_grapheme = true;
            if (std::none_of(combined.clusters.begin(), combined.clusters.end(),
                    [byte](const auto& cluster) { return cluster.byte_offset == byte; })) {
                boundaries_are_cluster_edges = false;
                break;
            }
        }
        if (!boundaries_are_cluster_edges && !boundary_splits_grapheme) {
            // A glyph cluster cannot be split between paint spans. Shape each
            // authored run independently so a ligature never inherits only
            // the first character's color. Kerning across this boundary is
            // intentionally not synthesized from a partial cluster.
            for (std::size_t index = begin; index < end; ++index) {
                const auto& run = para_runs[index];
                auto placed = shape_scripted(run);
                if (placed.glyphs.empty() && !run.text.empty())
                    per_run_results.push_back(make_text_layout_error(
                        TextLayoutErrorKind::PerRunShapingFailed,
                        "compile_text_layout: paint run at a ligature boundary failed shaping"));
                else
                    per_run_results.push_back(std::move(placed));
            }
            begin = end;
            continue;
        }
        if (boundary_splits_grapheme) {
            // Keep the shaped grapheme in one placed run so its base/mark
            // offsets and shared line baseline remain intact. When HarfBuzz
            // exposes separate source clusters inside that grapheme, carry
            // their authored paint on the glyph instead of splitting its
            // geometry into text runs.
            for (auto& glyph : combined.glyphs) {
                const auto upper = std::upper_bound(boundaries.begin(), boundaries.end(),
                                                    glyph.byte_offset);
                const std::size_t owner = std::min<std::size_t>(
                    upper == boundaries.begin() ? 0 : upper - boundaries.begin() - 1,
                    para_runs.size() - 1);
                glyph.fill_color_override = para_runs[begin + owner].fill_color_override;
            }
            per_run_results.push_back(std::move(combined));
            begin = end;
            continue;
        }
        std::vector<PlacedGlyphRun> split(end - begin);
        std::vector<std::size_t> owners(combined.clusters.size(), 0);
        std::vector<float> owner_origins(split.size(), 0.0f);
        std::vector<bool> owner_has_origin(split.size(), false);
        float pen = 0.0f;
        for (std::size_t ci = 0; ci < combined.clusters.size(); ++ci) {
            const auto& cluster = combined.clusters[ci];
            // Preserve HarfBuzz's positioned sub-clusters inside an extended
            // grapheme. If the base and mark have distinct source clusters,
            // this lets a paint boundary color the mark without reshaping or
            // changing the shared glyph positions.
            const auto upper = std::upper_bound(boundaries.begin(), boundaries.end(),
                                                cluster.byte_offset);
            const std::size_t owner = std::min<std::size_t>(
                upper == boundaries.begin() ? 0 : upper - boundaries.begin() - 1,
                split.size() - 1);
            owners[ci] = owner;
            if (!owner_has_origin[owner]) {
                owner_origins[owner] = pen;
                owner_has_origin[owner] = true;
            }
            pen += combined.clusters[ci].advance;
        }
        for (std::size_t ci = 0; ci < combined.clusters.size(); ++ci) {
            const auto& source = combined.clusters[ci];
            const std::size_t owner = owners[ci];
            auto& target = split[owner];
            target.ascent = combined.ascent;
            target.descent = combined.descent;
            target.baseline = combined.baseline;
            target.total_height = combined.total_height;
            target.font_size = combined.font_size;
            const std::size_t first_glyph = target.glyphs.size();
            for (std::size_t gi = source.start_glyph;
                 gi < source.end_glyph && gi < combined.glyphs.size(); ++gi) {
                auto glyph = combined.glyphs[gi];
                // Preserve advance between clusters that share one paint span.
                // Reset only at a paint boundary; resetting at every cluster
                // collapsed mixed-script glyphs onto the same origin. Cluster
                // iteration is in logical byte order; for RTL this subtracts
                // the preceding logical chunk width, and concatenate_runs
                // restores that prefix while retaining HarfBuzz's visual x.
                glyph.x -= owner_origins[owner];
                target.glyphs.push_back(std::move(glyph));
            }
            auto cluster = source;
            cluster.start_glyph = first_glyph;
            cluster.end_glyph = target.glyphs.size();
            target.total_width += source.advance;
            target.clusters.push_back(std::move(cluster));
        }
        for (auto& part : split) per_run_results.push_back(std::move(part));
        begin = end;
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
    // FU02next — pre-render invariant: ShapingProducedNoGlyphs
    // is the canonical Kind for "merged PlacedGlyphRun has zero
    // glyphs despite non-empty input".  Renamed from ShapingFailed
    // (the legacy label) as part of FU02next's invariant taxonomy
    // alignment.  Distinguishes the post-shape zero-glyphs outcome
    // from per-run shaping failures (PerRunShapingFailed) and from
    // upstream font-absence failures (MissingFont).
    if (merged.glyphs.empty() && !para_text.empty()) {
        return make_text_layout_error(
            TextLayoutErrorKind::ShapingProducedNoGlyphs,
            "compile_text_layout: HarfBuzz produced no glyphs for non-empty paragraph");
    }
    return true;  // Ok — bool marker (Result<void, E> is ill-formed in this project)
}

} // namespace chronon3d::text_internal::compile
