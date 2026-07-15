// ═══════════════════════════════════════════════════════════════════════════
// Authoring DSL — Scene::sequence forwarder tests
//
// The facade routes through SceneBuilder::sequence -> compile_sequence().
// Tests cover the canonical SequenceBuilder surface, nested local frames,
// inactive spatial suppression, fluent chaining, and move-only semantics.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/sequence_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/authoring/scene.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

namespace {

chronon3d::FrameContext engine_ctx(chronon3d::Frame cf = chronon3d::Frame{0}) {
    chronon3d::FrameContext context;
    context.frame      = cf;
    context.frame_rate = chronon3d::FrameRate{30, 1};
    context.width      = 1920;
    context.height     = 1080;
    return context;
}

chronon3d::CanvasInfo author_canvas(float width = 1920.0f,
                                    float height = 1080.0f) {
    return chronon3d::CanvasInfo::with_safe_area(
        width, height, chronon3d::SafeAreaPreset{});
}

} // namespace

TEST_CASE("Authoring / Scene::sequence / SequenceBuilder receives correct local_frame") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{0})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    bool invoked = false;
    chronon3d::Frame captured_local{999};
    int layer_count_inside_sequence = 0;

    scene.sequence(
        "intro",
        {.from = chronon3d::Frame{0}, .duration = chronon3d::Frame{60}},
        [&](chronon3d::SequenceBuilder& sequence) {
            invoked = true;
            captured_local = sequence.local_frame();
            sequence.layer("title", [&](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {
                    .size = {100.0f, 100.0f},
                    .color = chronon3d::Color::white()
                });
                ++layer_count_inside_sequence;
            });
        });

    CHECK(invoked);
    CHECK(captured_local == chronon3d::Frame{0});
    CHECK(layer_count_inside_sequence == 1);

    chronon3d::Scene evaluated = builder.build();
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "title");
}

TEST_CASE("Authoring / Scene::sequence / returns Scene& for fluent chaining") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{0})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    chronon3d::authoring::Scene& first = scene.sequence(
        "a",
        {.from = chronon3d::Frame{0}, .duration = chronon3d::Frame{30}},
        [](chronon3d::SequenceBuilder&) {});
    chronon3d::authoring::Scene& second = scene.sequence(
        "b",
        {.from = chronon3d::Frame{30}, .duration = chronon3d::Frame{30}},
        [](chronon3d::SequenceBuilder&) {});

    CHECK(&first == &scene);
    CHECK(&second == &scene);
}

TEST_CASE("Authoring / Scene::sequence / nested local frame derives from parent") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{110})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    chronon3d::Frame captured_outer{999};
    chronon3d::Frame captured_inner{999};

    scene.sequence(
        "outer",
        {.from = chronon3d::Frame{100}, .duration = chronon3d::Frame{60}},
        [&](chronon3d::SequenceBuilder& outer) {
            captured_outer = outer.local_frame();
            outer.sequence(
                "inner",
                {.from = chronon3d::Frame{10}, .duration = chronon3d::Frame{40}},
                [&](chronon3d::SequenceBuilder& inner) {
                    captured_inner = inner.local_frame();
                    inner.layer("inner_layer", [](chronon3d::LayerBuilder& layer) {
                        layer.rect("r", {
                            .size = {20.0f, 20.0f},
                            .color = chronon3d::Color::white()
                        });
                    });
                });
        });

    CHECK(captured_outer == chronon3d::Frame{10});
    CHECK(captured_inner == chronon3d::Frame{0});

    chronon3d::Scene evaluated = builder.build();
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "inner_layer");
}

TEST_CASE("Authoring / Scene::sequence / inactive frame emits no spatial layers") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{200})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    bool lambda_invoked = false;
    scene.sequence(
        "inactive",
        {.from = chronon3d::Frame{100}, .duration = chronon3d::Frame{60}},
        [&](chronon3d::SequenceBuilder& sequence) {
            lambda_invoked = true;
            sequence.layer("hidden_layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {
                    .size = {50.0f, 50.0f},
                    .color = chronon3d::Color::white()
                });
            });
        });

    CHECK(lambda_invoked);
    chronon3d::Scene evaluated = builder.build();
    CHECK(evaluated.layers().empty());
}

TEST_CASE("Authoring / Scene is move-only") {
    CHECK(!std::is_copy_constructible_v<chronon3d::authoring::Scene>);
    CHECK(!std::is_copy_assignable_v<chronon3d::authoring::Scene>);
    CHECK(std::is_move_constructible_v<chronon3d::authoring::Scene>);
    CHECK(std::is_move_assignable_v<chronon3d::authoring::Scene>);
}
