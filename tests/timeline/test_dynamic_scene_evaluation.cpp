// SPDX-License-Identifier: MIT
//
// Regression coverage for the compiled-scene template cache. SceneFunction is
// a per-frame API unless the definition explicitly promises frame invariance.

#include <doctest/doctest.h>

#include <chronon3d/timeline/compile_evaluate.hpp>

#include <memory>
#include <stdexcept>
#include <utility>

using namespace chronon3d;

namespace {

CompositionDefinition make_definition(CompositionDefinition::SceneFunction scene) {
    CompositionDefinition definition;
    definition.composition.name = "dynamic-scene-evaluation";
    definition.composition.width = 64;
    definition.composition.height = 64;
    definition.composition.frame_rate = FrameRate{30, 1};
    definition.composition.duration = Frame{120};
    definition.scene = std::move(scene);
    definition.scene_content_fingerprint = 0xD1A6u;
    return definition;
}

CompositionEvaluateContext make_evaluate_context() {
    return CompositionEvaluateContext{
        .frame_context = make_frame_context(FrameContextParams{
            .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
            .duration = Frame{120},
            .width = 64,
            .height = 64,
        })
    };
}

CompiledComposition compile_or_throw(const CompositionDefinition& definition) {
    auto compiled = compile_composition(definition, CompositionCompileContext{});
    if (!compiled.has_value()) {
        throw std::runtime_error(
            "dynamic scene regression fixture failed to compile: " +
            compiled.error().message);
    }
    return std::move(compiled).value();
}

} // namespace

TEST_CASE("DynamicTopologyByFrame") {
    auto definition = make_definition([](const FrameContext& ctx) {
        Scene scene(ctx.resource);

        Layer base(ctx.resource);
        base.name = "base";
        scene.add_layer(std::move(base));

        if (ctx.frame() >= Frame{50}) {
            Layer late(ctx.resource);
            late.name = "late";
            scene.add_layer(std::move(late));
        }
        return scene;
    });

    auto compiled = compile_or_throw(definition);
    const auto context = make_evaluate_context();

    auto frame0 = evaluate(compiled, context, Frame{0});
    auto frame60 = evaluate(compiled, context, Frame{60});
    REQUIRE(frame0.has_value());
    REQUIRE(frame60.has_value());
    CHECK(frame0->scene.layers().size() == 1);
    CHECK(frame60->scene.layers().size() == 2);
}

TEST_CASE("DynamicTextContentByFrame") {
    auto definition = make_definition([](const FrameContext& ctx) {
        Scene scene(ctx.resource);
        Layer text(ctx.resource);
        text.kind = LayerKind::Text;
        // Keep this unit test independent from FontEngine: the PMR string is
        // the frame-varying authored payload that catches a frozen Scene clone.
        text.name = ctx.frame() == Frame{7} ? "frame-7" : "frame-other";
        scene.add_layer(std::move(text));
        return scene;
    });

    auto compiled = compile_or_throw(definition);
    const auto context = make_evaluate_context();

    auto frame7 = evaluate(compiled, context, Frame{7});
    auto frame8 = evaluate(compiled, context, Frame{8});
    REQUIRE(frame7.has_value());
    REQUIRE(frame8.has_value());
    REQUIRE(frame7->scene.layers().size() == 1);
    REQUIRE(frame8->scene.layers().size() == 1);
    CHECK(frame7->scene.layers().front().kind == LayerKind::Text);
    CHECK(frame8->scene.layers().front().kind == LayerKind::Text);
    CHECK(frame7->scene.layers().front().name == "frame-7");
    CHECK(frame8->scene.layers().front().name == "frame-other");
}

TEST_CASE("ConditionalLayerByFrame") {
    auto definition = make_definition([](const FrameContext& ctx) {
        Scene scene(ctx.resource);
        Layer selected(ctx.resource);
        selected.name = ctx.frame() < Frame{50} ? "A" : "B";
        scene.add_layer(std::move(selected));
        return scene;
    });

    auto compiled = compile_or_throw(definition);
    const auto context = make_evaluate_context();

    auto before = evaluate(compiled, context, Frame{49});
    auto after = evaluate(compiled, context, Frame{50});
    REQUIRE(before.has_value());
    REQUIRE(after.has_value());
    CHECK(before->scene.layers().front().name == "A");
    CHECK(after->scene.layers().front().name == "B");
}

TEST_CASE("FrameInvariantSceneTemplateRequiresExplicitOptIn") {
    auto calls = std::make_shared<int>(0);
    auto definition = make_definition([calls](const FrameContext& ctx) {
        ++(*calls);
        Scene scene(ctx.resource);
        Layer layer(ctx.resource);
        layer.name = "static";
        scene.add_layer(std::move(layer));
        return scene;
    });
    definition.scene_is_frame_invariant = true;

    auto compiled = compile_or_throw(definition);
    const auto context = make_evaluate_context();

    auto first = evaluate(compiled, context, Frame{1});
    auto second = evaluate(compiled, context, Frame{90});
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*calls == 1);
    CHECK(first->scene.layers().front().name == "static");
    CHECK(second->scene.layers().front().name == "static");
}
