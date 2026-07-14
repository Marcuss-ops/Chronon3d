// ═══════════════════════════════════════════════════════════════════════════
// test_text_run_builder_animated_doc.cpp — PR 9 wiring tests
//
// Verifies that:
//   1. Static path (no animated_doc) preserves the initial spec text.
//   2. With an AnimatedTextDocument attached, the materializer routes
//      ActiveTextState through apply_active_state_to_text_run_shape so
//      shape->layout->source_text matches sample_at(integral_frame).
//   3. TextRunBuilder::from_animated_document binds the
//      shared_ptr<const AnimatedTextDocument> into PendingTextRun.
//   4. PendingTextRun.animated_doc defaults to nullptr when not set.
// ═══════════════════════════════════════════════════════════════════════════

#include <optional>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/text_run.hpp>
#include <doctest/doctest.h>

#include <memory>
#include <utility>

using namespace chronon3d;

namespace {

// ── Helpers ────────────────────────────────────────────────────────────────

AnimatedTextDocument make_two_keyframe_doc(const std::string& text_a,
                                           const std::string& text_b) {
    AnimatedTextDocument doc;
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = text_a;
    doc.add_keyframe(kf0);

    SourceTextKeyframe kf_scr;
    kf_scr.frame = Frame{60};
    kf_scr.transition = SourceTextTransition::Scramble;
    kf_scr.document.utf8 = text_b;
    doc.add_keyframe(kf_scr);
    return doc;
}

TextRunSpec make_spec(const std::string& literal_text) {
    TextRunSpec spec;
    spec.text.content.value = literal_text;
    spec.text.font.font_family = "DejaVu Sans";
    spec.text.font.font_size   = 32.0f;
    spec.text.layout.box       = {800.0f, 200.0f};
    return spec;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Static path — no animated_doc: spec.text.content.value is preserved.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder+PendingDoc: static path keeps initial text") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    TextRunSpec spec = make_spec("Static initial text");

    auto shape = materialize_text_run_shape(
        spec, &engine, SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
        /*animated_doc=*/nullptr);

    REQUIRE(shape);
    REQUIRE(shape->layout);
    CHECK(shape->layout->source_text == "Static initial text");
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. AnimatedTextDocument drives the layout at sample_at(integral_frame)
//    AND at every per-frame driver invocation with a different sample_time.
//    This is the core wiring PR 9 adds — verifies both (a) the build-time
//    materialization applies the doc, and (b) the per-frame driver can
//    re-sample and re-apply when called with a different SampleTime.
//
// Font-independent: the asserted invariants are purely on
// `shape->layout->source_text` (a string compare), not glyph geometry.
// Runs cleanly even on CI machines without DejaVu Sans installed.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder+PendingDoc: animat    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value(); {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
        chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    AnimatedTextDocument doc;
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Apple";
    doc.add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.transition = SourceTextTransition::Scramble;
    kf60.document.utf8 = "Orange";
    doc.add_keyframe(kf60);
    auto shared_doc = std::make_shared<const AnimatedTextDocument>(std::move(doc));

    TextRunSpec spec = make_spec("INITIAL_PLACEHOLDER_NOT_USED");

    // Materialize at frame 0 — materializer routes the doc, layout carries
    // the active document's text "Apple".
    {
        auto shape = materialize_text_run_shape(
            spec, &engine, SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
            shared_doc);
        REQUIRE(shape);
        REQUIRE(shape->layout);
        CHECK(shape->layout->source_text == "Apple");

        // Drive the per-frame path (PR 8 driver) at frame 30 — at that
        // moment the doc is mid-Scramble.  The driver re-samples the
        // doc, calls apply_active_state_to_text_run_shape, swaps the
        // layout's source_text to the Scramble's transition_text.
        const auto sampled = shared_doc->sample_at(Frame{30});
        REQUIRE(sampled.transition == SourceTextTransition::Scramble);
        REQUIRE_FALSE(sampled.transition_text.empty());

        chronon3d::update_text_run_shape_per_frame(
            *shape, SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1}));
        REQUIRE(shape->layout);
        // The layout's source_text must equal the per-frame transition_text.
        CHECK(shape->layout->source_text == sampled.transition_text);

        // Drive again at frame 60 — past the gap, the doc's active=>"Orange".
        chronon3d::update_text_run_shape_per_frame(
            *shape, SampleTime::from_frame_int(Frame{60}, FrameRate{30, 1}));
        REQUIRE(shape->layout);
        CHECK(shape->layout->source_text == "Orange");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. TextRunBuilder::from_animated_document binds the doc into PendingTextRun.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: from_animated_document binds into PendingTextRun") {
    auto doc = make_two_keyframe_doc("Bind A", "Bind B");
    auto doc_ptr = std::make_shared<const AnimatedTextDocument>(std::move(doc));

    LayerBuilder layer("animated_layer", Frame{30});
    auto& trb = layer.text_run("doc_bound", TextRunSpec{});
    trb.from_animated_document(doc_ptr);

    const auto& pending = trb.build_spec();
    REQUIRE(static_cast<bool>(pending.animated_doc));
    CHECK(pending.animated_doc.get() == doc_ptr.get());
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Without from_animated_document, PendingTextRun.animated_doc is null.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: animated_doc defaults to nullptr (no binding)") {
    LayerBuilder layer("plain", Frame{0});
    auto& trb = layer.text_run("plain_text", TextRunSpec{});
    (void)trb.font_size(48.0f).opacity(1.0f);

    const auto& pending = trb.build_spec();
    CHECK_FALSE(static_cast<bool>(pending.animated_doc));
}
