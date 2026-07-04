// SPDX-License-Identifier: MIT
//
// src/text/compiler/text_run_composition.cpp — M1.5#5 stage 5 / 6 / 6.5.
//
// Stage map:
//   5.  compose_paragraph              — thin wrapper around
//                                       compose_single_line_paragraph so
//                                       the orchestrator stays slim.
//   6.  apply_composition_to_placed    — in-place glyph x/y/advance
//                                       adjustment walking composed.lines.
//   6.5 concatenate_runs               — merge multiple PlacedGlyphRuns
//                                       into one continuous run (shifts
//                                       each run's glyphs by the running
//                                       cursor_x; rebases cluster offsets).
//   2b. build_paragraph_text           — concatenate ResolvedTextRun.text
//                                       into the full paragraph source
//                                       string (used before the cache
//                                       lookup AND as the TextRunLayout's
//                                       source_text).
//
// Verbatim body moves from the historical inline bodies inside
// `src/text/text_run_builder.cpp::compile_text_layout` (pre-M1.5#5
// monolith) and the anonymous namespace helpers
// `apply_composition_to_placed`, `paragraph_source_text`,
// `concatenate_runs`.  No behavioural mutations.
//
// Stage ordering rationale:
//   - build_paragraph_text (2b) lives here because it operates on
//     ResolvedTextRun, the resolver's contract surface, and it's used
//     both before the cache lookup (stage 3 prep) AND as the layout's
//     source_text after composition (stages 5-7 work).  Keeping it
//     with the composition helpers keeps the resolver/datatype
//     surface contained.
//   - concatenate_runs (6.5) lives in composition because it
//     operates on PlacedGlyphRun and the downstream composition
//     pipeline assigns cluster offsets post-merge.  The orchestrator
//     only sees the merged PlacedGlyphRun before compose_paragraph.

#include "text_compile_internal.hpp"

