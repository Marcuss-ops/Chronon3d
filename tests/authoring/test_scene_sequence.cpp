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
    context = context.with_frame(cf);
    context = context.with_frame_rate(chronon3d::FrameRate{30, 1});
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

TEST_CASE("Authoring / Scene::series / stacks sequences with cumulative from") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{0})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<int> layer_counts;
    scene.series("main")
        .add("intro", chronon3d::Frame{30},
             [&](chronon3d::SequenceBuilder& seq) {
                 seq.layer("intro_layer", [](chronon3d::LayerBuilder& layer) {
                     layer.rect("r", {.size = {10.0f, 10.0f},
                                      .color = chronon3d::Color::white()});
                 });
             })
        .add("body", chronon3d::Frame{60},
             [&](chronon3d::SequenceBuilder& seq) {
                 seq.layer("body_layer", [](chronon3d::LayerBuilder& layer) {
                     layer.rect("r", {.size = {20.0f, 20.0f},
                                      .color = chronon3d::Color::white()});
                 });
             })
        .add("outro", chronon3d::Frame{45},
             [&](chronon3d::SequenceBuilder& seq) {
                 seq.layer("outro_layer", [](chronon3d::LayerBuilder& layer) {
                     layer.rect("r", {.size = {30.0f, 30.0f},
                                      .color = chronon3d::Color::white()});
                 });
             });

    // Evaluate at the middle of each segment.
    {
        chronon3d::SceneBuilder eval_builder{engine_ctx(chronon3d::Frame{15})};
        chronon3d::authoring::Scene eval_scene{eval_builder, author_canvas()};
        eval_builder = std::move(builder);
        // Rebuild not needed; layers are already in eval_builder's scene.
        // Instead, build directly from the original builder.
    }

    // Build at frame 15 -> only intro_layer active.
    {
        chronon3d::SceneBuilder b{engine_ctx(chronon3d::Frame{15})};
        chronon3d::authoring::Scene s{b, author_canvas()};
        s.series("main")
            .add("intro", chronon3d::Frame{30},
                 [](chronon3d::SequenceBuilder& seq) {
                     seq.layer("intro_layer", [](chronon3d::LayerBuilder& layer) {
                         layer.rect("r", {.size = {10.0f, 10.0f},
                                          .color = chronon3d::Color::white()});
                     });
                 })
            .add("body", chronon3d::Frame{60},
                 [](chronon3d::SequenceBuilder& seq) {
                     seq.layer("body_layer", [](chronon3d::LayerBuilder& layer) {
                         layer.rect("r", {.size = {20.0f, 20.0f},
                                          .color = chronon3d::Color::white()});
                     });
                 });
        chronon3d::Scene evaluated = b.build();
        REQUIRE(evaluated.layers().size() == 1);
        CHECK(evaluated.layers()[0].name == "intro_layer");
    }

    // Build at frame 45 -> body_layer active (intro ended at 30).
    {
        chronon3d::SceneBuilder b{engine_ctx(chronon3d::Frame{45})};
        chronon3d::authoring::Scene s{b, author_canvas()};
        s.series("main")
            .add("intro", chronon3d::Frame{30},
                 [](chronon3d::SequenceBuilder& seq) {
                     seq.layer("intro_layer", [](chronon3d::LayerBuilder& layer) {
                         layer.rect("r", {.size = {10.0f, 10.0f},
                                          .color = chronon3d::Color::white()});
                     });
                 })
            .add("body", chronon3d::Frame{60},
                 [](chronon3d::SequenceBuilder& seq) {
                     seq.layer("body_layer", [](chronon3d::LayerBuilder& layer) {
                         layer.rect("r", {.size = {20.0f, 20.0f},
                                          .color = chronon3d::Color::white()});
                     });
                 });
        chronon3d::Scene evaluated = b.build();
        REQUIRE(evaluated.layers().size() == 1);
        CHECK(evaluated.layers()[0].name == "body_layer");
    }
}

