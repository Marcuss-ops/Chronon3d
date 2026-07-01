// ═══════════════════════════════════════════════════════════════════════════
// text_run_builder.cpp — Full-pipeline text run builder implementation
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/text/glyph_selector.hpp>      // build_text_unit_map
#include <chronon3d/text/single_line_composer.hpp>
#include <chronon3d/text/text_resolver.hpp>
#include <chronon3d/text/text_unit_map.hpp>        // TextUnitMap (used by build_text_unit_map)

// TICKET-092: INTERNAL accumulator types (cat-3 freeze — NOT in include/).
// Provides CompiledParagraphResult / TextDocumentCompileResult /
// compile_text_document().  See header doc-comment for the per-paragraph
// failure preservation contract.
#include "text_document_compile_result.hpp"

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
///
/// TICKET-103a — the 3 new TextLayoutRequest fields (direction, language,
/// features) are now honored by the cache key directly.  The previous
/// overrides that forced `direction = Auto` and `language.clear()` are
/// GONE — bidi / BCP-47 / OT-shaping-features parity is what the cache
/// key discriminates, so LTR vs RTL inputs MUST collide on different
/// keys (graceful LTR-only behavior was a workaround-era cost; bidi
/// callers now share a memory footprint with their actual cache contents).
[[nodiscard]] TextLayoutCacheKey build_cache_key(
    const std::string& full_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout,
    TextDirection direction,
    const std::string& language,
    const std::string& features
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
    key.direction   = direction;             // TICKET-103a — was: TextDirection::Auto (per-run bidi override)
    key.language    = language;              // TICKET-103a — was: cleared string
    key.features    = features;              // TICKET-103a — new field
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

// NOTE: `paragraph_is_multi_font` was hoisted to the public header
// `include/chronon3d/text/text_resolver.hpp` (CR#5 closure) where the
// font hierarchy is canonical.  Both call sites below route through
// that free helper, which uses std::tie-based font_spec equality so
// future FontSpec fields are locally extendable.  The local copy that
// previously lived here has been removed; the helper now lives next
// to where the font-resolver data is defined.

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

// TICKET-092: spacing_collapse rewritten to use `source_index` as the
// bridge between the accumulator vector and the resolved tree.  The
// previous implementation assumed `result.paragraphs[i] == tree.paragraphs[i]`
// (no per-paragraph failure), which broke the post-process pass the moment
// any paragraph returned Err: the loop would index into the WRONG tree
// paragraph (using vector offset instead of source ordinal) and the bounds
// adjustment would target a paragraph unrelated to the one being spaced.
//
// Contract: this pass is a no-op between two paragraphs that are not
// BOTH Ok.  When paragraph `i` fails to compile, the spacing_collapse
// chain is BROKEN at `i`: the loop sees (Ok at i-1, Err at i) and skips,
// then (Err at i, Ok at i+1) and skips again.  This is the conservative
// behaviour — a missing middle paragraph's space_after / space_before
// cannot be reasoned about, so we leave the Ok siblings at their
// pre-collapse bounds.y.  Callers that need a specific gap-recovery
// policy (e.g. collapse across the missing paragraph) should add a
// follow-up patch; the cat-3 freeze keeps the contract minimal here.
inline void apply_spacing_collapse(
    text_internal::TextDocumentCompileResult& compile_result,
    const ResolvedTextTree& tree,
    const TextLayoutSpec& layout
) {
    using ParagraphStyle = chronon3d::ParagraphStyle;
    using ParagraphSpacingCollapse = chronon3d::ParagraphSpacingCollapse;

    if (compile_result.paragraphs.size() < 2) return;
    if (tree.paragraphs.size() < 2) return;

    for (std::size_t i = 1; i < compile_result.paragraphs.size(); ++i) {
        auto& prev_entry = compile_result.paragraphs[i - 1];
        auto& cur_entry  = compile_result.paragraphs[i];

        // Skip pairs that are not BOTH Ok (Err paragraphs have no
        // bounds to adjust and break the spacing chain — see contract).
        if (!prev_entry.result || !cur_entry.result) continue;

        // Defensive: source_index must be valid for the resolved tree.
        if (prev_entry.source_index >= tree.paragraphs.size()) continue;
        if (cur_entry.source_index  >= tree.paragraphs.size()) continue;

        // TICKET-092: bridge via source_index — the resolved tree's
        // paragraphs are indexed by the ORIGINAL doc ordinal, not by
        // the accumulator's vector position.
        const auto& prev_tree_para = tree.paragraphs[prev_entry.source_index];
        const auto& cur_tree_para  = tree.paragraphs[cur_entry.source_index];

        ParagraphStyle prev_style;
        if (!(prev_tree_para.style == ParagraphStyle{})) {
            prev_style = prev_tree_para.style;
        } else {
            prev_style = layout.paragraph;
        }

        ParagraphStyle cur_style;
        if (!(cur_tree_para.style == ParagraphStyle{})) {
            cur_style = cur_tree_para.style;
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
            prev_entry.result.value()->bounds.y -= prev_after;
        }
        // Step 2: replace cur.space_before with merged_gap (delta shift).
        const float delta = merged_gap - cur_before;
        if (delta != 0.0f) {
            cur_entry.result.value()->bounds.y += delta;
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
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved
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

    // P1-6 — When `pre_resolved` is non-null, bind `tree` to the
    // caller's tree by const reference (no copy).  When null, resolve
    // internally into `local_storage` so the rest of the function
    // uses one uniform `const ResolvedTextTree&` binding.  The
    // comma-operator pattern in the nullptr branch extends the
    // returned prvalue lifetime to the scope of `local_storage`,
    // avoiding a copy of the (potentially large) resolved tree on
    // the new doc-compile path.
    ResolvedTextTree local_storage{};
    const ResolvedTextTree& tree = (pre_resolved != nullptr)
        ? *pre_resolved
        : (local_storage = resolve_text_run_tree(doc, engine), local_storage);

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
    // TICKET-101: index into the requested paragraph of the ORIGINAL
    // document.  build_text_run iterates 0..N passing the i-th index;
    // materialize_text_run_shape passes 0.  Bounds-checked against the
    // resolved tree's paragraph count (defensive: caller may have
    // skipped split_paragraphs() and the doc index may be stale).
    if (request.paragraph_index >= tree.paragraphs.size()) {
        return TextLayoutError{
            TextLayoutErrorKind::MalformedLayout,
            "compile_text_layout: request.paragraph_index out of range"
        };
    }

    const auto& para = tree.paragraphs[request.paragraph_index];

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

    // ── Multi-font paragraph compile: ADOPTED (Phase 1.4 verdict Issue #3) ──
    // The Fase 1.5 stab path refused multi-font (Err UnsupportedMultiFontRun).
    // Phase 1.4 replaces it with the additive path: compile succeeds and
    // `font_spans` (populated below) describes the per-glyph font identity
    // so the renderer can switch BLFont at span boundaries.  No paragraph
    // is ever rejected on font identity grounds after this commit.  See
    // verdict Issue #3 closure; tests/text/test_compile_text_layout_errors.cpp
    // retires the corresponding assertion.

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
        // Single-font paragraph: try the cache before re-shaping.
        // TICKET-103a — direction/language/features now propagated from
        // request so LTR vs RTL / ar vs en / kern=1 vs kern=0 any of these
        // produce a different cache_key and bypass the cached entry,
        // forcing a re-shape for the new shaping parameters.
        TextLayoutCacheKey cache_key = build_cache_key(
            para_text, primary_font, layout,
            request.direction, request.language, request.features);
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
    // ── Phase 1.4: Populate font_spans for the renderer ──
    // Compute per-run glyph-index ranges BEFORE concatenation by walking
    // the shaped `placed_runs` vector.  After concatenation the glyphs
    // from `placed_runs[i]` occupy indices [run_glyph_begin[i],
    // run_glyph_begin[i] + placed_runs[i].glyphs.size()) in the merged
    // run.  Emit one ShapedFontSpan per run with the corresponding
    // FontIdentity (FontSpec minus font_size).  Empty runs (no shaped
    // glyphs) are skipped — a run with nothing to render would
    // otherwise leave a `[N, N)` span in the list.
    {
        std::uint32_t glyph_cursor = 0;
        for (std::size_t ri = 0; ri < placed_runs.size(); ++ri) {
            const std::uint32_t begin = glyph_cursor;
            const std::uint32_t end   = glyph_cursor +
                static_cast<std::uint32_t>(placed_runs[ri].glyphs.size());
            glyph_cursor = end;
            if (begin == end) continue;          // skip empty runs
            if (ri >= para.runs.size()) break;    // defensive bound
            ShapedFontSpan span;
            span.font        = font_identity_of(para.runs[ri].font);
            span.glyph_begin = begin;
            span.glyph_end   = end;
            text_layout->font_spans.push_back(std::move(span));
        }
    }

    // ── PHASE 1.4 debug signal ──
    // A multi-font input whose shaping produced zero glyphs has collapsed
    // font_spans to empty AND the renderer treats it as an empty paragraph
    // (no glyphs to draw).  Without this warn, a CI box missing the alternate
    // family would silently emit a zero-frame artefact with no diagnostic.
    // Surface it so the regression is detected by logs + the run-time counters.
    if (text_layout->font_spans.empty() && para.runs.size() > 1) {
        spdlog::warn(
            "compile_text_layout: paragraph resolved into {} ResolvedTextRun(s) "
            "but font_spans is empty after shaping (likely missing system fonts "
            "for one or more identities).  Multi-font input renders as an empty "
            "paragraph — verify fonts are installed on CI.",
            para.runs.size());
    }

    text_layout->bounds      = composed.bounds;
    text_layout->tracking    = layout.tracking;
    text_layout->wrap        = layout.wrap;
    // TICKET-101: direction + language preservation from comp_style
    // (paragraph style override or layout spec default).  Previously
    // direction was hardcoded to TextDirection::Auto and language was
    // never set, silently dropping per-paragraph direction/language info
    // that lived on the ORIGINAL doc.  Now both fields flow through
    // comp_style (which is para.style if non-default, else layout.paragraph).
    text_layout->direction   = comp_style.direction;
    text_layout->language    = comp_style.language;
    // line_height: prefer comp_style override (para.style.line_height
    // when set), fall back to layout.line_height when comp_style uses
    // the default 0.0f sentinel.
    text_layout->line_height = comp_style.line_height > 0.0f
        ? comp_style.line_height
        : layout.line_height;

    if (services.cache && is_single_font) {
        TextLayoutCacheKey cache_key = build_cache_key(
            para_text, primary_font, layout,
            request.direction, request.language, request.features);
        services.cache->store(std::move(cache_key), text_layout);
    }

    return std::move(text_layout);
}

// ═══════════════════════════════════════════════════════════════════════════
// build_text_run — backward-compatible multi-paragraph wrapper
// ═══════════════════════════════════════════════════════════════════════════
//
// TICKET-092 refactor: this wrapper is now a THIN filter around
// `text_internal::compile_text_document` (the canonical accumulator).
// The accumulator carries Ok AND Err per-paragraph results with a
// `source_index` bridge, then runs `apply_spacing_collapse` with the
// bridge.  This wrapper only converts the accumulator into the legacy
// `TextRunBuildResult` shape (Ok-only `paragraphs` vector) and emits
// the per-paragraph diagnostic that the previous implementation
// inlined as `spdlog::warn + goto next_paragraph`.
//
// Why a thin wrapper (rather than keeping the loop here)?
//   - The accumulator pattern (carry Ok+Err with source_index) is the
//     primary contract being added.  Inlining it here would re-introduce
//     the spdlog::warn+skip pattern that TICKET-092 explicitly removes.
//   - The accumulator is the natural input to `apply_spacing_collapse`
//     because both share the `source_index` bridge.  Keeping the loop
//     in `build_text_run` and calling spacing_collapse on the filtered
//     Ok-only result would re-introduce the result[i] == tree[i] bug.
//   - Tests that need to assert per-paragraph failure (the test added
//     by TICKET-092) call `compile_text_document` directly to inspect
//     the full accumulator; build_text_run is no longer the only
//     compile-pipeline entry point.
//
// The pre-loop `auto tree = resolve_text_run_tree(...)` is no longer
// needed here — `compile_text_document` resolves internally.

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

    // Delegate to the canonical accumulator.  After the call, `result`
    // (TextRunBuildResult) is built by filtering Ok entries and
    // emitting the per-paragraph diagnostic that callers used to see
    // via the inlined spdlog::warn + goto next_paragraph.
    auto compile_result = text_internal::compile_text_document(
        doc, engine, layout, cache);

    // Propagate completeness from the internal accumulator to the
    // public result so callers can distinguish "all paragraphs ok"
    // from "some paragraphs failed" (audit P0 #5).
    result.complete = compile_result.complete;

    for (auto& entry : compile_result.paragraphs) {
        if (entry.result) {
            result.paragraphs.push_back(
                std::const_pointer_cast<TextRunLayout>(entry.result.value()));
        } else {
            // Per-paragraph diagnostic — preserved from the previous
            // implementation (spdlog::warn + skip) so log-scrapers
            // looking for `paragraph N skipped` keep working.  The
            // accumulator no longer needs the diagnostic; only this
            // public wrapper does, for backward-compat.
            spdlog::warn(
                "build_text_run: paragraph {} skipped — {}",
                entry.source_index, entry.result.error().message);
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// compile_text_document — INTERNAL canonical accumulator (TICKET-092)
// ═══════════════════════════════════════════════════════════════════════════
//
// The single source of truth for per-paragraph compile outcomes.  Walks
// the resolved tree once, runs the multi-font pre-check (TICKET-101
// verdict Issue #3 closure at the public surface), invokes
// `compile_text_layout` per paragraph, and accumulates Ok AND Err
// results into a `TextDocumentCompileResult`.  Then calls
// `apply_spacing_collapse` with the `source_index` bridge so the
// spacing pass uses the ORIGINAL doc ordinal, not the accumulator's
// vector position.
//
// This function is the sole reason `apply_spacing_collapse` and
// `build_text_run` can be refactored to drop the warn+skip pattern:
// every per-paragraph outcome is preserved (Ok → value, Err → error
// in the Result), every paragraph has a `source_index` ordinal, and
// the post-process passes can pick the right tree paragraph without
// re-deriving an offset.
namespace text_internal {

TextDocumentCompileResult compile_text_document(
    const TextDocument& doc,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    TextLayoutCache* cache
) {
    TextDocumentCompileResult result;
    result.complete = true;  // optimistic; flipped to false on any Err

    if (doc.utf8.empty() && doc.paragraphs.empty()) {
        return result;  // empty doc → 0 paragraphs, complete=true
    }

    // Resolve once for both iteration + spacing_collapse.
    auto tree = resolve_text_run_tree(doc, engine);

    TextCompileServices services{&engine, cache};

    const FontSpec primary_font = doc.defaults.font;

    for (std::size_t i = 0; i < tree.paragraphs.size(); ++i) {
        CompiledParagraphResult entry;
        entry.source_index = i;

        // ── TICKET-101: multi-font pre-check (verdict Issue #3) ────────
        // The wrapper enforces strict single-font policy.  Direct
        // compile_text_layout callers retain the Phase 1.4 additive
        // font_spans path.  When divergence is detected the paragraph
        // is accumulated as Err(UnsupportedMultiFontRun) — the
        // accumulator carries it through apply_spacing_collapse and
        // lets callers identify the failing paragraph via source_index.
        //
        // P1-7 — multi-font divergence is now detected via the
        // canonical `font_identity_of()` projection (defined in
        // `<chronon3d/text/font_engine.hpp>`).  This replaces the
        // previous hand-rolled 4-field compare on
        // font_path/font_family/font_weight/font_style — the inline
        // `FontIdentity::operator!=` is the same contract
        // `compile_text_layout`'s font_spans path uses to label per
        // glyph ranges, so this site is now in SYNC with the
        // span-emission path (one canonical equality used
        // everywhere).  font_size is intentionally DROPPED by the
        // helper (size is a layout concern, not a font identity —
        // superscript / drop-cap / size-varying typing use the same
        // font at different sizes).
        // P1-7 (review-mandated readability nit): hoist a local
        // reference to `tree.paragraphs[i].runs` so the triple-deref
        // collapses to one.  `std::any_of` replaces the manual
        // break-on-divergent loop and matches the C++20 idiom used
        // elsewhere in the text pipeline.
        const auto& para_runs = tree.paragraphs[i].runs;
        if (para_runs.size() > 1) {
            const FontIdentity base_id =
                font_identity_of(para_runs.front().font);
            const bool divergent = std::any_of(
                para_runs.begin() + 1, para_runs.end(),
                [&base_id](const ResolvedTextRun& r) {
                    return font_identity_of(r.font) != base_id;
                });
            if (divergent) {
                entry.result = Err(TextLayoutError{
                    TextLayoutErrorKind::UnsupportedMultiFontRun,
                    "compile_text_document: paragraph " + std::to_string(i)
                    + " is multi-font (span font differs from run 0)"
                });
                result.paragraphs.push_back(std::move(entry));
                result.complete = false;
                continue;
            }
        }

        // ── Compile via the canonical compiler (TICKET-101 contract) ──
        TextLayoutRequest request{
            &doc,
            &layout,
            primary_font,
            i,
        };
        // P1-6 — pass &tree (the doc-level resolved tree resolved once
        // above) so compile_text_layout skips its internal
        // resolve_text_run_tree() call.  N+1 -> 1 resolution per
        // document compile (the original cost was N+1 for an
        // N-paragraph doc).
        entry.result = compile_text_layout(request, services, &tree);
        if (!entry.result) {
            result.complete = false;
        }
        result.paragraphs.push_back(std::move(entry));
    }

    // ── TEXT-PLY-01: wire spacing_collapse across consecutive paragraphs ──
    // TICKET-092: uses `source_index` as the bridge into tree.paragraphs.
    apply_spacing_collapse(result, tree, layout);

    return result;
}

} // namespace text_internal

} // namespace chronon3d
