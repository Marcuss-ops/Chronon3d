// SPDX-License-Identifier: MIT
//
// text_run_driver.cpp — Per-frame driver (M1.5#2 ORCHESTRATOR).
//
// Splits into single-responsibility helpers under src/text/driver/:
//   * text_state_sampler    — select_target_text / is_in_dissolve_gap
//   * text_font_state       — compute_effective_font (carries P0-2 fix)
//   * text_layout_rebuild   — EffectiveTextState projection + fast-path
//                             match + build_text_run + DissolveLayouts
//                             lifecycle + prewarm
// This file keeps:
//   - the cheap-path animator-stack evaluation that isn't about
//     layout (and thus doesn't fit the rebuild module);
//   - the dissolve_glyphs animator mirror (extends the cheap-path to
//     the outgoing slot, only meaningful inside the gap);
//   - the three public free-function entry points
//     (update/apply/prewarm).  Each of those is now a thin wrapper
//     that delegates to the helpers.
//
// Collaboration rules (M1.5#2 white-paper):
//   1. `update_text_run_shape_per_frame` is a CHEAP path — only
//      animator-stack evaluation.  Layout swap happens lazily via
//      `apply_active_state_to_text_run_shape` (see Phase A6 contract).
//   2. `apply_active_state_to_text_run_shape` calls:
//        a) text_state_sampler::select_target_text
//        b) text_font_state::compute_effective_font
//        c) text_layout_rebuild::target_effective_state
//        d) compare against text_layout_rebuild::current_effective_state
//           → fast-path hit (return false) OR
//        e) text_layout_rebuild::rebuild_active_side (swap layout + glyphs)
//        f) text_layout_rebuild::rebuild_dissolve_slot (PR 11 lifecycle)
//   3. `prewarm_text_run_layout_for_frame` calls:
//        a) text_state_sampler::select_target_text
//        b) text_font_state::compute_effective_font_for_prewarm
//        c) text_layout_rebuild::prewarm_for_frame (writes to cache, no
//           shape mutation)

#include <chronon3d/text/text_run_driver.hpp>

#include "driver/text_state_sampler.hpp"
#include "driver/text_font_state.hpp"
#include "driver/text_layout_rebuild.hpp"

#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/animation/text_pre_shaping.hpp>
#include <chronon3d/text/font_engine.hpp>  // P0-2 — font_layout_identity_of

