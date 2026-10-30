#include <chronon3d/scene/builders/scene_builder.hpp>
#include <doctest/doctest.h>
#include <tests/helpers/composition_helpers.hpp>

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <src/render_plan/render_plan_compiler_detail.hpp>

#include <cmath>
#include <filesystem>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

namespace {

enum class TestAxis { Tilt, Pan, Roll };

void add_motion_content(SceneBuilder& s, const FrameContext& ctx) {
    s.layer("card", [ctx](LayerBuilder& l) {
        l.enable_3d()
         .rect("card", {
             .size = {static_cast<f32>(ctx.width) * 0.55f, static_cast<f32>(ctx.height) * 0.35f},
             .color = Color::from_hex("#f2f2f2"),
             .pos = {static_cast<f32>(ctx.width) * 0.5f, static_cast<f32>(ctx.height) * 0.5f, 0.0f},
         })
         .rect("marker", {
             .size = {static_cast<f32>(ctx.width) * 0.08f, static_cast<f32>(ctx.height) * 0.08f},
             .color = Color::from_hex("#d44c4c"),
             .pos = {static_cast<f32>(ctx.width) * 0.36f, static_cast<f32>(ctx.height) * 0.35f, 0.0f},
         })
         .line("slash", {
             .from = {static_cast<f32>(ctx.width) * 0.30f, static_cast<f32>(ctx.height) * 0.25f, 0.0f},
             .to = {static_cast<f32>(ctx.width) * 0.70f, static_cast<f32>(ctx.height) * 0.68f, 0.0f},
             .thickness = 12.0f,
             .color = Color::from_hex("#2b59ff"),
         });
    });
}

void render_resolved_camera(TestAxis axis, const char* name, const char* filename) {
    std::filesystem::create_directories("output/camera_motion");

    auto renderer = test::make_renderer();
    CompositionSpec spec;
    spec.name = name;
    spec.width = 1920;
    spec.height = 1080;
    spec.duration = 60;
    Composition comp(spec, [axis](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        Camera2_5D camera;
        camera.enabled = true;
        camera.position = {0.0f, 0.0f, -1080.0f};
        camera.zoom = 1080.0f;
        const f32 angle = ctx.frame() < 30 ? -18.0f : 18.0f;
        switch (axis) {
        case TestAxis::Tilt: camera.rotation.x = angle; break;
        case TestAxis::Pan: camera.rotation.y = angle; break;
        case TestAxis::Roll: camera.rotation.z = angle; break;
        }
        scene.set_evaluated_camera(std::move(camera));
        add_motion_content(scene, ctx);
        return scene.build();
    });

    auto scene_start = chronon3d::test_support::evaluate_frame(comp, Frame{0});
    auto scene_mid = chronon3d::test_support::evaluate_frame(comp, Frame{30});

    auto fb_start = renderer.render(comp, 0);
    auto fb_mid = renderer.render(comp, 30);

    REQUIRE(fb_start != nullptr);
    REQUIRE(fb_mid != nullptr);
    CHECK(fb_start->width() == fb_mid->width());
    CHECK(fb_start->height() == fb_mid->height());
    CHECK(scene_start.camera_2_5d().enabled);
    CHECK(scene_mid.camera_2_5d().enabled);
    CHECK(scene_start.camera_2_5d().rotation != scene_mid.camera_2_5d().rotation);

    const std::filesystem::path out = std::filesystem::path("output/camera_motion") / filename;
    CHECK(save_png(*fb_mid, out.string()));
    CHECK(std::filesystem::exists(out));
    CHECK(std::filesystem::file_size(out) > 0);
}

} // namespace

TEST_CASE("Renderer consumes already-resolved camera samples") {
    SUBCASE("tilt") {
        render_resolved_camera(TestAxis::Tilt, "ResolvedCameraTiltTest", "tilt.png");
    }

    SUBCASE("pan") {
        render_resolved_camera(TestAxis::Pan, "ResolvedCameraPanTest", "pan.png");
    }

    SUBCASE("roll") {
        render_resolved_camera(TestAxis::Roll, "ResolvedCameraRollTest", "roll.png");
    }
}

TEST_CASE("runtime camera provider changes rendered pixels") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;

    RenderPlan plan;
    plan.job_id = "runtime-camera-pixel-truth";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1},
                   .duration = Frame{2}};
    plan.camera = CameraPlan{};
    LayerPlan subject;
    subject.id = "camera-subject";
    subject.type = LayerType::Color;
    subject.color = {0.95f, 0.2f, 0.1f, 1.0f};
    subject.size = {42.0f, 30.0f};
    subject.position = {48.0f, 32.0f, 0.0f};
    plan.layers.push_back(subject);

    RuntimeCameraSampleProvider provider{
        .fingerprint = "cm3d-pixel-truth-test",
        .sample = [camera = *plan.camera](double frame) mutable {
            auto resolved = camera;
            // A deterministic resolved CM3D pose moves the camera across the
            // subject between integer presentation frames.
            resolved.position[0] = static_cast<float>(frame * 240.0);
            return resolved;
        }};
    assets::AssetResolver resolver;
    const auto prepared = compile_render_plan(plan, resolver, {}, nullptr, &provider);
    REQUIRE(prepared);

    auto renderer = test::make_renderer();
    const auto first = renderer.render_compiled(prepared->compiled_composition, Frame{0});
    const auto second = renderer.render_compiled(prepared->compiled_composition, Frame{1});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->width() == 96);
    CHECK(first->height() == 64);
    CHECK(test::framebuffer_hash(*first) != test::framebuffer_hash(*second));
}

TEST_CASE("RenderPlan camera_samples sub-frame evaluation uses quaternion slerp across wrap") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;

    RenderPlan plan;
    plan.job_id = "camera-samples-slerp-wrap";
    plan.canvas = {.width = 96, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{2}};
    LayerPlan bg;
    bg.id = "bg";
    bg.type = LayerType::Color;
    bg.color = {0.1f, 0.1f, 0.1f, 1.0f};
    plan.layers.push_back(bg);

    CameraSamplePlan s0;
    s0.frame = 0.0;
    s0.camera.rotation_deg = {0.0f, 0.0f, 170.0f};
    CameraSamplePlan s1;
    s1.frame = 1.0;
    s1.camera.rotation_deg = {0.0f, 0.0f, -170.0f};
    plan.camera = s0.camera;
    plan.camera_samples = std::vector<CameraSamplePlan>{s0, s1};

    const auto eval = chronon3d::render_plan::detail::make_camera_evaluator(plan);
    const auto cam = eval(0.5);
    CHECK(cam.enabled);
    CHECK(std::abs(cam.rotation.z) == doctest::Approx(180.0f).epsilon(1e-3f));
}
