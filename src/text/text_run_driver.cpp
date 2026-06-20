// ═══════════════════════════════════════════════════════════════════════════
// text_run_driver.cpp — Wire TextRunLayout into the compositor and
// animation driver (PR 8).
//
// Two free functions:
//   * update_text_run_shape_per_frame — cheap per-frame re-evaluation
//     of the AE-style animator stack, writing per-glyph state back
//     into TextRunShape::glyphs.
//   * apply_active_state_to_text_run_shape — bridges AnimatedTextDocument
//     into the layout pipeline, rebuilding shape->layout when the
//     transition text changes.
//
// Both functions preserve the rest of the shape (paint, material,
// shadows, animators) across mutations; only layout and glyphs mutate.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_driver.hpp>

#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_document.hpp>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// update_text_run_shape_per_frame
// ═══════════════════════════════════════════════════════════════════════════
//
// Cheap path.  Re-evaluates animators in-place; no shaping cost.
// PR 9 extension: when the shape carries an AnimatedTextDocument binding,
// re-sample it at the integral frame and forward the resulting
// ActiveTextState through `apply_active_state_to_text_run_shape` so
// transition_text changes (Scramble / Morph) reach the rendered glyphs
// every frame — not just at materialization time.

void update_text_run_shape_per_frame(TextRunShape& shape, SampleTime time) {
    // ── Guard rails — the compositor (TextRunNode) calls this every frame
    //    so we must be a no-op for static shapes. ────────────────────────
    if (!shape.layout) return;

    // ── PR 9 — re-sample AnimatedTextDocument first, then animators ────
    //
    // Order matters: apply_active_state_to_text_run_shape may SWAP the
    // shape's `layout` slot (when transition_text differs from the
    // current layout.source_text).  After such a swap the AE-style
    // animator stack must be evaluated against the NEW layout — otherwise
    // per-glyph animators would still target the previous glyph count
    // / cluster map.  Re-evaluating after the swap (lines below) clears
    // that residue.
    //
    // PR 10 NOTE: cache-key invalidation for the per-frame loop is now
    // closed.  `TextRunNode::cache_key(ctx)` and
    // `MultiSourceNode::cache_key(ctx)` call `hash_text_run_shape(*shape,
    // ctx.frame.sample_time.integral_frame())`, the frame overload that
    // folds transition_type + active->utf8 + active->defaults.font +
    // transition_text bytes + morph_map bytes + mix into the cache key.
    // This guarantees the post-mutation layout's hash differs from any
    // other frame's — including font-only Cut transitions (which would
    // share the legacy per-text hash) and per-frame scramble bytes.
    // Layout-shaped work is still re-done every Scramble/Morph frame;
    // `prewarm_text_run_layout_for_frame` is the public hook to
    // mitigate that cost for anticipator schedulers.
    if (shape.animated_doc && shape.engine != nullptr) {
        const ActiveTextState state =
            shape.animated_doc->sample_at(time.integral_frame());
        if (state.active != nullptr) {
            // PR 10 NOTE: the return value (true iff shape->layout was
            // swapped by the apply) is intentionally discarded here.
            // The cache-key refresher in `hash_text_run_shape(*shape,
            // ctx.frame.sample_time.integral_frame())` (called by the
            // executor) already captures the post-apply layout bytes,
            // so the per-frame loop doesn't need the boolean feedback.
            (void)apply_active_state_to_text_run_shape(
                shape, state, *shape.engine, shape.layout_spec);
        }
    }

    if (shape.animators.empty()) {
        // Even with no animators, ensure the glyphs vector is sized for the
        // layout the first time the shape sees the driver.  This handles
        // shapes produced by pipelines that skipped `materialize_text_run_shape`
        // (e.g. tests, custom build paths) where the first frame must still
        // see initial states.  Note: a doc-driven layout swap above may have
        // changed the placed-run glyph count, so we always re-size here.
        if (shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
            shape.glyphs = make_initial_glyph_states(shape.layout->placed);
        }
        return;
    }

    // ── Re-evaluate in place.  evaluate_animator_stack_into re-allocates
    //    inout_states to match `placed.glyphs.size()`.  We always pass the
    //    current layout's placed run so glyph counts stay in sync with
    //    the (possibly rebuilt) layout. ────────────────────────────────
    evaluate_animator_stack_into(
        shape.glyphs,
        shape.animators,
        shape.layout->placed,
        shape.layout->source_text,
        time
    );
}

