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
#include <chronon3d/text/text_layout_cache.hpp>

#include <cmath>
#include <string>

namespace chronon3d {

namespace {

// Identity for the canonical pre-shaping map.  Reuse the complete semantic
// layout key (text + font + paragraph + layout options), not just the source
// string: a font/layout change must invalidate the codepoint→unit mapping.
[[nodiscard]] std::string pre_shaping_layout_key(const TextRunShape& shape) {
    TextLayoutCacheKey key;
    key.text = shape.pre_shaping_source;
    if (shape.layout) {
        const auto& font = shape.layout->font;
        key.font_path = font.font_path;
        key.font_family = font.font_family;
        key.font_weight = font.font_weight;
        key.font_style = font.font_style;
        key.font_size = shape.layout->font_size;
        key.direction = shape.layout->direction;
        key.language = shape.layout->language;
        key.variation_axes = shape.layout->variation_axes;
    }
    const auto& spec = shape.layout_spec;
    key.tracking = spec.tracking;
    key.box_width = spec.box.x;
    key.box_height = spec.box.y;
    key.wrap = spec.wrap;
    key.align = static_cast<int>(spec.align);
    key.vertical_align = static_cast<int>(spec.vertical_align);
    key.anchor = static_cast<int>(spec.anchor);
    key.centering_mode = static_cast<int>(spec.centering_mode);
    key.line_height = spec.line_height;
    key.max_lines = spec.max_lines;
    key.overflow = static_cast<int>(spec.overflow);
    key.ellipsis = spec.ellipsis;
    key.features = spec.features;
    key.paragraph = spec.paragraph;
    return std::to_string(key.digest());
}

// Build the canonical map once per authored source + layout identity.  When
// the active layout already corresponds to the authored source we can reuse
// its placed run and avoid a second shaping pass entirely.
[[nodiscard]] std::shared_ptr<const PreShapingUnitMap> ensure_pre_shaping_unit_map(
    TextRunShape& shape
) {
    if (!shape.has_pre_shaping_source || shape.layout == nullptr) {
        return nullptr;
    }
    const std::string key = pre_shaping_layout_key(shape);
    if (shape.pre_shaping_unit_map &&
        shape.pre_shaping_unit_map_source == shape.pre_shaping_source &&
        shape.pre_shaping_unit_map_key == key) {
        return shape.pre_shaping_unit_map;
    }

    std::shared_ptr<const PreShapingUnitMap> map;
    if (shape.layout->source_text == shape.pre_shaping_source) {
        map = build_pre_shaping_unit_map(
            shape.layout->placed, shape.pre_shaping_source, key);
    } else if (shape.engine != nullptr) {
        TextDocument td;
        td.utf8 = shape.pre_shaping_source;
        td.defaults.font = shape.layout->font;
        td.split_paragraphs();
        TextLayoutRequest request{
            &td, &shape.layout_spec, td.defaults.font, 0};
        request.features = shape.layout_spec.features;
        TextCompileServices services{shape.engine, shape.cache, {}};
        auto result = compile_text_document_layout(request, services);
        if (result && result.value()) {
            map = build_pre_shaping_unit_map(
                result.value()->placed, shape.pre_shaping_source, key);
        }
    }

    if (!map) {
        shape.pre_shaping_unit_map.reset();
        shape.pre_shaping_unit_map_source.clear();
        shape.pre_shaping_unit_map_key.clear();
        return nullptr;
    }
    shape.pre_shaping_unit_map = map;
    shape.pre_shaping_unit_map_source = shape.pre_shaping_source;
    shape.pre_shaping_unit_map_key = key;
    return map;
}

} // namespace

// ═══════════════════════════════════════════════════════════
// update_text_run_shape_per_frame — cheap-path orchestrator
// ═══════════════════════════════════════════════════════════

void update_text_run_shape_per_frame(
    TextRunShape& shape,
    SampleTime time,
    TextLayoutCache* cache /*M1.5#2: cache flows via shape.cache or compute path */
) {
    // ── Guard: static-shape fast path (layout nullptr is no-op) ─────
    if (!shape.layout) return;
    if (!shape.has_pre_shaping_source) {
        shape.pre_shaping_source = shape.layout->source_text;
        shape.has_pre_shaping_source = true;
    }
    shape.caret.frame = static_cast<f64>(time.integral_frame().integral());

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

    // ── P2-05 — PreLayout spacing reflow. ──────────────────────────
    // Reflow properties feed the paragraph composer, so they must be
    // applied before PostLayout evaluation.  A change in either amount
    // rebuilds the layout and re-seeds glyph states; the cache key carries
    // both values through ParagraphStyle.
    f32 reflow_word_spacing = 0.0f;
    f32 reflow_line_spacing = 0.0f;
    bool has_reflow_spacing = false;
    for (const auto& animator : shape.animators) {
        for (const auto& prop : animator.properties) {
            if (const auto* p = std::get_if<ReflowWordSpacingProperty>(&prop)) {
                reflow_word_spacing += p->pixels.evaluate(time);
                has_reflow_spacing = true;
            } else if (const auto* p = std::get_if<ReflowLineSpacingProperty>(&prop)) {
                reflow_line_spacing += p->pixels.evaluate(time);
                has_reflow_spacing = true;
            }
        }
    }
    if (has_reflow_spacing && shape.engine != nullptr) {
        const float target_word = shape.reflow_word_spacing_base + reflow_word_spacing;
        const float target_line = shape.reflow_line_spacing_base + reflow_line_spacing;
        auto& paragraph = shape.layout_spec.paragraph;
        if (std::abs(paragraph.word_spacing - target_word) > 1e-4f ||
            std::abs(paragraph.line_spacing - target_line) > 1e-4f) {
            paragraph.word_spacing = target_word;
            paragraph.line_spacing = target_line;
            TextDocument td;
            td.utf8 = shape.layout->source_text;
            td.defaults.font = shape.layout->font;
            td.split_paragraphs();
            TextLayoutRequest request{
                &td, &shape.layout_spec, td.defaults.font, 0};
            request.features = shape.layout_spec.features;
            TextCompileServices services{
                shape.engine,
                cache != nullptr ? cache : shape.cache, {}};
            if (auto result = compile_text_document_layout(request, services);
                result && result.value()) {
                shape.layout = std::move(result.value());
                reset_glyph_states_in_place(shape.glyphs, shape.layout->placed);
            }
        }
    }

    // ── Empty animators short-circuit: just (re)seed glyphs. ─────────
    if (shape.animators.empty()) {
        evaluate_animator_stack_into(
            shape.glyphs, shape.animators, shape.layout->placed,
            shape.layout->source_text, shape.layout->units, time,
            &shape.layout->font_spans);
        return;
    }

    // ── Pre-shaping phase: rebuild layout when offset_source differs. ─
    if (has_pre_shaping_properties(shape.animators)) {
        const bool selector_scoped = has_selector_scoped_pre_shaping(shape.animators);
        const auto unit_map = selector_scoped
            ? ensure_pre_shaping_unit_map(shape)
            : nullptr;
        std::string offset_source = evaluate_pre_shaping_source(
            shape.animators, shape.pre_shaping_source, time,
            selector_scoped ? unit_map.get() : nullptr);

        if (offset_source != shape.layout->source_text) {
            TextDocument td;
            td.utf8 = offset_source;
            if (shape.layout) {
                td.defaults.font = shape.layout->font;
            }
            td.split_paragraphs();

            auto* cache = shape.cache;  // Fase B3: per-shape cache
            TextLayoutRequest request{
                &td, &shape.layout_spec, td.defaults.font, 0};
            request.features = shape.layout_spec.features;
            TextCompileServices services{shape.engine, cache, {}};
            auto result = compile_text_document_layout(request, services);

            if (result && result.value()) {
                shape.layout = std::move(result.value());
                reset_glyph_states_in_place(shape.glyphs, shape.layout->placed);
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
        time,
        &shape.layout->font_spans
    );

    // ── PR 11 — mirror the animator stack onto dissolve_glyphs when
    //    inside the gap, so per-glyph animators (transform / opacity /
    //    blur / color / etc.) apply symmetrically to both sides. ────
    if (!shape.dissolve_glyphs.empty() && shape.dissolve_layout) {
        reset_glyph_states_in_place(shape.dissolve_glyphs, shape.dissolve_layout->placed);
        evaluate_animator_stack_into(
            shape.dissolve_glyphs,
            shape.animators,
            shape.dissolve_layout->placed,
            shape.dissolve_layout->source_text,
            shape.dissolve_layout->units,
            time,
            &shape.dissolve_layout->font_spans
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
    // Empty is a valid target for an authoritative transition (typewriter
    // frame 0).  Rebuild to a zero-glyph layout so both backends hit their
    // explicit no-op path instead of rendering a stale full-text layout.
    const std::string target_text = select_target_text(state);

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
            const bool dissolve_rebuilt = rebuild_dissolve_slot(
                shape, state, /*outgoing=*/state.dissolve_from
                    ? state.dissolve_from->defaults.font
                    : effective_font,
                layout_spec, engine, cache);
            // The active side was already current, but the dissolve slot may
            // still be newly materialized. Report that observable rebuild to
            // callers so the transition lifecycle is not mistaken for a
            // no-op (TICKET-068 crossfade contract).
            return dissolve_rebuilt;
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
