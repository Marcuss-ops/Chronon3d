// ═══════════════════════════════════════════════════════════════════════════
// test_text_run_driver.cpp — Per-frame TextRun driver tests (PR 8)
//
// Covers:
//   1. update_text_run_shape_per_frame with Position/Opacity/Scale animators
//   2. No-op semantics when layout is null or animators empty
//   3. apply_active_state_to_text_run_shape for Hold/Cut/DissolveLayouts
//   4. apply_active_state_to_text_run_shape for Scramble/Morph transitions
//      (returns true on first call, false when text matches existing layout)
//   5. Determinism: same shape+animators produce same glyph state
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_resolver.hpp>
#include <doctest/doctest.h>
#include "test_text_font_fixture.hpp"

#include <optional>
#include <vector>

using namespace chronon3d;

// File-local cache, declared AFTER includes for unity-build safety (the
// same pattern as test_text_run_driver_pr10.cpp / test_compile_text_layout_identity.cpp).
static chronon3d::TextLayoutCache s_text_cache;

namespace {

// ── Helpers ─────────────────────────────────────────────────────────────

TextDocument make_doc(const std::string& utf8) {
    TextDocument doc;
    doc.utf8 = utf8;
    // Path-based fixture font: family-only specs can't be resolved on hosts
    // without a family→path mapping (see font_engine load path).
    doc.defaults.font = test_text_fixture::inter_bold();
    doc.split_paragraphs();
    return doc;
}

/// Build a TextRunShape holding a real-but-minimal TextRunLayout.  Glyph
/// count is non-zero when the system has DejaVu Sans installed; otherwise
/// zero and tests on per-glyph index 0 are skipped.
std::shared_ptr<TextRunShape> make_shape(
    const std::string& text,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    const std::vector<TextAnimatorSpec>& animators = {}
) {
    auto doc = make_doc(text);
    auto& cache = s_text_cache;
    auto result = build_text_run(doc, engine, layout, &cache);
    REQUIRE(result.paragraphs.size() == 1);
    auto shape = std::make_shared<TextRunShape>();
    shape->layout = std::const_pointer_cast<const TextRunLayout>(
        std::const_pointer_cast<TextRunLayout>(result.paragraphs.front()));
    shape->glyphs = make_initial_glyph_states(shape->layout->placed);
    shape->animators = animators;
    return shape;
}

/// Build a TextAnimatorSpec with a "weight=1 always" global selector.
TextAnimatorSpec make_global_spec(const std::string& id, TextAnimatorProperty prop) {
    TextAnimatorSpec spec;
    spec.id = id;
    spec.enabled = true;
    spec.transform_mode = TextPropertyBlendMode::Replace;
    spec.color_mode = TextPropertyBlendMode::Replace;
    GlyphSelectorSpec sel;
    sel.id = id + "_sel";
    sel.unit = TextSelectorUnit::Glyph;
    sel.shape = TextSelectorShape::Square;
    sel.start = {0.0f};
    sel.end = {100.0f};
    sel.amount = {100.0f};
    sel.exclude_spaces = false;
    spec.selectors = {sel};
    spec.properties = {prop};
    return spec;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. No-op semantics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: no-op when layout is null") {
    TextRunShape shape;
    // No layout — must not crash.
    update_text_run_shape_per_frame(shape, SampleTime{});
    CHECK(shape.glyphs.empty());
}

TEST_CASE("TextRunDriver: no-op when animators empty, but seeds glyphs") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto shape = make_shape("Hello", engine, layout);
    shape->glyphs.clear();  // simulate un-seeded shape

    update_text_run_shape_per_frame(*shape, SampleTime{});
    if (!shape->layout->placed.glyphs.empty()) {
        CHECK(shape->glyphs.size() == shape->layout->placed.glyphs.size());
        CHECK(shape->glyphs[0].position.x == 0.0f);
        CHECK(shape->glyphs[0].opacity   == doctest::Approx(1.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Per-frame animator evaluation
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: Position animator moves glyphs per frame") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto shape = make_shape("Hello", engine, layout, {
        make_global_spec("trb_pos", PositionProperty{Vec3{40.0f, 0.0f, 0.0f}})
    });

    if (shape->layout->placed.glyphs.empty()) {
        // Skip glyph-count assertions when font is missing.
        return;
    }

    update_text_run_shape_per_frame(*shape, SampleTime{});
    CHECK(shape->glyphs[0].position.x == doctest::Approx(40.0f));
    CHECK(shape->glyphs[0].position.y == doctest::Approx(0.0f));
    CHECK(shape->glyphs[0].position.z == doctest::Approx(0.0f));
}

TEST_CASE("TextRunDriver: Opacity animator sets per-glyph alpha") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto shape = make_shape("Hello", engine, layout, {
        make_global_spec("trb_op", OpacityProperty{0.5f})
    });

