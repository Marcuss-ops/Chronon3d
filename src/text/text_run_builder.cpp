// SPDX-License-Identifier: MIT
//
// text_run_builder.cpp — SLIM ORCHESTRATOR (M1.5#5).
//
// Pre-M1.5#5 this file hosted a 830-LOC monolith housing:
//   - `compile_text_layout` (the public canonical compiler)
//   - `build_text_run` (the public wrapper)
//   - `text_internal::compile_text_document` (the canonical accumulator,
//     TICKET-092 closure)
//   - `apply_spacing_collapse` (post-process, TICKET-092 contract)
//   - 4 anonymous-namespace helper bodies (concatenate_runs,
//     build_cache_key, apply_composition_to_placed,
//     paragraph_source_text)
//
// After M1.5#5 the four verbatim body moves live in
// `src/text/compiler/`:
//   - text_compile_validation.cpp    : validate_layout_request
//                                     + check_paragraph_has_font
//   - text_run_shaping.cpp          : build_paragraph_text, build_cache_key,
//                                     shape_paragraph_runs, apply_failure_policy,
//                                     validate_concatenated_run
//   - text_run_composition.cpp      : compose_paragraph,
//                                     apply_composition_to_placed,
//                                     concatenate_runs
//   - text_font_span_builder.cpp    : populate_font_spans
//
// What stays in this slim orchestrator:
//   - `compile_text_layout` — the public single-paragraph canonical
//     compiler, now a THIN linear pipeline of 11 stage calls
//     (3 validator/shaper helpers + 2 shaping result handler +
//      3 composition helpers + 1 font-spans helper + 2 cache-key
//      constructions + 1 resolve-tree plumbing + 1 ParagraphStyle
//      resolver).
//   - `build_text_run` — the public backward-compatible multi-paragraph
//     wrapper (delegates to text_internal::compile_text_document).
//   - `text_internal::compile_text_document` — the canonical multi-
//     paragraph accumulator with multi-font pre-check +
//     apply_spacing_collapse post-process.
//   - `apply_spacing_collapse` — TICKET-092 closure contract; bridges
//     accumulator vector to the resolved tree via `source_index`.
//
// Pipeline (canonical 7-stage, mapped to helpers in src/text/compiler/):
//   1. validate request                          — text_compile_validation
//   2. resolve document                          — orchestrator (pre_resolved)
//   3. shape every run                           — text_run_shaping (stage 3)
//   4. apply failure policy                      — text_run_shaping (stage 4)
//   5. compose paragraph                         — text_run_composition (5+6)
//   6. build font spans                          — text_font_span_builder
//   7. store immutable layout                    — orchestrator (cache store)
//
// Public API surface (`include/chronon3d/text/text_run_builder.hpp`):
// compile_text_layout + build_text_run + Result types.  Unchanged.

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/text/glyph_selector.hpp>      // build_text_unit_map
#include <chronon3d/text/single_line_composer.hpp>
#include <chronon3d/text/text_resolver.hpp>

#include "compiler/text_compile_internal.hpp"      // M1.5#5 — 11 stage helpers
#include "text_document_compile_result.hpp"        // TICKET-092 INTERNAL accumulator

#include <spdlog/spdlog.h>

#include <algorithm>

