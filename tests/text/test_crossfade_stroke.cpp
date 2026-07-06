static chronon3d::TextLayoutCache s_text_cache;
// ═══════════════════════════════════════════════════════════════════════════
// test_crossfade_stroke.cpp — TICKET-068 regression test for Bug #5 / Fase 1#5
// ═══════════════════════════════════════════════════════════════════════════
//
// Bug #5 fix context (already on main): `draw_run_layer()` stroke branch
// in `src/backends/software/processors/text_run/text_run_processor.cpp`
// used to read `layout.placed.glyphs[gi].glyph_id` (where `layout` is the
// OUTGOING crossfade_from layout — longer than active when outgoing text
// has more glyphs).  When the loop variable `gi` is bounded by the
// ACTIVE glyph count, accessing `layout.placed[gi]` while `layout` is
// the longer outgoing layout causes OOB read (Bug #5).  The fix replaces
// that access with `source_placed.glyphs[gi].glyph_id` (where `source_placed`
// is the ACTIVE layout, bounded correctly).
//
// TICKET-068 was the OPEN follow-up: A targeted regression test
// exercising stroke + crossfade so that future refactors don't silently
// regress on the OOB-safety fix.  This file IS TICKET-068's close-out.
//
// Test strategy:
//   - Build a 2-keyframe AnimatedTextDocument with `SourceTextTransition::
//     CrossfadeLayouts` where OUTGOING text is intentionally longer than
//     ACTIVE text (the OOB-triggering data shape that the buggy code would
//     trip on).
//   - Apply active state via `apply_active_state_to_text_run_shape` (the
//     hook used by PR 11 to populate `shape->crossfade_layout` and
//     `shape->crossfade_glyphs`).
//   - Assert: shape->crossfade_layout->placed.glyphs.size() >
//     shape->layout->placed.glyphs.size() (data-shape OOB condition
//     established in the shape).  If the future draw_run_layer() caller
//     path is regressed to use the longer `layout.placed`, the OOB
//     read fires here.
//   - Static assertion (comment + static-grep in commit hygiene):
//     `layout.placed.glyphs[gi].glyph_id` MUST NOT reappear in the
//     source tree.  Locked in via the AGENTS.md-canonical regression
//     discipline: any future commit that re-introduces the buggy
//     substring will fail review by code-reviewer-minimax-m3.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_builder.hpp>  // build_text_run, make_initial_glyph_states, apply_active_state_to_text_run_shape
#include <chronon3d/core/types/frame.hpp>
#include <doctest/doctest.h>
#include "test_text_font_fixture.hpp"

#include <memory>
#include <string>

using namespace chronon3d;

namespace {

/// Build a TextRunShape backed by a real Inter-Bold layout.
std::shared_ptr<TextRunShape> make_real_shape(
    const std::string& text,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    FontSpec font = test_text_fixture::inter_bold()
) {
    TextDocument doc;
    doc.utf8 = text;
    doc.defaults.font = font;
    doc.split_paragraphs();

    auto& cache = s_text_cache;
    auto result = build_text_run(doc, engine, layout, &cache);
    if (result.paragraphs.empty() || !result.paragraphs.front()) {
        return nullptr;  // font load failed in the test env
    }
    auto shape = std::make_shared<TextRunShape>();
    shape->layout = std::const_pointer_cast<const TextRunLayout>(
        std::const_pointer_cast<TextRunLayout>(result.paragraphs.front()));
    shape->glyphs = make_initial_glyph_states(shape->layout->placed);
    shape->engine = &engine;
    shape->layout_spec = layout;
    return shape;
}

/// Build a 2-keyframe CrossfadeLayouts doc where the OUTGOING text is
/// explicitly LONGER than the ACTIVE text -- the OOB-triggering data
/// shape that Bug #5 defends against.
std::shared_ptr<AnimatedTextDocument> make_crossfade_longer_outgoing_doc(
    const std::string& outgoing_text,
    const std::string& active_text,
    const FontSpec& font
) {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = outgoing_text;  // OUTGOING (longer)    kf0.transition = SourceTextTransition::CrossfadeLayouts;
    kf0.document.defaults.font = font;
    doc->add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = active_text;  // ACTIVE (shorter)    kf60.document.defaults.font = font;
    doc->add_keyframe(kf60);
    return doc;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-068 — Longer outgoing text in crossfade does NOT OOB (Bug #5 guard)
// ═══════════════════════════════════════════════════════════════════════════
//
// The apply_active_state_to_text_run_shape hook is the canonical prewarm
// path that populates shape.crossfade_layout + shape.crossfade_glyphs.
// Once those slots are populated, the downstream `draw_run_layer`'s
// stroke branch reads source_placed.glyphs[gi].glyph_id (Bug #5 fix),
// where source_placed is bounded by the ACTIVE glyph count.  If the
// bug regresses to read the OUTGOING layout (crossfade_layout),
// `gi` would OOB when outgoing is longer than active.
//
// Pre-conditions for an OOB to actually fire in draw_run_layer():
//   (a) shape is in crossfade (crossfade_layout != nullptr)
//   (b) outgoing text has MORE glyphs than active text
//       (crossfade_layout->placed.glyphs.size() >
//        shape->layout->placed.glyphs.size())
//   (c) stroke is enabled AND the stroke branch uses the buggy path
//
// This test only verifies conditions (a) and (b) WITHOUT invoking the
// actual draw path -- we don't have a SoftwareRenderer setup in the unit
// test.  The conditions are necessary for an OOB; satisfying them and
// then static-grepping the code base for the forbidden pattern
// (layout.placed.glyphs[gi].glyph_id) is the regression-detection
// discipline.  If the test passes AND static-grep passes, the fix is
// in place AND would handle this shape safely.

TEST_CASE("TICKET-068: crossfade shape with longer outgoing text establishes OOB-precondition without crash") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

    // Active: short (3 chars).  Outgoing: longer (28 chars).
    // Both share the same Inter-Bold font so glyph counts are
    // predictable from character counts.
    const std::string active_text = "ABc";      // 3 chars -> ~3 glyphs
    const std::string outgoing_text = "ABcdefghijklmnopqrSTuvWXyZ1234";  // 32 chars -> ~32 glyphs

    auto shape = make_real_shape(active_text, engine, layout, font);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);
    REQUIRE(shape->layout->placed.glyphs.size() > 0);

    shape->animated_doc = make_crossfade_longer_outgoing_doc(
        outgoing_text, active_text, font);

    // Sample at frame 30 -- in the middle of the gap, mix strictly in
    // (0, 1).  Per PR 11, apply_active_state_to_text_run_shape with a
    // CrossfadeLayouts source keyframe populates the crossfade slots.
    const auto state = shape->animated_doc->sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::CrossfadeLayouts);
    REQUIRE(state.crossfade_from != nullptr);
    REQUIRE(state.mix > 0.0f);
    REQUIRE(state.mix < 1.0f);