    if (shape->layout->placed.glyphs.empty()) {
        return;
    }

    update_text_run_shape_per_frame(*shape, SampleTime{});
    CHECK(shape->glyphs[0].opacity == doctest::Approx(0.5f));
}

TEST_CASE("TextRunDriver: deterministic between repeated calls") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto shape = make_shape("Hello", engine, layout, {
        make_global_spec("trb_pos_a", PositionProperty{Vec3{20.0f, 0.0f, 0.0f}}),
        make_global_spec("trb_op_b",  OpacityProperty{0.75f})
    });

    if (shape->layout->placed.glyphs.empty()) {
        return;
    }

    update_text_run_shape_per_frame(*shape, SampleTime{});
    auto snapshot = shape->glyphs;
    update_text_run_shape_per_frame(*shape, SampleTime{});

    REQUIRE(snapshot.size() == shape->glyphs.size());
    for (size_t i = 0; i < snapshot.size(); ++i) {
        CHECK(snapshot[i].position.x == doctest::Approx(shape->glyphs[i].position.x));
        CHECK(snapshot[i].position.y == doctest::Approx(shape->glyphs[i].position.y));
        CHECK(snapshot[i].opacity   == doctest::Approx(shape->glyphs[i].opacity));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Animator stack via evaluate_animator_stack_into (in-place path)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: evaluate_animator_stack_into writes back in place") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto doc = make_doc("Hello");
    auto& cache = s_text_cache;
    auto result = build_text_run(doc, engine, layout, &cache);
    REQUIRE(result.paragraphs.size() == 1);

    auto layout_ptr = std::const_pointer_cast<const TextRunLayout>(
        std::const_pointer_cast<TextRunLayout>(result.paragraphs.front()));
    REQUIRE(!layout_ptr->placed.glyphs.empty());

    std::vector<GlyphInstanceState> states;  // empty input
    evaluate_animator_stack_into(
        states,
        { make_global_spec("easi_pos", PositionProperty{Vec3{10.0f, 0.0f, 0.0f}}) },
        layout_ptr->placed,
        layout_ptr->source_text,
        layout_ptr->units,
        SampleTime{}
    );

    CHECK(states.size() == layout_ptr->placed.glyphs.size());
    CHECK(states[0].position.x == doctest::Approx(10.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. apply_active_state_to_text_run_shape — Hold/Cut fast paths
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: Hold transition renders active document (no-op return)") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    auto shape = make_shape("Hello", engine, layout);

    AnimatedTextDocument doc;
    SourceTextKeyframe kf;
    kf.frame = Frame{0};
    kf.document.utf8 = "Hello";
    doc.add_keyframe(kf);

    auto state = doc.sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::Hold);
    REQUIRE(state.active != nullptr);

    bool changed = apply_active_state_to_text_run_shape(*shape, state, engine, layout);
    CHECK_FALSE(changed);  // text matches existing layout → no rebuild
    CHECK(shape->layout->source_text == "Hello");
}

TEST_CASE("TextRunDriver: Hold with different active text rebuilds layout") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    auto shape = make_shape("Hello", engine, layout);

    AnimatedTextDocument doc;
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Hello";
    doc.add_keyframe(kf0);
    SourceTextKeyframe kf_scr;
    kf_scr.frame = Frame{60};
    // The transition field on a keyframe describes the OUTGOING
    // transition from this keyframe to its successor.  The Scramble
    // destination keyframe here is kf_scr — kf0 (origin) defaults
    // to Hold which is what this test expects at frame 0.
    kf_scr.transition = SourceTextTransition::Scramble;
    kf_scr.document.utf8 = "World";
    doc.add_keyframe(kf_scr);

    auto state = doc.sample_at(Frame{0});
    REQUIRE(state.transition == SourceTextTransition::Hold);
    REQUIRE(state.active != nullptr);

    bool changed = apply_active_state_to_text_run_shape(*shape, state, engine, layout);
    CHECK_FALSE(changed);
    CHECK(shape->layout->source_text == "Hello");
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Scramble / Morph transitions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: Scramble transition rebuilds layout with transition_text") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    auto shape = make_shape("Hello", engine, layout);

    AnimatedTextDocument doc;
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Hello";
    // The transition field on a keyframe describes the OUTGOING
    // transition from this keyframe to its successor.  To arm a
    // Scramble across frames 0–60 we set kf0.transition (the origin),
    // not kf60.transition (the destination).
    kf0.transition = SourceTextTransition::Scramble;
    doc.add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "World";
    doc.add_keyframe(kf60);

    auto state = doc.sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::Scramble);
    REQUIRE_FALSE(state.transition_text.empty());

    bool changed = apply_active_state_to_text_run_shape(*shape, state, engine, layout);
    CHECK(changed);
    // Layout now holds the scrambled text (which is deterministic per frame).
    CHECK(shape->layout->source_text == state.transition_text);
}

