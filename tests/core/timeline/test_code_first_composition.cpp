#include <doctest/doctest.h>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
using namespace chronon3d;


TEST_CASE("Composition foundation") {
    CompositionSpec spec{
        .name = "TestComp",
        .width = 1280,
        .height = 720,
        .frame_rate = {30, 1},
        .duration = 300
    };

    SUBCASE("Property retention") {
        Composition comp(spec, [](const FrameContext&){ return Scene{}; });
        CHECK(comp.name() == "TestComp");
        CHECK(comp.width() == 1280);
        CHECK(comp.height() == 720);
        CHECK(comp.duration() == 300);
        CHECK(comp.frame_rate().numerator == 30);
    }

    SUBCASE("Evaluation and context passing") {
        bool called = false;
        Composition comp(spec, [&](const FrameContext& ctx) {
            called = true;
            CHECK(ctx.frame() == Frame{150});
            CHECK(ctx.duration() == Frame{300});
            return Scene{};
        });

        auto scene = comp.evaluate(Frame{150});
        CHECK(called);
    }
}

TEST_CASE("Composition compatibility adapter snapshots the canonical pipeline") {
    CompositionSpec spec{
        .name = "AdapterComp",
        .width = 640,
        .height = 360,
        .frame_rate = {24, 1},
        .duration = 48
    };

    int callback_frame = -1;
    Composition comp(spec, [&callback_frame](const FrameContext& ctx) {
        callback_frame = static_cast<int>(ctx.frame());
        Scene scene;
        scene.set_assets_root(ctx.assets_root);
        return scene;
    });

    auto compiled = compile_composition(comp, {});
    REQUIRE(compiled.has_value());
    REQUIRE(compiled->definition);
    CHECK(compiled->definition->composition.name == comp.name());
    CHECK(compiled->definition->composition.width == comp.width());
    CHECK(compiled->definition->composition.height == comp.height());
    CHECK(compiled->definition->composition.frame_rate.numerator == 24);

    CompositionEvaluateContext eval_context;
    eval_context.frame_context = eval_context.frame_context.with_frame_rate(comp.frame_rate());
    eval_context.frame_context.width = comp.width();
    eval_context.frame_context.height = comp.height();

    auto evaluated = evaluate(compiled.value(), eval_context, Frame{12});
    REQUIRE(evaluated.has_value());
    CHECK(callback_frame == 12);
    CHECK_FALSE(evaluated->camera.has_value());
}

TEST_CASE("Composition compatibility adapter rejects a null scene callback") {
    CompositionSpec spec{
        .name = "NullScene",
        .width = 320,
        .height = 180,
        .frame_rate = {30, 1},
        .duration = 1
    };
    Composition comp(spec, Composition::SceneFunction{});

    auto compiled = compile_composition(comp, {});
    REQUIRE_FALSE(compiled.has_value());
    CHECK(compiled.error().kind == CompositionCompileError::Kind::NoSceneFunction);
}

TEST_CASE("Composition adapter preserves descriptor with precompiled camera precedence") {
    CompositionSpec spec{
        .name = "ProgramPrecedence",
        .width = 320,
        .height = 180,
        .frame_rate = {30, 1},
        .duration = 30
    };
    Composition comp(spec, [](const FrameContext&) { return Scene{}; });

    camera_v1::CameraDescriptor descriptor;
    descriptor.id = "adapter-descriptor";
    descriptor.source = camera_v1::StaticCameraSource{};
    descriptor.base.position = {0.0f, 0.0f, -1000.0f};

    camera_v1::CameraDescriptor program_descriptor = descriptor;
    program_descriptor.id = "program-camera";
    program_descriptor.base.position = {250.0f, 0.0f, -500.0f};
    auto camera_result = chronon3d::compile_camera(program_descriptor, {});
    REQUIRE(camera_result.has_value());
    comp.default_camera_descriptor(descriptor);
    comp.camera_program(std::move(camera_result).value());

    auto compiled = compile_composition(comp, {});
    REQUIRE(compiled.has_value());
    REQUIRE(compiled->definition);
    REQUIRE(compiled->definition->camera.has_value());
    CHECK(compiled->definition->camera->id == "adapter-descriptor");
    REQUIRE(compiled->camera_program);
    CHECK(compiled->camera_program->is_compiled());
    CHECK(compiled->fingerprint != 0);

    CompositionEvaluateContext eval_context;
    eval_context.frame_context = eval_context.frame_context.with_frame_rate(comp.frame_rate());
    eval_context.frame_context.width = comp.width();
    eval_context.frame_context.height = comp.height();
    auto evaluated = evaluate(compiled.value(), eval_context, Frame{0});
    REQUIRE(evaluated.has_value());
    REQUIRE(evaluated->camera.has_value());
    CHECK(evaluated->camera->position.x == doctest::Approx(250.0f));
    CHECK(evaluated->camera->position.z == doctest::Approx(-500.0f));
}
