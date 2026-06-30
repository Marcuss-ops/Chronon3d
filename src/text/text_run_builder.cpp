// ═══════════════════════════════════════════════════════════════════════════
// text_run_builder.cpp — Full-pipeline text run builder implementation
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/text/glyph_selector.hpp>      // build_text_unit_map
#include <chronon3d/text/single_line_composer.hpp>
#include <chronon3d/text/text_resolver.hpp>
#include <chronon3d/text/text_unit_map.hpp>        // TextUnitMap (used by build_text_unit_map)

#include <spdlog/spdlog.h>

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
// compile_text_layout — single canonical TextRunLayout compiler (Fase 1.1)
//
// INVARIANT: on Ok return, `result.value()->units` is ALWAYS a valid
// TextUnitMap (possibly empty for zero-glyph paragraphs, but never
// uninitialized).  This is the bug-fix for `shape.layout->units` being
// undefined after Scramble / Morph / CrossfadeLayouts transitions
// (verdetto Text, Issue #2).
//
// Pipeline (one paragraph):
//   1. Validate request & services (MalformedLayout / ShapingFailed on bad inputs).
//   2. Resolve document → paragraph tree via resolve_text_run_tree().
//   3. Index into the requested paragraph (MalformedLayout if out of range).
//   4. Empty paragraph → empty layout with empty TextUnitMap (counts all 0).
//   5. Cache lookup (single-font paragraphs only; keys on text + primary_font).
//   6. Shape + place every resolved run; concatenate; compose; apply.
//   7. Build TextRunLayout — `units` is populated UNCONDITIONALLY by
//      `build_text_unit_map(placed, source_text)`.
//   8. Cache store (single-font paragraphs only).
//
// On Err, no TextRunLayout is allocated (caller decides fallback policy,
// e.g. `build_text_run` does spdlog::warn + skip the offending paragraph).
// ═══════════════════════════════════════════════════════════════════════════