// ═══════════════════════════════════════════════════════════════════════════
// apply_active_state_to_text_run_shape
// ═══════════════════════════════════════════════════════════════════════════
//
// Slow path — only runs when the per-frame transition text differs from
// the shape's current layout.  Cost: 1× HarfBuzz shaping pass + paragraph
// composition (mitigated by the shared TextLayoutCache, which keys on the
// transition text content + font + paragraph-style digest).

bool apply_active_state_to_text_run_shape(
    TextRunShape& shape,
    const ActiveTextState& state,
    FontEngine& engine,
    const TextLayoutSpec& layout_spec
) {
    // ── Guard: no active document or shape with no layout ────────────
    if (!state.active) return false;

    // ── Pick target text from the transition type ─────────────────────
    std::string target_text;
    switch (state.transition) {
        case SourceTextTransition::Hold:
        case SourceTextTransition::Cut:
        case SourceTextTransition::CrossfadeLayouts:
            // No transition-text blending for these modes (Hold/Cut are
            // discrete; CrossfadeLayouts blends per-glyph in the
            // compositor, not in the layout itself).  Render the active
            // document directly.
            target_text = state.active->utf8;
            break;

        case SourceTextTransition::Scramble:
        case SourceTextTransition::Morph:
            // transition_text holds the per-frame generated string
            // produced by AnimatedTextDocument::sample_at.  Empty only
            // when pre/post-conditions clip the transition (e.g. mix=1
            // already; falls through to active->utf8).
            target_text = state.transition_text.empty()
                ? state.active->utf8
                : state.transition_text;
            break;
    }

    // Short-circuit when there is nothing to render.  Building an empty
    // TextDocument is wasted work; the shape already represents its
    // current state correctly.
    if (target_text.empty()) {
        return false;
    }

    // ── Fast-path: text already matches the shape's current layout ──
    if (shape.layout
        && shape.layout->source_text == target_text
        && !target_text.empty())
    {
        return false;  // cache would hit; skip rebuild
    }

    // ── Build a synthetic TextDocument from the transition text ──────
    //
    // Copy the layout's font (path/weight/style/size) so cache-key
    // construction in build_text_run uses the same effective FontSpec
    // the shape was initially laid out with.  When the active document
    // carries a default font that differs from the shape's, prefer the
    // active document's defaults (the scene typically rewires fonts on
    // keyframes).
    TextDocument td;
    td.utf8 = target_text;

    if (shape.layout) {
        td.defaults.font = shape.layout->font;
    }
    if (!state.active->defaults.font.font_path.empty()) {
        // Keyframe-driven font swap.  Most uses keep the same family
        // and only change weight/style/size; AudioKeyframes etc. vary
        // them.  When path is empty we keep the layout's path.
        if (!state.active->defaults.font.font_path.empty()) {
            td.defaults.font.font_path = state.active->defaults.font.font_path;
        }
        if (!state.active->defaults.font.font_family.empty()) {
            td.defaults.font.font_family = state.active->defaults.font.font_family;
        }
        if (state.active->defaults.font.font_weight != 0) {
            td.defaults.font.font_weight = state.active->defaults.font.font_weight;
        }
        if (!state.active->defaults.font.font_style.empty()) {
            td.defaults.font.font_style = state.active->defaults.font.font_style;
        }
        if (state.active->defaults.font.font_size > 0.0f) {
            td.defaults.font.font_size = state.active->defaults.font.font_size;
        }
    }

    // ── Split paragraphs (build_text_run requires pre-split input) ──
    td.split_paragraphs();

    // ── Build via the canonical pipeline (PR 7) ──────────────────────
    auto& cache = shared_text_layout_cache();
    TextRunBuildResult result = build_text_run(td, engine, layout_spec, &cache);

    if (result.paragraphs.empty() || !result.paragraphs.front()) {
        return false;
    }

    // ── Swap layout, re-seed glyphs ──────────────────────────────────
    //
    // Paint/material/shadows/animators are preserved because they live
    // on TextRunShape — only the `layout` slot is replaced.  glyphs is
    // re-seeded via make_initial_glyph_states so the per-frame driver
    // sees a fresh, layout-aligned vector.  Any subsequent frame will
    // overwrite glyphs via update_text_run_shape_per_frame anyway, so
    // we don't need to run the animator stack here.
    //
    // `SharedTextRunLayout` is `shared_ptr<const TextRunLayout>`.  A
    // `shared_ptr<TextRunLayout>` implicitly converts via the standard
    // `T* → const T*` builder conversion — no const_pointer_cast needed.
    shape.layout = result.paragraphs.front();
    shape.glyphs = make_initial_glyph_states(shape.layout->placed);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// prewarm_text_run_layout_for_frame — PR 10 public pre-warm hook
// ═══════════════════════════════════════════════════════════════════════════
//
// Reads (not mutates) the shape, samples the bound AnimatedTextDocument at
// `frame`, builds the TextDocument the per-frame driver will need, and
// stores its layout into shared_text_layout_cache().  Idempotent on the
// same (text, font, layout) key.

bool prewarm_text_run_layout_for_frame(
    const TextRunShape& shape,
    Frame frame
) {
    // ── Guard rails — only meaningful for shapes with a binding ──────
    if (!shape.animated_doc) {
        return false;  // static shape — nothing transitions; skip.
    }
    if (shape.engine == nullptr) {
        // The per-frame driver logs this on the FIRST actual use of the
        // shape; the prewarm hook is silent so an anticipator that runs
        // before the layer is fully wired doesn't spam.
        return false;
    }

    const ActiveTextState state = shape.animated_doc->sample_at(frame);
    if (state.active == nullptr) {
        return false;
    }

    // ── Compute the target text the per-frame driver will render ────
    //
    // Mirrors the `target_text` selection in
    // `apply_active_state_to_text_run_shape` exactly so the cache
    // key we build lines up with the key the driver will hit on its
    // next `build_text_run` call.  If we diverge, prewarm is wasted.
    std::string target_text;
    switch (state.transition) {
        case SourceTextTransition::Hold:
        case SourceTextTransition::Cut:
        case SourceTextTransition::CrossfadeLayouts:
            target_text = state.active->utf8;
            break;
        case SourceTextTransition::Scramble:
        case SourceTextTransition::Morph:
            target_text = state.transition_text.empty()
                ? state.active->utf8
                : state.transition_text;
            break;
    }
    if (target_text.empty()) {
        return false;
    }

    // ── Build a synthetic TextDocument matching apply_active_state_… ──
    //
    // Font override rules mirror `apply_active_state_to_text_run_shape`
    // exactly so the cache key we build here lines up with the key the
    // driver will hit on its next `build_text_run` call.  Any divergence
    // here would cause prewarm to silently store under a different key
    // and the driver's lookup to miss.
    TextDocument td;
    td.utf8 = target_text;
    if (shape.layout) {
        td.defaults.font = shape.layout->font;
    }
    if (!state.active->defaults.font.font_path.empty()) {
        td.defaults.font.font_path = state.active->defaults.font.font_path;
        if (!state.active->defaults.font.font_family.empty()) {
            td.defaults.font.font_family = state.active->defaults.font.font_family;
        }
        if (state.active->defaults.font.font_weight != 0) {
            td.defaults.font.font_weight = state.active->defaults.font.font_weight;
        }
        if (!state.active->defaults.font.font_style.empty()) {
            td.defaults.font.font_style = state.active->defaults.font.font_style;
        }
        if (state.active->defaults.font.font_size > 0.0f) {
            td.defaults.font.font_size = state.active->defaults.font.font_size;
        }
    }
    td.split_paragraphs();

    // ── Build via the canonical pipeline (PR 7). ─────────────────
    //
    // `build_text_run` does an internal cache lookup first.  On a hit
    // it returns the cached layout immediately (skips HarfBuzz).  On
    // a miss it shapes the text, stores the result, and returns the
    // freshly-built layout.  Either way the shared cache is left in
    // a consistent state and the next frame's
    // `apply_active_state_to_text_run_shape` lookup will hit.
    //
    // Do NOT use size-before / size-after deltas to detect a "new"
    // insert: `LruCache::put` re-promotes on overwrite (size
    // unchanged), which would falsely report prewarm as a failure
    // for already-cached layouts.  The semantic we expose to callers
    // is "operation succeeded" — the cache now contains an entry for
    // `target_text` regardless of how it got there.
    auto& cache = shared_text_layout_cache();
    TextRunBuildResult result =
        build_text_run(td, *shape.engine, shape.layout_spec, &cache);

    // ── Return true on operation success ─────────────────────────
    //
    // `result.paragraphs.empty()` only happens when build_text_run
    // itself failed (e.g. font load failure, malformed document).
    // Caller can use this to log + skip the anticipator entry.
    return !result.paragraphs.empty();
}

} // namespace chronon3d