namespace chronon3d {

namespace tci = text_internal::compile;

// ═══════════════════════════════════════════════════════════════════════════
// compile_text_layout — single canonical TextRunLayout compiler
//                       (slim orchestrator after M1.5#5)
// ═══════════════════════════════════════════════════════════════════════════
//
// INVARIANT: on Ok return, `result.value()->units` is ALWAYS a valid
// TextUnitMap (possibly empty for zero-glyph paragraphs, but never
// uninitialized).  This is the bug-fix for `shape.layout->units` being
// undefined after Scramble / Morph / CrossfadeLayouts transitions
// (verdetto Text, Issue #2).
//
// Order rationale (verbatim from the thinker verdict on A2):
//   - cache store MUST happen AFTER populate_font_spans (otherwise
//     cache hits return a TextRunLayout with empty font_spans, which
//     would break the renderer on cache hits).
//   - build_text_unit_map(placed, para_text) MUST happen AFTER
//     apply_composition_to_placed so the grapheme bounds take the
//     composed x/y positions (line breaks, justification padding)
//     into account, NOT the raw baseline coordinates.

Result<std::shared_ptr<TextRunLayout>, TextLayoutError>
compile_text_layout(
    const TextLayoutRequest& request,
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved
) {
    // ── Stage 1 — Validate request + services ──────────────────────
    auto validated = tci::validate_layout_request(request, services);
    if (!validated) return validated.error();
    auto& refs = validated.value();
    const TextDocument&   doc    = refs.doc;
    const TextLayoutSpec& layout = refs.layout;
    FontEngine&           engine = refs.engine;

    // ── Stage 2 — Resolve document → paragraph tree ────────────────
    // Orchestrator-level concern: pre_resolved or resolve locally.
    // P1-6 — when pre_resolved is non-null, bind `tree` to the
    // caller's tree by const reference (no copy).  When null, resolve
    // internally into `local_storage`.  The comma-operator pattern
    // extends the prvalue lifetime to the scope of `local_storage`,
    // avoiding a copy of the (potentially large) resolved tree on
    // the new doc-compile path.
    ResolvedTextTree local_storage{};
    const ResolvedTextTree& tree = (pre_resolved != nullptr)
        ? *pre_resolved
        : (local_storage = resolve_text_run_tree(doc, engine), local_storage);

    // ── Tree guards (orchestrator-level) ──────────────────────────
    if (tree.paragraphs.empty()) {
        return TextLayoutError{
            TextLayoutErrorKind::EmptySource,
            "compile_text_layout: resolved tree has no paragraphs"
        };
    }
    if (request.paragraph_index >= tree.paragraphs.size()) {
        return TextLayoutError{
            TextLayoutErrorKind::MalformedLayout,
            "compile_text_layout: request.paragraph_index out of range"
        };
    }

    const auto& para = tree.paragraphs[request.paragraph_index];

    // ── Empty paragraph short-circuit (Fase 1.1 invariant) ─────────
    // Two consecutive \n produce a paragraph with no runs.  Return
    // an empty TextRunLayout with a valid empty TextUnitMap so
    // callers (e.g. selector/animation) see deterministic counts
    // (all zero) instead of undefined state.
    if (para.runs.empty()) {
        auto empty_layout = std::make_shared<TextRunLayout>();
        empty_layout->source_text.clear();
        empty_layout->font = doc.defaults.font;
        empty_layout->font_size = doc.defaults.font.font_size;
        empty_layout->bounds = {0.0f, 0.0f};
        empty_layout->line_height = doc.defaults.font.font_size * 1.2f;
        empty_layout->units = build_text_unit_map(PlacedGlyphRun{}, std::string{});
        return std::move(empty_layout);
    }

    // ── Orchestrator-side: primary_font + is_single_font ───────────
    const FontSpec primary_font = request.primary_font.font_path.empty()
        ? doc.defaults.font
        : request.primary_font;
    const bool is_single_font = (para.runs.size() == 1);

    // ── Stage 2.5 — MissingFont pre-check (delegate to shaping) ───
    if (!tci::check_paragraph_has_font(para.runs)) {
        return TextLayoutError{
            TextLayoutErrorKind::MissingFont,
            "compile_text_layout: no usable font resolved for paragraph"
        };
    }

    // ── Stage 2b — Build paragraph source text (delegate) ──────────
    std::string para_text = tci::build_paragraph_text(para.runs);

    // ── Stage 2c — Cache lookup (single-font paragraphs only) ──────
    // TICKET-103a — direction/language/features now propagated from
    // the request so LTR vs RTL / ar vs en / kern=1 vs kern=0 any of
    // these produce a different cache_key and bypass the cached
    // entry, forcing a re-shape for the new shaping parameters.
    if (services.cache && is_single_font) {
        TextLayoutCacheKey cache_key = tci::build_cache_key(
            para_text, primary_font, layout,
            request.direction, request.language, request.features);
        if (auto cached = services.cache->find(cache_key)) {
            return std::const_pointer_cast<TextRunLayout>(cached);
        }
    }

    // ── Stage 3 — Shape every run (delegate to shaping) ───────────
    auto per_run_results = tci::shape_paragraph_runs(
        para.runs, engine, layout.tracking);

    // ── Stage 4 — Apply ShapingFailurePolicy (delegate to shaping) ─
    auto placed_or_err = tci::apply_failure_policy(
        std::move(per_run_results), request.shaping_failure_policy);
    if (!placed_or_err) return placed_or_err.error();
    std::vector<PlacedGlyphRun> placed_runs = std::move(placed_or_err.value());

    // ── Stage 6.5 — Concatenate (delegate to composition) ──────────
    PlacedGlyphRun merged = tci::concatenate_runs(placed_runs);

    // ── Stage 4.5 — All-run guard (delegate to shaping) ────────────
    if (auto check = tci::validate_concatenated_run(merged, para_text); !check) {
        return check.error();
    }

    // ── Stage 5 — Compose paragraph (orchestrator-side: pick style)
    // The orchestrator picks comp_style (para.style if non-default,
    // else layout.paragraph) and feeds the box-width adjustment, then
    // delegates the actual composition call.
    ParagraphStyle comp_style = layout.paragraph;
    if (!(para.style == ParagraphStyle{})) {
        comp_style = para.style;
    }

    ParagraphLayout composed = tci::compose_paragraph(
        merged,
        layout.box.x - comp_style.left_indent - comp_style.right_indent,
        comp_style,
        para_text
    );

    // ── Stage 6 — Apply composition to placed (delegate) ───────────
    tci::apply_composition_to_placed(merged, composed);

    // ── Build initial TextRunLayout (orchestrator-side assembly) ───
    auto text_layout = std::make_shared<TextRunLayout>();
    text_layout->source_text = para_text;
    text_layout->font        = primary_font;
    text_layout->font_size   = primary_font.font_size;
    text_layout->placed      = std::move(merged);
    // INVARIANT Fase 1.1: units populated AFTER apply_composition_to_placed.
    text_layout->units       = build_text_unit_map(text_layout->placed, para_text);

    // ── Stage 7 — Populate font_spans (delegate to font_span_builder;
    //              BEFORE cache store per thinker verdict A2) ───────
    tci::populate_font_spans(*text_layout, placed_runs, para.runs);

    // ── Orchestrator-side: assemble remaining TextRunLayout fields ─
    text_layout->bounds      = composed.bounds;
    text_layout->tracking    = layout.tracking;
    text_layout->wrap        = layout.wrap;
    text_layout->direction   = comp_style.direction;
    text_layout->language    = comp_style.language;
    text_layout->line_height = layout.line_height;

    // ── Cache store (AFTER populate_font_spans) ────────────────────
    if (services.cache && is_single_font) {
        TextLayoutCacheKey cache_key = tci::build_cache_key(
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

    auto compile_result = text_internal::compile_text_document(
        doc, engine, layout, cache);

    result.complete = compile_result.complete;

    for (auto& entry : compile_result.paragraphs) {
        if (entry.result) {
            result.paragraphs.push_back(
                std::const_pointer_cast<TextRunLayout>(entry.result.value()));
        } else {
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
// Unchanged after M1.5#5.  Lives close to the apply_spacing_collapse
// post-process because both share the `source_index` bridge.
//
// Walks the resolved tree once, runs the multi-font pre-check (TICKET-101
// verdict Issue #3 closure at the public surface), invokes
// `compile_text_layout` per paragraph, and accumulates Ok AND Err
// results into a `TextDocumentCompileResult`.  Then calls
// `apply_spacing_collapse` with the `source_index` bridge so the
// spacing pass uses the ORIGINAL doc ordinal, not the accumulator's
// vector position.
namespace text_internal {

// Forward-declaration of apply_spacing_collapse: defined below
// compile_text_document but called from inside compile_text_document's
// body.  C++ requires free non-template functions to be DECLARED
// (or defined) BEFORE their first use site within the same TU —
// class member functions defer via two-phase lookup, but free
// functions in a namespace don't.  The forward declaration here
// satisfies the lookup; the definition matches below (same
// signature).  TICKET-092 closure contract.
void apply_spacing_collapse(
    TextDocumentCompileResult& compile_result,
    const ResolvedTextTree& tree,
    const TextLayoutSpec& layout
);

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
        // ── TICKET-101: multi-font pre-check (verdict Issue #3) ────
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
                result.paragraphs.push_back(CompiledParagraphResult{
                    i,
                    TextLayoutError{
                        TextLayoutErrorKind::UnsupportedMultiFontRun,
                        "compile_text_document: paragraph " + std::to_string(i)
                        + " is multi-font (span font differs from run 0)"
                    }
                });
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
        auto layout_res = compile_text_layout(request, services, &tree);
        if (!layout_res) {
            result.complete = false;
        }
        result.paragraphs.push_back(CompiledParagraphResult{
            i,
            std::move(layout_res)
        });
    }

    // ── TEXT-PLY-01: wire spacing_collapse across consecutive paragraphs ──
    // TICKET-092: uses `source_index` as the bridge into tree.paragraphs.
    apply_spacing_collapse(result, tree, layout);

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// apply_spacing_collapse — TICKET-092 closure contract (post-process pass)
// ═══════════════════════════════════════════════════════════════════════════
//
// Stays in the slim orchestrator because it shares the `source_index`
// bridge with compile_text_document.  The post-process walks the
// resolved tree's paragraphs by ORIGINAL DOC ORDINAL (not the
// accumulator's vector position) and adjusts the Ok paragraphs'
// bounds.y to reflect the ParagraphSpacingCollapse rule (Add /
// Maximum).
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
//
// Lives in `namespace text_internal` (TICKET-092 lineage) so it's visible
// from compile_text_document by the canonical 2-segment qualification.
// Public API consumers do NOT see this symbol — it stays in
// `src/text/` (NOT in `include/chronon3d/`), per AGENTS.md v0.1 cat-3
// freeze-compliant scope.
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

        if (!prev_entry.result || !cur_entry.result) continue;

        if (prev_entry.source_index >= tree.paragraphs.size()) continue;
        if (cur_entry.source_index  >= tree.paragraphs.size()) continue;

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

        if (prev_after > 0.0f) {
            prev_entry.result.value()->bounds.y -= prev_after;
        }
        const float delta = merged_gap - cur_before;
        if (delta != 0.0f) {
            cur_entry.result.value()->bounds.y += delta;
        }
    }
}

} // namespace text_internal

} // namespace chronon3d