Result<std::shared_ptr<TextRunLayout>, TextLayoutError>
compile_text_layout(
    const TextLayoutRequest& request,
    TextCompileServices& services
) {
    // ── Validate pointers ─────────────────────────────────────────
    if (request.doc == nullptr || request.layout == nullptr) {
        return TextLayoutError{
            TextLayoutErrorKind::MalformedLayout,
            "compile_text_layout: request.doc or request.layout is null"
        };
    }
    if (services.engine == nullptr) {
        return TextLayoutError{
            TextLayoutErrorKind::ShapingFailed,
            "compile_text_layout: services.engine is null"
        };
    }

    const TextDocument&   doc    = *request.doc;
    const TextLayoutSpec& layout = *request.layout;
    FontEngine&           engine = *services.engine;

    // ── Resolve document → paragraph tree ───────────────────────────
    if (doc.utf8.empty() && doc.paragraphs.empty()) {
        return TextLayoutError{
            TextLayoutErrorKind::EmptySource,
            "compile_text_layout: empty source text"
        };
    }

    auto tree = resolve_text_run_tree(doc, engine);

    // ── Resolve paragraph index ─────────────────────────────────────
    // The caller supplies the paragraph index they want compiled.
    // build_text_run (the wrapper) iterates 0..tree.paragraphs.size().
    // materialize_text_run_shape uses paragraph index 0.
    //
    // We map the request's `paragraph_index` to the resolved tree's
    // sibling with the same ordinal — `compile_text_layout` does NOT
    // have a `paragraph_index` field on TextLayoutRequest yet (kept
    // minimal); instead callers compile paragraph-by-paragraph by
    // passing a synthesized per-paragraph TextDocument.  When the
    // request's doc IS the full document, we compile the FIRST
    // resolved paragraph (the typical single-paragraph case).  The
    // wrapper iterates.
    //
    // For the unification commit: if `request.doc.paragraphs` has been
    // pre-split, `tree.paragraphs` 1:1 mirrors it; we compile the FIRST
    // (and only) one.  build_text_run passes a fresh `Request` per
    // paragraph via this same contract by setting `paragraph_index`
    // through a doc swap.  For the atomic refactor we operate on the
    // FULL document and use `tree.paragraphs.front()` — this matches
    // what `materialize_text_run_shape` already does for single-para
    // shape contexts.
    if (tree.paragraphs.empty()) {
        return TextLayoutError{
            TextLayoutErrorKind::EmptySource,
            "compile_text_layout: resolved tree has no paragraphs"
        };
    }

    const auto& para = tree.paragraphs.front();

    // ── Empty paragraph (e.g. two consecutive \n) ─────────────────
    if (para.runs.empty()) {
        auto empty_layout = std::make_shared<TextRunLayout>();
        empty_layout->source_text.clear();
        empty_layout->font = doc.defaults.font;
        empty_layout->font_size = doc.defaults.font.font_size;
        empty_layout->bounds = {0.0f, 0.0f};
        empty_layout->line_height = doc.defaults.font.font_size * 1.2f;
        // INVARIANT: empty paragraphs must still return a valid empty
        // TextUnitMap so callers (e.g. selector/animation) see
        // deterministic counts (all zero) instead of undefined state.
        empty_layout->units = build_text_unit_map(PlacedGlyphRun{}, std::string{});
        return std::move(empty_layout);
    }

    // ── MissingFont check (pre-shape) ────────────────────────
    // If ALL resolved runs ended up with empty font_path AND empty
    // font_family after resolve_fallback_fonts(), there is no usable
    // font for this paragraph.  Surface a structured MissingFont error
    // so the caller can decide between (a) requesting a font load,
    // (b) showing a placeholder, or (c) skipping the whole layer.
    //
    // This is checked BEFORE the cache lookup — a cache hit would have
    // produced a glyph anyway, so it can't bypass this guard (cache
    // entries are written from previously-successful compiles, so a
    // cache hit cannot have empty font spec).  Cache miss + no font
    // spec is exactly the failure mode we want to surface.
    {
        bool has_font = false;
        for (const auto& r : para.runs) {
            if (!r.font.font_path.empty() || !r.font.font_family.empty()) {
                has_font = true;
                break;
            }
        }
        if (!has_font) {
            return TextLayoutError{
                TextLayoutErrorKind::MissingFont,
                "compile_text_layout: no usable font resolved for paragraph"
            };
        }
    }

    // ── Cache check (single-font paragraphs only) ─────────────────
    std::string para_text = paragraph_source_text(para.runs);
    bool is_single_font = (para.runs.size() == 1);

    const FontSpec primary_font = request.primary_font.font_path.empty()
        ? doc.defaults.font
        : request.primary_font;

    if (services.cache && is_single_font) {
        TextLayoutCacheKey cache_key = build_cache_key(para_text, primary_font, layout);
        if (auto cached = services.cache->find(cache_key)) {
            return std::const_pointer_cast<TextRunLayout>(cached);
        }
    }

    // ── Shape + place each resolved run ────────────────────────────
    std::vector<PlacedGlyphRun> placed_runs;
    placed_runs.reserve(para.runs.size());
    for (const auto& run : para.runs) {
        placed_runs.push_back(shape_resolved_run(run, engine, layout.tracking));
    }

    // ── Concatenate into one PlacedGlyphRun ────────────────────────
    PlacedGlyphRun merged = concatenate_runs(placed_runs);

    // ── ShapingFailed check (post-shape) ────────────────────
    // After resolving shape for every run in the paragraph, if the
    // merged placed run has zero glyphs AND the paragraph text was
    // non-empty, then HarfBuzz rejected the input even though a
    // font was provided.  Surface this as a structured ShapingFailed
    // error (instead of the previous zero-weight silent fallback
    // that froze animators with total_units=0).
    if (merged.glyphs.empty() && !para_text.empty()) {
        return TextLayoutError{
            TextLayoutErrorKind::ShapingFailed,
            "compile_text_layout: HarfBuzz produced no glyphs for non-empty paragraph"
        };
    }

    // ── Paragraph composition ──────────────────────────────────────
    ParagraphStyle comp_style = layout.paragraph;
    if (!(para.style == ParagraphStyle{})) {
        comp_style = para.style;
    }

    ParagraphLayout composed = compose_single_line_paragraph(
        merged,
        layout.box.x - comp_style.left_indent - comp_style.right_indent,
        comp_style,
        para_text
    );

    // ── Apply composition to glyph positions ───────────────────────
    apply_composition_to_placed(merged, composed);

    // ── Build TextRunLayout ────────────────────────────────────────
    auto text_layout = std::make_shared<TextRunLayout>();
    text_layout->source_text = para_text;
    text_layout->font        = primary_font;
    text_layout->font_size   = primary_font.font_size;
    text_layout->placed      = std::move(merged);
    // ── INVARIANT: `units` populated UNCONDITIONALLY (Fase 1.1 fix) ──
    // Per-frame drivers (Scramble/Morph/CrossfadeLayouts/prewarm) read
    // `shape.layout->units.total_units()` to seed selectors and weights.
    // A missing/empty units graph causes the selector to return weight 0
    // and the animation to silently stop.  Building it here guarantees
    // every returned layout carries a valid TextUnitMap.
    text_layout->units       = build_text_unit_map(text_layout->placed, para_text);
    text_layout->bounds      = composed.bounds;
    text_layout->tracking    = layout.tracking;
    text_layout->wrap        = layout.wrap;
    text_layout->direction   = TextDirection::Auto;
    text_layout->line_height = layout.line_height;

    if (services.cache && is_single_font) {
        TextLayoutCacheKey cache_key = build_cache_key(para_text, primary_font, layout);
        services.cache->store(std::move(cache_key), text_layout);
    }

    return std::move(text_layout);
}

