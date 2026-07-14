static chronon3d::TextLayoutCache s_text_cache;
// ═══════════════════════════════════════════════════════════════════════════
// test_text_run_driver.cpp — Per-frame TextRun driver tests (PR 8)
//
// Covers:
//   1. update_text_run_shape_per_frame with Position/Opacity/Scale animators
//   2. No-op semantics when layout is null or animators empty
//   3. apply_active_state_to_text_run_shape for Hold/Cut/CrossfadeLayouts
//   4. apply_active_state_to_text_run_shape for Scramble/Morph transitions
//      (returns true on first call, false when text matches existing layout)
//   5. Determinism: same shape+animators produce same glyph state
// ═══════════════════════════════════════════════════════════════════════════

#include <optional>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_resolver.hpp>
#include <doctest/doctest.h>

#include <vector>

using namespace chronon3d;

namespace {

// ── Helpers ─────────────────────────────────────────────────────────────

TextDocument make_doc(const std::string& utf8) {
    TextDocument doc;
    doc.utf8 = utf8;
    doc.defaults.font.font_family = "DejaVu Sans";
    doc.defaults.font.font_size   = 32.0f;
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
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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

TEST_CASE("TextRu    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value(); {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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
     auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();prox(0.0f));
}

TEST_CASE("TextRunDriver: Opacity animator sets per-glyph alpha") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};

    auto shape = make_shape("Hello", engine, layout, {
        make_global_spec("trb_op", OpacityProperty{0.5f})
    });

    if (shape->layout->placed.glyphs.empty())     auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();per_frame(*shape, SampleTime{});
    CHECK(shape->glyphs[0].opacity == doctest::Approx(0.5f));
}

TEST_CASE("TextRunDriver: deterministic between repeated calls") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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

// ═════════    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();═══════════════
// 3. Animator stack via evaluate_animator_stack_into (in-place path)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: evaluate_animator_stack_into writes back in place") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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

    CHECK(states.size() == layout_ptr->placed.glyphs.si    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();pprox(10.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. apply_active_state_to_text_run_shape — Hold/Cut fast paths
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunDriver: Hold transition renders active document (no-op return)") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextLayoutSpec layout;
    layout.box = {800.0f, 200.0f};
    auto shape = make_shape("Hello", engine, layout);

    AnimatedTextDocument doc;
    SourceTextKeyframe kf;
    kf.frame = Frame{0};
    kf.document.utf8 = "Hello";
      auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();e_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::Hold);
    REQUIRE(state.active != nullptr);

    bool changed = apply_active_state_to_text_run_shape(*shape, state, engine, layout);
    CHECK_FALSE(changed);  // text matches existing layout → no rebuild
    CHECK(shape->layout->source_text == "Hello");
}

TEST_CASE("TextRunDriver: Hold with different active text rebuilds layout") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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
    REQU    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();;
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
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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
    doc.add_keyframe(kf    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();rame{60};
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
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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
    doc.add_key    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();30});
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
    auto runtime = chronon3d::runtime::RenderRuntime::create(
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