namespace chronon3d::text_internal::compile {

// ═══════════════════════════════════════════════════════════════════════════
// Stage 5 — Paragraph composition
// ═══════════════════════════════════════════════════════════════════════════

ParagraphLayout
compose_paragraph(
    const PlacedGlyphRun& merged,
    float box_width,
    const ParagraphStyle& comp_style,
    const std::string& para_text
) {
    return compose_single_line_paragraph(
        merged,
        box_width,
        comp_style,
        para_text
    );
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 6 — Apply composition adjustments to placed glyph positions
// ═══════════════════════════════════════════════════════════════════════════
//
// Walks composed.lines and mutates placed.glyphs in place.  Each line's
// cluster range maps 1:1 to placed.clusters (both built from the same
// HarfBuzz output).  Per-glyph adjustments:
//   - y += line.baseline_y                          (baseline shift)
//   - x += spacing_accum - line.left_overhang       (spacing spread + overhang)
//   - advance_x += per_glyph_spacing                (letter + word spacing)
//   - advance_x *= line.glyph_scale if != 1.0       (glyph scale)
//
// Cumulative spacing: glyph 0 gets +0, glyph 1 gets +s, glyph 2 gets
// +2s, etc.  This is the standard "spread apart" semantic — NOT the
// "shift uniformly" semantic that the pre-refactor version briefly
// produced and the cat-1 revert restored.  See text_run.cpp git
// history (this is verbatim the post-Fase-1.1 fix).

void
apply_composition_to_placed(
    PlacedGlyphRun& placed,
    const ParagraphLayout& composed
) {
    if (composed.lines.empty() || placed.clusters.empty()) return;

    // We use the cluster-to-glyph mapping: ComposedLine indices refer to
    // ShapedCluster positions, which are 1:1 with placed.clusters (both
    // built from the same HarfBuzz output).
    for (const auto& line : composed.lines) {
        if (line.cluster_count == 0) continue;

        size_t first_cluster_idx = line.first_cluster;
        size_t last_cluster_idx  = line.first_cluster + line.cluster_count - 1;

        if (first_cluster_idx >= placed.clusters.size()) break;
        if (last_cluster_idx  >= placed.clusters.size())
            last_cluster_idx = placed.clusters.size() - 1;

        size_t first_glyph = placed.clusters[first_cluster_idx].start_glyph;
        size_t last_glyph  = placed.clusters[last_cluster_idx].end_glyph;
        if (last_glyph > placed.glyphs.size())
            last_glyph = placed.glyphs.size();

        // ── Cumulative spacing accumulator ────────────────────────
        // Letter/word spacing should spread glyphs apart, not shift
        // them uniformly.  We accumulate the per-glyph spacing and
        // add it to each glyph's x so glyph 0 gets +0, glyph 1 gets
        // +s, glyph 2 gets +2s, etc.
        float spacing_accum = 0.0f;
        float per_glyph_spacing = line.letter_spacing_adjustment
                                + line.word_spacing_adjustment;

        for (size_t gi = first_glyph; gi < last_glyph; ++gi) {
            auto& g = placed.glyphs[gi];

            // Baseline: shift y to the line's baseline.
            g.y += line.baseline_y;

            // Cumulative spacing: spread glyphs apart.
            // Apply spacing + overhang in one pass.
            g.x += spacing_accum - line.left_overhang;
            g.advance_x += per_glyph_spacing;
            spacing_accum += per_glyph_spacing;

            // Glyph scale: adjust advance_x proportionally.
            if (line.glyph_scale != 1.0f) {
                g.advance_x *= line.glyph_scale;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Stage 6.5 — Concatenate multiple PlacedGlyphRuns into one continuous run
// ═══════════════════════════════════════════════════════════════════════════

PlacedGlyphRun
concatenate_runs(
    std::vector<PlacedGlyphRun>& runs
) {
    if (runs.empty()) {
        return PlacedGlyphRun{};
    }
    if (runs.size() == 1) {
        return std::move(runs[0]);
    }

    PlacedGlyphRun merged;
    float cursor_x = 0.0f;
    float max_ascent = 0.0f;
    float max_descent = 0.0f;

    for (auto& run : runs) {
        // ── Shift all glyphs in this run by cursor_x ──────────────
        for (auto& g : run.glyphs) {
            g.x += cursor_x;
        }

        // ── Append glyphs ─────────────────────────────────────────
        merged.glyphs.insert(
            merged.glyphs.end(),
            std::make_move_iterator(run.glyphs.begin()),
            std::make_move_iterator(run.glyphs.end()));

        // ── Adjust cluster start_glyph / end_glyph offsets ────────
        size_t glyph_offset = merged.glyphs.size() - run.glyphs.size();
        for (auto& cl : run.clusters) {
            PlacedGlyphRun::Cluster adjusted = cl;
            adjusted.start_glyph += glyph_offset;
            adjusted.end_glyph   += glyph_offset;
            merged.clusters.push_back(adjusted);
        }

        // ── Advance cursor ────────────────────────────────────────
        cursor_x += run.total_width;
        max_ascent  = std::max(max_ascent, run.ascent);
        max_descent = std::max(max_descent, run.descent);
    }

    merged.total_width  = cursor_x;
    merged.ascent       = max_ascent;
    merged.descent      = max_descent;
    merged.total_height  = max_ascent + max_descent;
    merged.baseline     = max_ascent;
    merged.font_size    = runs[0].font_size;  // use first run's size

    return merged;
}

// NOTE: `build_paragraph_text` (stage 2b) lives in `text_run_shaping.cpp`
// alongside `build_cache_key` to keep the cache-prep helpers co-located
// in one TU.  Cross-TU callers reach through the internal header
// `text_compile_internal.hpp`.  Do NOT duplicate the definition here —
// that would create a same-namespace ODR violation.

} // namespace chronon3d::text_internal::compile