// ═══════════════════════════════════════════════════════════════════════════
// build_text_run — backward-compatible multi-paragraph wrapper
// ═══════════════════════════════════════════════════════════════════════════
//
// Iterates paragraphs in the resolved tree; for each paragraph, builds
// a synthetic single-paragraph TextDocument, calls compile_text_layout,
// and accumulates the result into a TextRunBuildResult.  Preserves the
// external API (TextRunBuildResult) and derives the same per-paragraph
// invariants compile_text_layout guarantees.  Errors from
// compile_text_layout are logged via spdlog::warn and the paragraph is
// skipped (better than a silent empty layout).
//
// This dual-resolve (compile_text_layout resolves the tree, build_text_run
// also resolves it) is a temporary atomicity cost: a follow-up commit
// will thread the resolved tree through both paths.  Cost is one extra
// O(paragraphs) pass per call and is acceptable.

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

    // Resolve once for wrapper-level concerns (paragraph iteration
    // count + spacing_collapse).  compile_text_layout re-resolves
    // internally; that duplication is documented above.
    auto tree = resolve_text_run_tree(doc, engine);

    TextCompileServices services{&engine, cache};

    const FontSpec primary_font = doc.defaults.font;

    for (std::size_t i = 0; i < tree.paragraphs.size(); ++i) {
        // Synthesize a per-paragraph TextDocument so compile_text_layout
        // compiles the i-th tree paragraph.  The source text for that
        // paragraph is the concatenation of its resolved runs' text,
        // which mirrors the previous in-loop behavior exactly.
        TextDocument para_doc;
        para_doc.utf8            = paragraph_source_text(tree.paragraphs[i].runs);
        para_doc.defaults.font   = primary_font;
        para_doc.split_paragraphs();

        TextLayoutRequest request{
            &para_doc,
            &layout,
            primary_font,
        };
        auto compiled = compile_text_layout(request, services);
        if (!compiled) {
            spdlog::warn(
                "build_text_run: paragraph {} skipped — {}",
                i, compiled.error().message);
            continue;
        }
        result.paragraphs.push_back(
            std::const_pointer_cast<TextRunLayout>(compiled.value()));
    }

    // ── TEXT-PLY-01: wire spacing_collapse across consecutive paragraphs ──
    apply_spacing_collapse(result, tree, layout);

    return result;
}

} // namespace chronon3d