TEST_CASE("Authoring / Scene::sequence / premount and postmount extend active range") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{5})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    scene.sequence(
        "with_mounts",
        {.from = chronon3d::Frame{10},
         .duration = chronon3d::Frame{20},
         .premount = chronon3d::Frame{5},
         .postmount = chronon3d::Frame{5}},
        [](chronon3d::SequenceBuilder& seq) {
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    chronon3d::Scene evaluated = builder.build();
    CHECK(evaluated.layers().size() == 1);
}

TEST_CASE("Authoring / Scene::sequence / loop_duration repeats content") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{40})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<chronon3d::Frame> captured_locals;
    scene.sequence(
        "looped",
        {.from = chronon3d::Frame{0},
         .duration = chronon3d::Frame{60},
         .loop_duration = chronon3d::Frame{30}},
        [&](chronon3d::SequenceBuilder& seq) {
            captured_locals.push_back(seq.local_frame());
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    REQUIRE(captured_locals.size() == 1);
    CHECK(captured_locals[0] == chronon3d::Frame{10}); // 40 % 30 == 10
}

TEST_CASE("Authoring / Scene::sequence / freeze_at locks local frame") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{50})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<chronon3d::Frame> captured_locals;
    scene.sequence(
        "frozen",
        {.from = chronon3d::Frame{0},
         .duration = chronon3d::Frame{60},
         .freeze_at = chronon3d::Frame{7}},
        [&](chronon3d::SequenceBuilder& seq) {
            captured_locals.push_back(seq.local_frame());
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    REQUIRE(captured_locals.size() == 1);
    CHECK(captured_locals[0] == chronon3d::Frame{7});
}

TEST_CASE("Authoring / Scene::sequence / trim_after extends active duration") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{75})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    scene.sequence(
        "trimmed",
        {.from = chronon3d::Frame{0},
         .duration = chronon3d::Frame{60},
         .trim_after = chronon3d::Frame{30}},
        [](chronon3d::SequenceBuilder& seq) {
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    chronon3d::Scene evaluated = builder.build();
    CHECK(evaluated.layers().size() == 1);
}

TEST_CASE("Authoring / Scene::sequence / freeze_at wins over loop_duration") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{40})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<chronon3d::Frame> captured_locals;
    scene.sequence(
        "frozen_loop",
        {.from = chronon3d::Frame{0},
         .duration = chronon3d::Frame{60},
         .freeze_at = chronon3d::Frame{7},
         .loop_duration = chronon3d::Frame{30}},
        [&](chronon3d::SequenceBuilder& seq) {
            captured_locals.push_back(seq.local_frame());
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    REQUIRE(captured_locals.size() == 1);
    // Without freeze, local would be 40 % 30 == 10. Freeze locks it at 7.
    CHECK(captured_locals[0] == chronon3d::Frame{7});
}namespace {

chronon3d::SceneBuilder::SequenceSpec make_extended_spec() {
    return {.from = chronon3d::Frame{10},
            .duration = chronon3d::Frame{20},
            .trim_after = chronon3d::Frame{15},
            .premount = chronon3d::Frame{5}};
}

} // namespace

TEST_CASE("Authoring / Scene::sequence / premount and trim_after extend active range both ways") {
    // Frame 3 is before the premount window -> inactive.
    {
        chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{3})};
        chronon3d::authoring::Scene scene{builder, author_canvas()};
        scene.sequence(
            "extended",
            make_extended_spec(),
            [](chronon3d::SequenceBuilder& seq) {
                seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                    layer.rect("r", {.size = {10.0f, 10.0f},
                                     .color = chronon3d::Color::white()});
                });
            });
        chronon3d::Scene evaluated = builder.build();
        CHECK(evaluated.layers().empty());
    }

    // Frame 5 is inside the premount -> active, clamped to first local frame.
    {
        chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{5})};
        chronon3d::authoring::Scene scene{builder, author_canvas()};
        std::vector<chronon3d::Frame> captured_locals;
        scene.sequence(
            "extended",
            make_extended_spec(),
            [&](chronon3d::SequenceBuilder& seq) {
                captured_locals.push_back(seq.local_frame());
                seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                    layer.rect("r", {.size = {10.0f, 10.0f},
                                     .color = chronon3d::Color::white()});
                });
            });
        chronon3d::Scene evaluated = builder.build();
        REQUIRE(evaluated.layers().size() == 1);
        REQUIRE(captured_locals.size() == 1);
        CHECK(captured_locals[0] == chronon3d::Frame{0});
    }

    // Frame 44 is inside the trim_after extension -> active, clamped to last local frame.
    {
        chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{44})};
        chronon3d::authoring::Scene scene{builder, author_canvas()};
        std::vector<chronon3d::Frame> captured_locals;
        scene.sequence(
            "extended",
            make_extended_spec(),
            [&](chronon3d::SequenceBuilder& seq) {
                captured_locals.push_back(seq.local_frame());
                seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                    layer.rect("r", {.size = {10.0f, 10.0f},
                                     .color = chronon3d::Color::white()});
                });
            });
        chronon3d::Scene evaluated = builder.build();
        REQUIRE(evaluated.layers().size() == 1);
        REQUIRE(captured_locals.size() == 1);
        CHECK(captured_locals[0] == chronon3d::Frame{19});
    }
}

