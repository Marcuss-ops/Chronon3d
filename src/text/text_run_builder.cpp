// ═══════════════════════════════════════════════════════════════════════════
// text_run_builder.cpp — Full-pipeline text run builder implementation
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/text/single_line_composer.hpp>
#include <chronon3d/text/text_resolver.hpp>

#include <algorithm>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Concatenate multiple PlacedGlyphRuns into one continuous run,
/// adjusting glyph positions so each run starts where the previous
/// ended.
[[nodiscard]] PlacedGlyphRun concatenate_runs(
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

/// Build a TextLayoutCacheKey for a single paragraph's content.
/// Only used when the paragraph has exactly one run (font-homogeneous).
/// Multi-font paragraphs skip caching to avoid false hits.
[[nodiscard]] TextLayoutCacheKey build_cache_key(
    const std::string& full_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout
) {
    TextLayoutCacheKey key;
    key.text        = full_text;
    key.font_path   = primary_font.font_path;
    key.font_weight = primary_font.font_weight;
    key.font_style  = primary_font.font_style;
    key.font_size   = primary_font.font_size;
    key.tracking    = layout.tracking;
    key.box_width   = layout.box.x;
    key.wrap        = layout.wrap;
    key.direction   = TextDirection::Auto;  // bidi handled per-run
    key.language.clear();
    key.paragraph   = layout.paragraph;
    return key;
}

/// Apply composition adjustments (line breaks, justification, overhang)
/// to the PlacedGlyphRun glyph positions.  This turns raw-shaped glyph
/// coordinates into final rendered positions.
void apply_composition_to_placed(
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

/// Build the paragraph text by concatenating all run texts (for the
/// source_text argument of the composer).
[[nodiscard]] std::string paragraph_source_text(
    const std::vector<ResolvedTextRun>& para_runs
) {
    std::string text;
    for (const auto& r : para_runs) {
        text += r.text;
    }
    return text;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEXT-PLY-01 — spacing_collapse wired between consecutive paragraphs
// ═══════════════════════════════════════════════════════════════════════════
//
// Previously the `paragraph_style.hpp::ParagraphSpacingCollapse` rule was
// defined but never applied.  This post-process pass walks the resolved
// paragraph list and adjusts each paragraph's `bounds.y` so that the gap
// between paragraphs reflects the collapse rule instead of independent
// space_after + space_before.
//
// Rule semantics:
//   - Add:        gap = prev.space_after + cur.space_before
//   - Maximum:    gap = max(prev.space_after, cur.space_before)
//
// Implementation:
//   1. Subtract `prev.space_after` from prev paragraph's bounds.y.
//   2. Replace `cur.space_before` in cur paragraph's bounds.y with the
//      merged gap, so cur is effectively positioned `merged` below prev.

namespace {

inline void apply_spacing_collapse(
    TextRunBuildResult& result,
    const ResolvedTextTree& tree,
    const TextLayoutSpec& layout
) {
    using ParagraphStyle = chronon3d::ParagraphStyle;
    using ParagraphSpacingCollapse = chronon3d::ParagraphSpacingCollapse;

    if (result.paragraphs.size() < 2) return;
    if (tree.paragraphs.size() < 2) return;

    for (size_t i = 1; i < result.paragraphs.size(); ++i) {
        ParagraphStyle prev_style;
        if (!(tree.paragraphs[i-1].style == ParagraphStyle{})) {
            prev_style = tree.paragraphs[i-1].style;
        } else {
            prev_style = layout.paragraph;
        }

        ParagraphStyle cur_style;
        if (!(tree.paragraphs[i].style == ParagraphStyle{})) {
            cur_style = tree.paragraphs[i].style;
        } else {
            cur_style = layout.paragraph;
        }

        const float prev_after  = prev_style.space_after;
        const float cur_before  = cur_style.space_before;
        if (prev_after <= 0.0f && cur_before <= 0.0f) continue;

        float merged_gap = cur_before;
        switch (prev_style.spacing_collapse) {
        case ParagraphSpacingCollapse::Add:
            merged_gap = prev_after + cur_before;
            break;
        case ParagraphSpacingCollapse::Maximum:
            merged_gap = std::max(prev_after, cur_before);
            break;
        }

        // Step 1: remove prev.space_after from prev paragraph's bounds.
        if (prev_after > 0.0f) {
            result.paragraphs[i-1]->bounds.y -= prev_after;
        }
        // Step 2: replace cur.space_before with merged_gap (delta shift).
        const float delta = merged_gap - cur_before;
        if (delta != 0.0f) {
            result.paragraphs[i]->bounds.y += delta;
        }
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// build_text_run
// ═══════════════════════════════════════════════════════════════════════════

TextRunBuildResult build_text_run(
    const TextDocument& doc,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    TextLayoutCache* cache
) {
    TextRunBuildResult result;

    if (doc.utf8.empty() && doc.paragraphs.empty()) {
        return result;
    }

    // ── Resolve document → paragraph tree ───────────────────────────
    auto tree = resolve_text_run_tree(doc, engine);

    // ── Process each paragraph ───────────────────────────────────────
    for (const auto& para : tree.paragraphs) {

        // ── Empty paragraph (e.g. two consecutive \n) ──────────────
        if (para.runs.empty()) {
            auto empty_layout = std::make_shared<TextRunLayout>();
            empty_layout->source_text.clear();
            empty_layout->font = doc.defaults.font;
            empty_layout->font_size = doc.defaults.font.font_size;
            empty_layout->bounds = {0.0f, 0.0f};
            empty_layout->line_height = doc.defaults.font.font_size * 1.2f;
            result.paragraphs.push_back(std::move(empty_layout));
            continue;
        }

        // ── Cache check (single-font paragraphs only) ─────────────
        std::string para_text = paragraph_source_text(para.runs);
        bool is_single_font = (para.runs.size() == 1);

        if (cache && is_single_font) {
            TextLayoutCacheKey cache_key = build_cache_key(
                para_text, para.runs[0].font, layout);
            if (auto cached = cache->find(cache_key)) {
                result.paragraphs.push_back(
                    std::const_pointer_cast<TextRunLayout>(cached));
                continue;
            }
        }

        // ── Shape + place each resolved run ────────────────────────
        std::vector<PlacedGlyphRun> placed_runs;
        placed_runs.reserve(para.runs.size());

        for (const auto& run : para.runs) {
            placed_runs.push_back(
                shape_resolved_run(run, engine, layout.tracking));
        }

        // ── Concatenate into one PlacedGlyphRun ────────────────────
        PlacedGlyphRun merged = concatenate_runs(placed_runs);

        // ── Paragraph composition ──────────────────────────────────
        // Start from layout.paragraph as the base (document defaults).
        // Then let para.style override any fields that were explicitly
        // set (non-zero / non-default).  This ensures layout.paragraph
        // serves as the fallback for all fields, not just composer.
        ParagraphStyle comp_style = layout.paragraph;
        if (!(para.style == ParagraphStyle{})) {
            // para.style has customizations — use it entirely.
            comp_style = para.style;
        }

        ParagraphLayout composed = compose_single_line_paragraph(
            merged,
            layout.box.x - comp_style.left_indent - comp_style.right_indent,
            comp_style,
            para_text
        );

        // ── Apply composition to glyph positions ───────────────────
        apply_composition_to_placed(merged, composed);

        // ── Build TextRunLayout ────────────────────────────────────
        auto text_layout = std::make_shared<TextRunLayout>();
        text_layout->source_text = para_text;
        text_layout->font = doc.defaults.font;
        text_layout->font_size = doc.defaults.font.font_size;
        text_layout->placed = std::move(merged);
        // Note: units (TextUnitMap) not built here — callers that need
        // glyph-to-word/line mapping should call build_text_unit_map()
        // separately from the placed run + source_text.
        text_layout->bounds = composed.bounds;
        text_layout->tracking = layout.tracking;
        text_layout->wrap = layout.wrap;
        text_layout->direction = TextDirection::Auto;
        // Line height from the paragraph style or layout default.
        text_layout->line_height = layout.line_height;

        result.paragraphs.push_back(std::move(text_layout));

        if (cache && is_single_font) {
            TextLayoutCacheKey cache_key = build_cache_key(
                para_text, para.runs[0].font, layout);
            cache->store(std::move(cache_key), result.paragraphs.back());
        }
    }

    // ── TEXT-PLY-01: wire spacing_collapse across consecutive paragraphs ──
    apply_spacing_collapse(result, tree, layout);

    return result;
}

} // namespace chronon3d