namespace chronon3d {

// ═══════════════════════════════════════════════════════════
// update_text_run_shape_per_frame — cheap-path orchestrator
// ═══════════════════════════════════════════════════════════

void update_text_run_shape_per_frame(
    TextRunShape& shape,
    SampleTime time,
    TextLayoutCache* /*cache*/ /*M1.5#2: cache flows via shape.cache or compute path */
) {
    // ── Guard: static-shape fast path (layout nullptr is no-op) ─────
    if (!shape.layout) return;

    // ── PR 9 — re-sample AnimatedTextDocument first, then animators ──
    if (shape.animated_doc && shape.engine != nullptr) {
        const ActiveTextState state =
            shape.animated_doc->sample_at(time.integral_frame());
        if (state.active != nullptr) {
            // Slow-path delegate: rebuild layout when needed, mirror
            // crossfade slots when in the gap, return value discarded.
            // Cache-key refresher (hash_text_run_shape frame overload)
            // already captures the post-apply layout bytes.
            (void)apply_active_state_to_text_run_shape(
                shape, state, *shape.engine, shape.layout_spec);
        }
    }

    // ── Empty animators short-circuit: just (re)seed glyphs. ─────────
    if (shape.animators.empty()) {
        if (shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
            shape.glyphs = make_initial_glyph_states(shape.layout->placed);
        }
        return;
    }

    // ── Pre-shaping phase: rebuild layout when offset_source differs. ─
    if (has_pre_shaping_properties(shape.animators)) {
        std::string offset_source = evaluate_pre_shaping_source(
            shape.animators, shape.layout->source_text);

        if (offset_source != shape.layout->source_text) {
            TextDocument td;
            td.utf8 = offset_source;
            if (shape.layout) {
                td.defaults.font = shape.layout->font;
            }
            td.split_paragraphs();

            auto* cache = shape.cache;  // Fase B3: per-shape cache
            TextRunBuildResult result = build_text_run(
                td, *shape.engine, shape.layout_spec, cache);

            if (!result.paragraphs.empty() && result.paragraphs.front()) {
                shape.layout = result.paragraphs.front();
                shape.glyphs = make_initial_glyph_states(
                    shape.layout->placed);
            }
        }
    }

    // ── In-place animator-stack evaluation on the ACTIVE side. ─────
    evaluate_animator_stack_into(
        shape.glyphs,
        shape.animators,
        shape.layout->placed,
        shape.layout->source_text,
        shape.layout->units,
        time
    );

    // ── PR 11 — mirror the animator stack onto dissolve_glyphs when
    //    inside the gap, so per-glyph animators (transform / opacity /
    //    blur / color / etc.) apply symmetrically to both sides. ────
    if (!shape.dissolve_glyphs.empty() && shape.dissolve_layout) {
        if (shape.dissolve_glyphs.size()
            != shape.dissolve_layout->placed.glyphs.size())
        {
            shape.dissolve_glyphs = make_initial_glyph_states(
                shape.dissolve_layout->placed);
        }
        evaluate_animator_stack_into(
            shape.dissolve_glyphs,
            shape.animators,
            shape.dissolve_layout->placed,
            shape.dissolve_layout->source_text,
            shape.dissolve_layout->units,
            time
        );
    }
}

// ═══════════════════════════════════════════════════════════
// apply_active_state_to_text_run_shape — slow-path orchestrator
// ═══════════════════════════════════════════════════════════

bool apply_active_state_to_text_run_shape(
    TextRunShape& shape,
    const ActiveTextState& state,
    FontEngine& engine,
    const TextLayoutSpec& layout_spec,
    TextLayoutCache* cache
) {
    using namespace text::driver;

    // ── Guard ───────────────────────────────────────────────────────
    if (state.active == nullptr) return false;

    // ── Stage 1: select target text per transition type ─────────────
    const std::string target_text = select_target_text(state);
    if (target_text.empty()) return false;

    // ── Stage 2: project effective font (P0-2 carries forward) ─────
    const FontSpec effective_font = compute_effective_font(
        state, shape.layout.get());

    // ── Stage 3: EffectiveTextState projection (current vs target) ──
    const EffectiveTextState target_state = target_effective_state(
        target_text, effective_font, layout_spec);

    if (shape.layout) {
        const EffectiveTextState current_state = current_effective_state(*shape.layout);
        if (fast_path_hit(current_state, target_state)) {
            // M1.5#2 closure: cache would hit → no rebuild.  Crucially
            // we still update dissolve_mix inside rebuild_dissolve_slot
            // below so the compositor sees the latest mix, then return
            // BEFORE the active-side rebuild (no work).
            (void)rebuild_dissolve_slot(
                shape, state, /*outgoing=*/state.dissolve_from
                    ? state.dissolve_from->defaults.font
                    : effective_font,
                layout_spec, engine, cache);
            return false;
        }
    }

    // ── Stage 4: cache-miss → rebuild active + crossfade slots ──────
    const bool active_rebuilt = rebuild_active_side(
        shape, target_text, effective_font, layout_spec, engine, cache);
    // Always run crossfade slot lifecycle regardless of active rebuild
    // outcome: an ActiveTextState transition out-of-gap clears the
    // outgoing slots even when the active side stays cached.
    const FontSpec outgoing_font =
        state.dissolve_from ? state.dissolve_from->defaults.font
                             : effective_font;
    (void)rebuild_dissolve_slot(
        shape, state, outgoing_font, layout_spec, engine, cache);

    return active_rebuilt;
}

// ═══════════════════════════════════════════════════════════
// prewarm_text_run_layout_for_frame — prewarm orchestrator
// ═══════════════════════════════════════════════════════════

bool prewarm_text_run_layout_for_frame(
    const TextRunShape& shape,
    Frame frame,
    TextLayoutCache* cache
) {
    // Guard rails: must be a doc-bound shape with a wired engine.
    if (!shape.animated_doc) {
        return false;
    }
    if (shape.engine == nullptr) {
        return false;
    }

    const ActiveTextState state =
        shape.animated_doc->sample_at(frame);
    return text::driver::prewarm_for_frame(
        shape, state, frame, shape.layout_spec, *shape.engine, cache);
}

} // namespace chronon3d
