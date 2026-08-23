// SPDX-License-Identifier: MIT
//
// Dynamic scene evaluation tests: ensures procedural / dynamic SceneFunctions
// depending on FrameContext (conditional layers, dynamic topology, frame-dependent content)
// are correctly re-evaluated without stale template caching.

#include <doctest/doctest.h>

#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_definition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <string>

using namespace chronon3d;

TEST_CASE("DynamicTopologyByFrame") {
    CompositionSpec spec;
    spec.name = "dynamic-topo";
    spec.width = 1920;
    spec.height = 1080;
    spec.frame_rate = FrameRate{30, 1};
    spec.duration = Frame{100};

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder sb;
        sb.layer("base", [](auto& l) {
            l.rect("r1", RectParams{Vec2{100.0f, 100.0f}});
        });
        if (ctx.frame() >= Frame{50}) {
            sb.layer("extra", [](auto& l) {
                l.rect("r2", RectParams{Vec2{50.0f, 50.0f}});
            });
        }
        return sb.build();
    });

    auto compiled_res = compile_composition(comp, CompositionCompileContext{});
    REQUIRE(compiled_res.has_value());
    const auto& compiled = compiled_res.value();
    CHECK_FALSE(compiled.is_static_topology);

    CompositionEvaluateContext eval_ctx;
    eval_ctx.frame_context = make_frame_context({
        .duration = spec.duration,
        .width = spec.width,
        .height = spec.height,
    });

    auto frame10 = evaluate(compiled, eval_ctx, Frame{10});
    REQUIRE(frame10.has_value());
    CHECK(frame10.value().scene.layers().size() == 1);

    auto frame60 = evaluate(compiled, eval_ctx, Frame{60});
    REQUIRE(frame60.has_value());
    CHECK(frame60.value().scene.layers().size() == 2);
}

TEST_CASE("DynamicTextContentByFrame") {
    CompositionSpec spec;
    spec.name = "dynamic-content";
    spec.width = 1920;
    spec.height = 1080;
    spec.frame_rate = FrameRate{30, 1};
    spec.duration = Frame{100};

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder sb;
        sb.layer("dyn_layer", [&](auto& l) {
            l.rect("r", RectParams{Vec2{static_cast<float>(ctx.frame()) * 10.0f, 50.0f}});
        });
        return sb.build();
    });

    auto compiled_res = compile_composition(comp, CompositionCompileContext{});
    REQUIRE(compiled_res.has_value());
    const auto& compiled = compiled_res.value();
    CHECK_FALSE(compiled.is_static_topology);

    CompositionEvaluateContext eval_ctx;
    eval_ctx.frame_context = make_frame_context({
        .duration = spec.duration,
        .width = spec.width,
        .height = spec.height,
    });

    auto frame5 = evaluate(compiled, eval_ctx, Frame{5});
    REQUIRE(frame5.has_value());
    REQUIRE(!frame5.value().scene.layers().empty());
    REQUIRE(!frame5.value().scene.layers()[0].nodes.empty());
    CHECK(frame5.value().scene.layers()[0].nodes[0].shape.rect().size.x == 50.0f);

    auto frame25 = evaluate(compiled, eval_ctx, Frame{25});
    REQUIRE(frame25.has_value());
    REQUIRE(!frame25.value().scene.layers().empty());
    REQUIRE(!frame25.value().scene.layers()[0].nodes.empty());
    CHECK(frame25.value().scene.layers()[0].nodes[0].shape.rect().size.x == 250.0f);
}

TEST_CASE("ConditionalLayerByFrame") {
    CompositionSpec spec;
    spec.name = "conditional-layer";
    spec.width = 1920;
    spec.height = 1080;
    spec.frame_rate = FrameRate{30, 1};
    spec.duration = Frame{100};

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder sb;
        if (static_cast<int>(ctx.frame()) % 2 == 0) {
            sb.layer("even_layer", [](auto& l) {
                l.rect("r", RectParams{Vec2{10.0f, 10.0f}});
            });
        } else {
            sb.layer("odd_layer", [](auto& l) {
                l.circle("c", CircleParams{10.0f});
            });
        }
        return sb.build();
    });

    auto compiled_res = compile_composition(comp, CompositionCompileContext{});
    REQUIRE(compiled_res.has_value());
    const auto& compiled = compiled_res.value();
    CHECK_FALSE(compiled.is_static_topology);

    CompositionEvaluateContext eval_ctx;
    eval_ctx.frame_context = make_frame_context({
        .duration = spec.duration,
        .width = spec.width,
        .height = spec.height,
    });

    auto f0 = evaluate(compiled, eval_ctx, Frame{0});
    REQUIRE(f0.has_value());
    REQUIRE(!f0.value().scene.layers().empty());
    CHECK(f0.value().scene.layers()[0].name == "even_layer");

    auto f1 = evaluate(compiled, eval_ctx, Frame{1});
    REQUIRE(f1.has_value());
    REQUIRE(!f1.value().scene.layers().empty());
    CHECK(f1.value().scene.layers()[0].name == "odd_layer");
}