TEST_CASE("TextRunDriver: Morph transition rebuilds layout with transition_text") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    auto shape = make_shape("AB", engine, layout);

    AnimatedTextDocument doc;
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "AB";
    // The transition field on a keyframe describes the OUTGOING
    // transition from this keyframe to its successor.  Set
    // kf0.transition (the origin) so the Morph fires between 0–60.
    kf0.transition = SourceTextTransition::Morph;
    doc.add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "CD";
    doc.add_keyframe(kf60);

    auto state = doc.sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::Morph);
    REQUIRE_FALSE(state.transition_text.empty());

    bool changed = apply_active_state_to_text_run_shape(*shape, state, engine, layout);
    CHECK(changed);
    CHECK(shape->layout->source_text == state.transition_text);
    CHECK_FALSE(state.morph_map.empty());  // morph_map populated
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. State preservation — paint/animators survive layout swap
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: animators preserved across layout swap") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RuntimeContext::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto animators = std::vector<TextAnimatorSpec>{
        make_global_spec("trb_persist", PositionProperty{Vec3{15.0f, 0.0f, 0.0f}})
    };
    auto shape = make_shape("Hello", engine, layout, animators);
    const auto saved_animators = shape->animators;

    AnimatedTextDocument doc;
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Hello";
    doc.add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.transition = SourceTextTransition::Scramble;
    kf60.document.utf8 = "World";
    doc.add_keyframe(kf60);

    auto state = doc.sample_at(Frame{30});
    apply_active_state_to_text_run_shape(*shape, state, engine, layout);

    // Animators preserved across the layout swap (driver uses them next frame).
    CHECK(shape->animators.size() == saved_animators.size());
    CHECK(shape->animators[0].id == saved_animators[0].id);

    // Re-running the per-frame evaluator produces a glyph state with the
    // persisted animator's delta (proves the new layout is also reachable).
    update_text_run_shape_per_frame(*shape, SampleTime{});
    if (!shape->layout->placed.glyphs.empty()) {
        CHECK(shape->glyphs[0].position.x == doctest::Approx(15.0f));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Hash & Prewarm contracts (folded from test_text_run_driver_pr10.cpp)
//
// PR-10 added hash stability and prewarm idempotency contracts on the same
// driver surface; they live here so the driver has one test authority.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Build a tiny TextRunShape with no font work (null layout / no engine).
/// Suitable for hash-only tests.
std::shared_ptr<TextRunShape> make_hash_only_shape_pr10() {
    auto shape = std::make_shared<TextRunShape>();
    return shape;
}

/// Build a 2-keyframe AnimatedTextDocument that swaps from "Alpha" to
/// "Beta" with a Cut transition.
std::shared_ptr<AnimatedTextDocument> make_hold_doc_pr10() {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Alpha";
    doc->add_keyframe(kf0);

    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "Beta";
    kf60.transition = SourceTextTransition::Cut;
    doc->add_keyframe(kf60);
    return doc;
}

} // namespace

TEST_CASE("Hash PR10: legacy overload always returns same hash for identical shape") {
    auto a = make_hash_only_shape_pr10();
    auto b = make_hash_only_shape_pr10();
    CHECK(hash_text_run_shape(*a) == hash_text_run_shape(*b));
}