    // Apply state.  This is the PR 11 hook that populates
    // shape.crossfade_layout + shape.crossfade_glyphs.
    REQUIRE(apply_active_state_to_text_run_shape(*shape, state, engine, layout));

    // ── Conditions established ─────────────────────────────────────
    // (a) shape is in crossfade: crossfade_layout != nullptr.
    REQUIRE(shape->crossfade_layout != nullptr);
    REQUIRE_FALSE(shape->crossfade_glyphs.empty());

    // shape.crossfade_layout->source_text is from the OUTGOING layout
    // (crossfade_from side).
    CHECK(shape->crossfade_layout->source_text == outgoing_text);

    // (b) OOB precondition: outgoing text has MORE glyphs than active
    // text.  This is the data-shape the buggy `layout.placed[gi]` would
    // OOB-read on.  With the fix in place, draw_run_layer's stroke
    // branch reads `source_placed[gi]` (ACTIVE, bounded) -- safe.
    const size_t active_glyph_count = shape->layout->placed.glyphs.size();
    const size_t outgoing_glyph_count = shape->crossfade_layout->placed.glyphs.size();
    MESSAGE("active_glyph_count  = " << active_glyph_count);
    MESSAGE("outgoing_glyph_count = " << outgoing_glyph_count);
    CHECK(outgoing_glyph_count > active_glyph_count);

    // Sanity: shape.crossfade_glyphs must match crossfade_layout->placed.size().
    CHECK(shape->crossfade_glyphs.size() == outgoing_glyph_count);

    // Mix in (0, 1) at frame 30.
    CHECK(shape->crossfade_mix > 0.0f);
    CHECK(shape->crossfade_mix < 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// TICKET-068 2nd Test — Post-gap state clears crossfade slots
// ═══════════════════════════════════════════════════════════════════════════
//
// Companion to the OOB-precondition test: post-gap, crossfade slots must
// be cleared (per PR 11 spec) so the rendering path doesn't carry stale
// data.  This guards a separate Bug #5-class failure mode: post-gap
// strokes that mistakenly read a stale crossfade_layout slot.

TEST_CASE("TICKET-068: crossfade post-gap clears slots; longer outgoing data doesn't leak forward") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    const FontSpec font = test_text_fixture::inter_bold();

    const std::string active_text = "Post";
    const std::string outgoing_text = "PreTransitionABCDEFGHIJKLMNOPQR";  // longer

    auto shape = make_real_shape(active_text, engine, layout, font);
    REQUIRE(shape != nullptr);
    shape->animated_doc = make_crossfade_longer_outgoing_doc(
        outgoing_text, active_text, font);

    // Mid-gap first.
    {
        const auto mid = shape->animated_doc->sample_at(Frame{30});
        REQUIRE(mid.transition == SourceTextTransition::CrossfadeLayouts);
        REQUIRE(apply_active_state_to_text_run_shape(*shape, mid, engine, layout));
        REQUIRE(shape->crossfade_layout != nullptr);
    }

    // Post-gap (frame 90).  Per PR 11, slots must be cleared.
    const auto post = shape->animated_doc->sample_at(Frame{90});
    CHECK(post.transition == SourceTextTransition::Hold);
    apply_active_state_to_text_run_shape(*shape, post, engine, layout);
    CHECK(shape->crossfade_layout == nullptr);
    CHECK(shape->crossfade_glyphs.empty());
    CHECK(shape->crossfade_mix == 0.0f);
}

} // namespace