TEST_CASE("Authoring / Scene::sequence / loop_duration applied after trim_before") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{40})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<chronon3d::Frame> captured_locals;
    scene.sequence(
        "looped_trimmed",
        {.from = chronon3d::Frame{0},
         .duration = chronon3d::Frame{60},
         .trim_before = chronon3d::Frame{5},
         .loop_duration = chronon3d::Frame{30}},
        [&](chronon3d::SequenceBuilder& seq) {
            captured_locals.push_back(seq.local_frame());
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    REQUIRE(captured_locals.size() == 1);
    // 40 % 30 == 10, then +5 from trim_before.
    CHECK(captured_locals[0] == chronon3d::Frame{15});
}

TEST_CASE("Authoring / Scene::sequence / freeze_at combined with trim_before") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{50})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<chronon3d::Frame> captured_locals;
    scene.sequence(
        "frozen_trimmed",
        {.from = chronon3d::Frame{0},
         .duration = chronon3d::Frame{60},
         .trim_before = chronon3d::Frame{3},
         .freeze_at = chronon3d::Frame{7}},
        [&](chronon3d::SequenceBuilder& seq) {
            captured_locals.push_back(seq.local_frame());
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    REQUIRE(captured_locals.size() == 1);
    CHECK(captured_locals[0] == chronon3d::Frame{10});
}

TEST_CASE("Authoring / Scene::sequence / freeze_at applies during premount") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{5})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<chronon3d::Frame> captured_locals;
    scene.sequence(
        "frozen_premount",
        {.from = chronon3d::Frame{10},
         .duration = chronon3d::Frame{20},
         .freeze_at = chronon3d::Frame{3},
         .premount = chronon3d::Frame{5}},
        [&](chronon3d::SequenceBuilder& seq) {
            captured_locals.push_back(seq.local_frame());
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    REQUIRE(captured_locals.size() == 1);
    // Without freeze, premount would clamp local to 0. Freeze locks it at 3.
    CHECK(captured_locals[0] == chronon3d::Frame{3});
}

TEST_CASE("Authoring / Scene::sequence / loop_duration applies after postmount clamp") {
    chronon3d::SceneBuilder builder{engine_ctx(chronon3d::Frame{25})};
    chronon3d::authoring::Scene scene{builder, author_canvas()};

    std::vector<chronon3d::Frame> captured_locals;
    scene.sequence(
        "looped_postmount",
        {.from = chronon3d::Frame{0},
         .duration = chronon3d::Frame{20},
         .loop_duration = chronon3d::Frame{15},
         .postmount = chronon3d::Frame{10}},
        [&](chronon3d::SequenceBuilder& seq) {
            captured_locals.push_back(seq.local_frame());
            seq.layer("layer", [](chronon3d::LayerBuilder& layer) {
                layer.rect("r", {.size = {10.0f, 10.0f},
                                 .color = chronon3d::Color::white()});
            });
        });

    REQUIRE(captured_locals.size() == 1);
    // Postmount clamps local to duration-1 (19), then loop gives 19 % 15 == 4.
    CHECK(captured_locals[0] == chronon3d::Frame{4});
}