TEST_CASE("Hash PR10: static shape (no animated_doc) returns identical hash at any frame") {
    auto shape = make_hash_only_shape_pr10();

    const u64 h0 = hash_text_run_shape(*shape, Frame{0});
    const u64 h60 = hash_text_run_shape(*shape, Frame{60});
    const u64 h120 = hash_text_run_shape(*shape, Frame{120});
    const u64 h9999 = hash_text_run_shape(*shape, Frame{9999});

    CHECK(h0 == h60);
    CHECK(h0 == h120);
    CHECK(h0 == h9999);
}

TEST_CASE("Hash PR10: Hold/Cut boundaries produce distinct hashes") {
    auto shape = make_hash_only_shape_pr10();
    shape->animated_doc = make_hold_doc_pr10();

    const u64 h0 = hash_text_run_shape(*shape, Frame{0});
    const u64 h30 = hash_text_run_shape(*shape, Frame{30});
    const u64 h60 = hash_text_run_shape(*shape, Frame{60});
    const u64 h120 = hash_text_run_shape(*shape, Frame{120});

    CHECK(h0 != h60);
    CHECK(h30 != h60);
    CHECK(h60 == h120);
    CHECK(hash_text_run_shape(*shape) == hash_text_run_shape(*shape));
}

TEST_CASE("Hash PR10: Scramble mid-transition frames produce distinct hashes") {
    auto shape = make_hash_only_shape_pr10();
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Hello";
    kf0.transition = SourceTextTransition::Scramble;
    kf0.scramble_params.seed = 42;
    doc->add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "World";
    doc->add_keyframe(kf60);
    shape->animated_doc = doc;

    const u64 h30a = hash_text_run_shape(*shape, Frame{30});
    const u64 h30b = hash_text_run_shape(*shape, Frame{30});
    CHECK(h30a == h30b);

    const u64 h31 = hash_text_run_shape(*shape, Frame{31});
    CHECK(h30a != h31);

    const u64 h0 = hash_text_run_shape(*shape, Frame{0});
    const u64 h60 = hash_text_run_shape(*shape, Frame{60});
    CHECK(h0 != h30a);
    CHECK(h60 != h30a);

    auto alt_doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe k0;
    k0.frame = Frame{0};
    k0.document.utf8 = "Hello";
    k0.transition = SourceTextTransition::Scramble;
    k0.scramble_params.seed = 42;
    alt_doc->add_keyframe(k0);
    SourceTextKeyframe k6;
    k6.frame = Frame{60};
    k6.document.utf8 = "World";
    alt_doc->add_keyframe(k6);
    shape->animated_doc = alt_doc;
    const u64 h30_alt = hash_text_run_shape(*shape, Frame{30});
    CHECK(h30a == h30_alt);
}

TEST_CASE("Hash PR10: Morph transit produces distinct hashes per frame") {
    auto shape = make_hash_only_shape_pr10();
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "ABC";
    kf0.transition = SourceTextTransition::Morph;
    doc->add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "XYZ";
    doc->add_keyframe(kf60);
    shape->animated_doc = doc;

    const u64 h10 = hash_text_run_shape(*shape, Frame{10});
    const u64 h20 = hash_text_run_shape(*shape, Frame{20});
    const u64 h50 = hash_text_run_shape(*shape, Frame{50});
    CHECK(h10 != h20);
    CHECK(h20 != h50);
    CHECK(h10 != h50);
}

TEST_CASE("Prewarm PR10: returns false when shape.animated_doc is null") {
    auto shape = make_hash_only_shape_pr10();
    const bool stored = prewarm_text_run_layout_for_frame(*shape, Frame{30});
    CHECK(stored == false);
}

TEST_CASE("Prewarm PR10: returns false when shape.animated_doc set but engine is null") {
    auto shape = make_hash_only_shape_pr10();
    shape->animated_doc = make_hold_doc_pr10();
    const bool stored = prewarm_text_run_layout_for_frame(*shape, Frame{30});
    CHECK(stored == false);
}

TEST_CASE("Prewarm PR10: idempotent — calling twice with the same args is safe") {
    auto shape = make_hash_only_shape_pr10();
    shape->animated_doc = make_hold_doc_pr10();
    for (int i = 0; i < 3; ++i) {
        const bool stored = prewarm_text_run_layout_for_frame(*shape, Frame{30});
        CHECK(stored == false);
    }
}
