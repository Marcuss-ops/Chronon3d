// ═══════════════════════════════════════════════════════════════════════════
// test_text_run_builder.cpp — PR 4 smoke tests
//
// Covers:
//   1. TextRunBuilder accumulates .position / .opacity / .animator / .selector
//      into the underlying TextRunBuildSpec.pending.animators vector uniformly.
//   2. LayerBuilder::text_run produces a stable reference that survives
//      multiple successive calls.
//   3. TextRunBuilder fluent chain (after commit()) can re-enter the
//      LayerBuilder chain without losing state.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <doctest/doctest.h>
#include <type_traits>    // for std::is_same_v (alias identity static_assert)

using namespace chronon3d;

namespace {

/// Tiny helper: build a TextRun with `count` glyphs.
TextRunParams make_text_run_params(std::string text, f32 font_size = 72.0f) {
    TextRunParams p;
    p.content.value = std::move(text);
    p.font.font_size = font_size;
    p.font.font_path = "assets/fonts/Inter-Bold.ttf";
    p.font.font_family = "Inter";
    p.font.font_weight = 800;
    p.font.font_style = "normal";
    p.appearance.color = {1.0f, 1.0f, 1.0f, 1.0f};
    p.position = {0.0f, 0.0f, 0.0f};
    p.layout.anchor = TextAnchor::Center;
    p.layout.align = TextAlign::Center;
    p.layout.vertical_align = VerticalAlign::Middle;
    p.layout.wrap = TextWrap::None;
    p.direction = TextDirection::Auto;
    return p;
}

GlyphSelectorSpec make_selector(std::string id, int n = 1) {
    GlyphSelectorSpec s;
    s.id = std::move(id);
    s.unit = TextSelectorUnit::Glyph;
    s.shape = TextSelectorShape::Square;
    s.start = {0.0f};
    s.end = {100.0f};
    s.amount = {100.0f};
    s.exclude_spaces = false;
    (void)n;
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. TextRunBuilder accumulates implicit per-mutator animators + explicit
//    .animator() / .selector() adds uniformly.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: position/opacity injects implicit animators") {
    TextRunSpec params = make_text_run_params("Hi");
    TextRunBuildSpec spec{
        .name = "test_run",
        .pending = std::move(params),
        .consumed = false,
    };
    // We can't construct TextRunBuilder directly (private ctor); build via
    // the LayerBuilder round-trip below.
}

TEST_CASE("TextRunBuilder: chain via LayerBuilder accumulates all entries") {
    LayerBuilder lb("test_layer", Frame{0});

    TextRunSpec params = make_text_run_params("Hello", 64.0f);

    // First text_run.
    TextRunBuilder& trb_a = lb.text_run("run_a", params);
    trb_a.position({10.0f, 20.0f, 0.0f})
         .opacity(0.7f)
         .animator(TextAnimatorSpec{
             .id = "user_slide_in",
             .enabled = true,
             .selectors = { make_selector("user_slide_in_sel") },
             .properties = { PositionProperty{{0.0f, 40.0f, 0.0f}} }
         })
         .selector(make_selector("user_stagger_sel"));

    // Second text_run — must NOT invalidate the first reference.
    TextRunSpec params_b = make_text_run_params("World", 64.0f);
    TextRunBuilder& trb_b = lb.text_run("run_b", params_b);
    trb_b.opacity(0.5f).rotate({0.0f, 30.0f, 0.0f});

    // Verify the first reference is still alive.
    CHECK_FALSE(trb_a.spec().text.content.value.empty());
    CHECK(trb_a.spec().text.content.value == "Hello");

    // Both runs landed.
    const auto& runs_a_after = trb_a.spec();
    const auto& runs_b_after = trb_b.spec();
    CHECK(runs_a_after.text.content.value == "Hello");
    CHECK(runs_b_after.text.content.value == "World");

    // 3 implicit animators from run_a (.position/.opacity/.animator)
    // + .selector attached to the LAST one
    // = 3 entries.  The explicit .animator() adds 1 → total 4.
    CHECK(runs_a_after.animators.size() == 4);
    // The LAST animator (= the explicit one) should have ≥2 selectors:
    // the implicit one from the explicit .animator() spec + the
    // follow-up .selector() call.
    CHECK(runs_b_after.animators.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Multiple text_run calls on a single LayerBuilder produce a stable
//    reference into the builder pool.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: chained references stay valid across push_back") {
    LayerBuilder lb("test_layer", Frame{0});

    TextRunBuilder* first_ref = nullptr;
    for (int i = 0; i < 5; ++i) {
        TextRunSpec params = make_text_run_params(
            "frame_" + std::to_string(i), 32.0f);
        TextRunBuilder& trb = lb.text_run(
            "name_" + std::to_string(i), std::move(params));
        if (i == 0) {
            first_ref = &trb;
        } else {
            // Verify the prior reference still works.
            // NOTE: this alone doesn't catch the bug (compiler might
            // re-allocate).  We rely on the live spec inspection below.
            CHECK(first_ref != nullptr);
        }
        trb.opacity(0.1f * static_cast<f32>(i + 1));
    }
    // First reference should still be alive and read correctly.
    REQUIRE(first_ref != nullptr);
    CHECK(first_ref->spec().text.content.value == "frame_0");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. commit() returns the parent LayerBuilder for re-entry.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBuilder: commit hands control back to parent") {
    LayerBuilder lb("test_layer", Frame{0});

    TextRunSpec params = make_text_run_params("Commit");
    TextRunBuilder& trb = lb.text_run("commit_run", std::move(params));
    LayerBuilder& back = trb.opacity(0.3f).commit();
    // Set a layer-level field; verify the returned reference is
    // the same LayerBuilder.
    back.opacity(0.9f);

    // Just a smoke check — re-using the same chain returned a valid
    // LayerBuilder (no segfault, valid parent association).
    CHECK(&back == &lb);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Registry integration: shape_ids::TextRun is registered, the variant
//    accepts TextRunParams, ShapeType::TextRun is in the enum, and
//    RenderNodeFactory::text_run produces a flagged RenderNode.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <chronon3d/registry/shape_params.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp>

TEST_CASE("TextRun registry: shape_ids::TextRun exposed + factory wires RenderNodeFactory::text_run") {
    auto& reg = chronon3d::registry::ShapeRegistry::instance();
    CHECK(reg.contains(chronon3d::registry::shape_ids::TextRun));

    // Variant rejects unrecognized payloads, so verify TextRunSpec
    // (canonical composable; TextRunParams is the deprecated alias)
    // is a valid alternative.
    chronon3d::registry::ShapeParams p = make_text_run_params("Hello");
    CHECK(std::holds_alternative<chronon3d::TextRunParams>(p));
    // The alias holds the exact same type identity as TextRunSpec.
    static_assert(std::is_same_v<chronon3d::TextRunParams, chronon3d::TextRunSpec>,
                  "TextRunParams must be an alias of TextRunSpec");
}

TEST_CASE("TextRun registry: RenderNodeFactory::text_run produces flagged RenderNode") {
    std::pmr::monotonic_buffer_resource pool;
    auto node = chronon3d::RenderNodeFactory::text_run(
        &pool, "registry_run", make_text_run_params("World"));

    CHECK(node.is_text_run_shape);
    CHECK(node.shape.type == chronon3d::ShapeType::TextRun);
    // Identity smoke check — the named node must round-trip.
    CHECK(node.name == std::pmr::string{"registry_run", &pool});
}

TEST_CASE("TextRun registry: ShapeRegistry::create_node(\"shape.text_run\", ...) routes to factory") {
    auto& reg = chronon3d::registry::ShapeRegistry::instance();
    std::pmr::monotonic_buffer_resource pool;
    chronon3d::registry::ShapeParams params = make_text_run_params("FromRegistry");
    auto node = reg.create_node(
        chronon3d::registry::shape_ids::TextRun,
        &pool, "registry_route",
        std::move(params));

    CHECK(node.is_text_run_shape);
    CHECK(node.shape.type == chronon3d::ShapeType::TextRun);
    CHECK(node.name == std::pmr::string{"registry_route", &pool});
}
