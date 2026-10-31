#include <chronon3d/scene/builders/scene_builder.hpp>
#include <doctest/doctest.h>
#include <tests/helpers/composition_helpers.hpp>

#include <chronon3d/animation/temporal/temporal_samples.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/camera_contract.hpp>
#include <chronon3d/render_plan/resolved_transform_samples.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <src/render_plan/render_plan_camera_adapter.hpp>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <src/render_plan/render_plan_compiler_detail.hpp>

#include <cmath>
#include <filesystem>
#include <vector>
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

TEST_CASE("Explicit camera samples preserve nonlinear poses at actual shutter times") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "camera-exact-shutter-samples";
    plan.canvas = {.width = 96, .height = 64,
                   .fps = FrameRate{30, 1}, .duration = Frame{2}};
    const auto camera_at = [](double frame) {
        CameraPlan camera;
        camera.position = {static_cast<float>(100.0 * frame * frame), 0.f, -1000.f};
        camera.rotation_deg = {0.f, static_cast<float>(10.0 * frame * frame), 0.f};
        camera.fov_deg = static_cast<float>(60.0 + frame * frame);
        camera.dof_enabled = true;
        camera.dof_focus_distance = static_cast<float>(500.0 + 10.0 * frame * frame);
        return camera;
    };
    plan.camera = camera_at(0.0);
    const auto shutter = temporal::make_temporal_sample_plan(
        temporal::TemporalSampleParams{}, 8, Frame{0}, FrameRate{30, 1}, 96, 64);
    REQUIRE(shutter.valid());
    std::vector<CameraSamplePlan> samples;
    for (const auto& context : shutter.contexts)
        samples.push_back({context.time.frame, camera_at(context.time.frame)});
    plan.camera_samples = samples;
    assets::AssetResolver resolver;
    const auto offline = compile_render_plan(plan, resolver);
    REQUIRE(offline);
    auto runtime_plan = plan;
    runtime_plan.camera_samples.reset();
    RuntimeCameraSampleProvider provider{.fingerprint = "exact-shutter-runtime", .sample = camera_at};
    const auto runtime = compile_render_plan(runtime_plan, resolver, {}, nullptr, &provider);
    REQUIRE(runtime);
    // Traverse the schedule in reverse too: random access must retain the
    // same pose, not state carried over from the preceding sample.
    for (std::size_t i = 0; i < shutter.contexts.size() * 2; ++i) {
        const auto index = i < shutter.contexts.size()
            ? i : shutter.contexts.size() * 2 - i - 1;
        const auto time = shutter.contexts[index].time;
        const auto baked_scene = evaluate(offline->compiled_composition, {}, time);
        const auto direct_scene = evaluate(runtime->compiled_composition, {}, time);
        REQUIRE(baked_scene);
        REQUIRE(direct_scene);
        const auto& baked = baked_scene->scene.camera_2_5d();
        const auto& direct = direct_scene->scene.camera_2_5d();
        CHECK(baked.position.x == doctest::Approx(direct.position.x));
        CHECK(baked.rotation.y == doctest::Approx(direct.rotation.y));
        CHECK(baked.fov_deg == doctest::Approx(direct.fov_deg));
        CHECK(baked.dof.focus_distance == doctest::Approx(direct.dof.focus_distance));
    }
}

TEST_CASE("Typed program samples the same camera as the compatibility evaluator") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "typed-camera-samples";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{3}};
    CameraPlan first;
    first.position = {0.f, 0.f, -1000.f};
    first.rotation_deg = {0.f, 0.f, 170.f};
    first.fov_deg = 45.f;
    CameraPlan last = first;
    last.position[0] = 200.f;
    last.rotation_deg[2] = -170.f;
    last.fov_deg = 75.f;
    plan.camera = first;
    plan.camera_samples = std::vector<CameraSamplePlan>{{0., first}, {2., last}};
    assets::AssetResolver resolver;
    auto compiled = compiler::compile(plan, resolver);
    REQUIRE(compiled);
    const auto evaluator = chronon3d::render_plan::detail::make_camera_evaluator(plan);
    for (double frame : {-1., 0., 0.375, 1., 2., 3.}) {
        const auto expected = evaluator(frame);
        const auto sampled = chronon3d::render_plan::detail::sample_resolved_camera(
            compiled->metadata().camera_samples, frame);
        CHECK(sampled.position.x == doctest::Approx(expected.position.x));
        CHECK(sampled.rotation.z == doctest::Approx(expected.rotation.z));
        CHECK(sampled.fov_deg == doctest::Approx(expected.fov_deg));
    }
    auto other_plan = plan;
    other_plan.camera_samples->back().camera.position[0] = 400.f;
    auto other = compiler::compile(other_plan, resolver);
    REQUIRE(other);
    CHECK(compiled->fingerprint() != other->fingerprint());
    auto invalid = plan;
    invalid.camera_samples = std::vector<CameraSamplePlan>{{1., first}, {1., last}};
    CHECK_FALSE(compiler::compile(invalid, resolver));
}

TEST_CASE("Typed program rendering consumes animated camera samples") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "typed-camera-pixels";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{2}};
    CameraPlan first;
    first.position = {0.f, 0.f, -1000.f};
    CameraPlan last = first;
    last.position[0] = 240.f;
    plan.camera = first;
    plan.camera_samples = std::vector<CameraSamplePlan>{{0., first}, {1., last}};
    LayerPlan subject;
    subject.id = "subject";
    subject.type = LayerType::Color;
    subject.color = {0.95f, 0.2f, 0.1f, 1.f};
    subject.size = {42.f, 30.f};
    subject.size_dimensions = 2;
    subject.position_dimensions = 2;
    subject.enable_3d = true;
    plan.layers.push_back(subject);
    assets::AssetResolver resolver;
    auto compiled = compiler::compile(plan, resolver);
    REQUIRE(compiled);
    auto renderer = test::make_renderer();
    const auto frame0 = renderer.render_program(*compiled, Frame{0});
    REQUIRE(frame0);
    const auto hash0 = test::framebuffer_hash(*frame0);
    int visible0 = 0;
    for (int y = 0; y < frame0->height(); ++y)
        for (int x = 0; x < frame0->width(); ++x)
            visible0 += frame0->get_pixel(x, y).a > 0.05f;
    const auto frame1 = renderer.render_program(*compiled, Frame{1});
    REQUIRE(frame1);
    int visible1 = 0;
    for (int y = 0; y < frame1->height(); ++y)
        for (int x = 0; x < frame1->width(); ++x)
            visible1 += frame1->get_pixel(x, y).a > 0.05f;
    const auto hash1 = test::framebuffer_hash(*frame1);
    INFO("visible pixels frame0=" << visible0 << " frame1=" << visible1);
    CHECK(visible0 > 0);
    CHECK(hash0 != hash1);
    // Camera motion must not depend on graph/cache history. Compare reverse
    // access and a fresh renderer against the sequential result.
    const auto frame0_again = renderer.render_program(*compiled, Frame{0});
    REQUIRE(frame0_again);
    CHECK(test::framebuffer_hash(*frame0_again) == hash0);
    auto fresh_renderer = test::make_renderer();
    const auto frame1_fresh = fresh_renderer.render_program(*compiled, Frame{1});
    REQUIRE(frame1_fresh);
    CHECK(test::framebuffer_hash(*frame1_fresh) == hash1);
}

TEST_CASE("Typed program temporal accumulation uses camera shutter samples") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "typed-camera-shutter";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{2}};
    CameraPlan first;
    first.position = {-240.f, 0.f, -1000.f};
    CameraPlan last = first;
    last.position[0] = 240.f;
    CameraPlan center = first;
    center.position[0] = 0.f;
    plan.camera = center;
    plan.camera_samples = std::vector<CameraSamplePlan>{{-1., first}, {1., last}};
    LayerPlan subject;
    subject.id = "subject";
    subject.type = LayerType::Color;
    subject.color = {0.95f, 0.2f, 0.1f, 1.f};
    subject.size = {24.f, 24.f};
    subject.size_dimensions = 2;
    subject.position_dimensions = 2;
    subject.enable_3d = true;
    plan.layers.push_back(subject);

    assets::AssetResolver resolver;
    const auto moving = compiler::compile(plan, resolver);
    REQUIRE(moving);
    auto static_plan = plan;
    static_plan.camera_samples = std::vector<CameraSamplePlan>{{-1., center}, {1., center}};
    const auto stationary = compiler::compile(static_plan, resolver);
    REQUIRE(stationary);

    auto renderer = test::make_renderer();
    auto settings = renderer.render_settings();
    settings.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
    settings.motion_blur.samples = 8;
    settings.motion_blur.shutter_angle_deg = 180.f;
    renderer.set_settings(settings);
    const auto blurred = renderer.render_program(*moving, Frame{0});
    REQUIRE(blurred);
    const auto blurred_hash = test::framebuffer_hash(*blurred);
    int visible = 0;
    for (int y = 0; y < blurred->height(); ++y)
        for (int x = 0; x < blurred->width(); ++x)
            visible += blurred->get_pixel(x, y).a > 0.05f;
    CHECK(visible > 0);
    const auto still = renderer.render_program(*stationary, Frame{0});
    REQUIRE(still);
    CHECK(blurred_hash != test::framebuffer_hash(*still));
    const auto repeated = renderer.render_program(*moving, Frame{0});
    REQUIRE(repeated);
    CHECK(test::framebuffer_hash(*repeated) == blurred_hash);
}

TEST_CASE("Resolved transform sample JSON is strict and preserves fractional times") {
    using namespace chronon3d::render_plan;
    nlohmann::json json = {
        {"schema", "chronon.render-plan.v3"}, {"version", 3}, {"job_id", "resolved-json"},
        {"canvas", {{"width", 96}, {"height", 64}, {"fps_num", 30}, {"fps_den", 1}, {"duration_frames", 3}}},
        {"layers", nlohmann::json::array({{
            {"id", "plane"}, {"type", "color"}, {"color", "#CC331A"},
            {"transform_samples", nlohmann::json::array({{
                {"frame", 0.123456789012345}, {"position", {2., 3., 4.}},
                {"scale", {1., 1., 1.}}, {"rotation_deg", {0., 0., 0.}}, {"opacity", .75}
            }})}
        }})},
        {"output", {{"path", "resolved.png"}, {"format", "png"}}}
    };
    const auto decoded = decode_render_plan(json);
    INFO(decoded ? "" : decoded.error().message);
    REQUIRE(decoded);
    REQUIRE(decoded->layers[0].transform_samples.size() == 1);
    CHECK(decoded->layers[0].transform_samples[0].frame == 0.123456789012345);
    auto malformed = json;
    malformed["layers"][0]["transform_samples"][0]["unexpected"] = true;
    CHECK_FALSE(decode_render_plan(malformed));
    malformed = json;
    malformed["layers"][0]["transform_samples"][0]["position"] = {1., 2.};
    CHECK_FALSE(decode_render_plan(malformed));
}

TEST_CASE("Typed shutter offline and runtime cameras consume identical resolved layer samples") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "typed-motion-exact-shutter";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{3}};
    plan.temporal_motion_blur = MotionBlurPlan{180.f, 8};
    const auto camera_at = [](double time) {
        CameraPlan camera;
        camera.position = {static_cast<float>(20. * time * time), 0.f, -100.f};
        camera.rotation_deg[2] = static_cast<float>(170. + 10. * time);
        camera.fov_deg = static_cast<float>(50. + time * time);
        return camera;
    };
    plan.camera = camera_at(0.);
    plan.camera_samples.emplace();
    const auto times = resolved_sample_frames(plan.canvas, plan.temporal_motion_blur);
    REQUIRE(times.size() == 27);
    LayerPlan subject;
    subject.id = "resolved-motion-plane";
    subject.type = LayerType::Color;
    subject.color = {0.8f, 0.2f, 0.1f, 1.f};
    subject.size = {28.f, 20.f};
    subject.size_dimensions = 2;
    subject.enable_3d = true;
    for (double time : times) {
        plan.camera_samples->push_back({time, camera_at(time)});
        subject.transform_samples.push_back({time,
            {static_cast<float>(3. * time * time), 0.f, 0.f}, {1.f, 1.f, 1.f},
            {0.f, 0.f, static_cast<float>(time * time)}, static_cast<float>(.6 + .05 * time * time)});
    }
    plan.layers.push_back(subject);
    assets::AssetResolver resolver;
    const auto offline = compiler::compile(plan, resolver);
    REQUIRE(offline);
    auto runtime_plan = plan;
    runtime_plan.camera_samples.reset();
    RuntimeCameraSampleProvider provider{.fingerprint = "typed-motion-exact-runtime", .sample = camera_at};
    compiler::CompileOptions options;
    options.runtime_camera = &provider;
    const auto runtime = compiler::compile(runtime_plan, resolver, options);
    REQUIRE(runtime);
    CHECK(offline->fingerprint() == runtime->fingerprint());
    const auto compatibility = compile_render_plan(plan, resolver);
    REQUIRE(compatibility);
    for (auto it = times.rbegin(); it != times.rend(); ++it) {
        compiler::FrameParameterBlock parameters;
        compiler::TypedProgramSampler{}.sample_into(*offline, *it, parameters);
        REQUIRE(parameters.layers.size() == 1);
        CHECK(parameters.layers[0].matrix[3][0] == doctest::Approx(3. * *it * *it));
        CHECK(parameters.layers[0].opacity == doctest::Approx(.6 + .05 * *it * *it));
        const auto snapshot = evaluate(compatibility->compiled_composition, {}, SampleTime::from_frame(*it, plan.canvas.fps));
        REQUIRE(snapshot);
        CHECK(chronon3d::render_plan::sample_resolved_transform(subject.transform_samples, *it).frame == *it);
    }
    auto renderer = test::make_renderer();
    auto settings = renderer.render_settings();
    settings.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
    settings.motion_blur.samples = 8;
    settings.motion_blur.shutter_angle_deg = 180.f;
    renderer.set_settings(settings);
    std::vector<std::uint64_t> hashes;
    for (int frame : {0, 1, 2}) {
        const auto baked = renderer.render_program(*offline, Frame{frame});
        REQUIRE(baked);
        hashes.push_back(test::framebuffer_hash(*baked));
        const auto direct = renderer.render_program(*runtime, Frame{frame});
        REQUIRE(direct);
        CHECK(test::framebuffer_hash(*direct) == hashes.back());
    }
    for (int frame : {2, 0, 1}) {
        const auto repeated = renderer.render_program(*offline, Frame{frame});
        REQUIRE(repeated);
        CHECK(test::framebuffer_hash(*repeated) == hashes[frame]);
    }
    auto invalid = plan;
    invalid.layers[0].transform_samples[1].frame = invalid.layers[0].transform_samples[0].frame;
    CHECK_FALSE(compiler::compile(invalid, resolver));
    CHECK_FALSE(compile_render_plan(invalid, resolver));
}

TEST_CASE("Camera capability contract rejects unsupported optics at both compiler boundaries") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "camera-capability-boundary";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{1}};
    plan.camera = CameraPlan{};
    LayerPlan layer;
    layer.id = "subject";
    layer.type = LayerType::Color;
    plan.layers.push_back(layer);
    assets::AssetResolver resolver;
    for (float fov : {10.f, 120.f}) {
        plan.camera->fov_deg = fov;
        CHECK_FALSE(camera_contract_problem(*plan.camera));
        CHECK(compiler::compile(plan, resolver));
        CHECK(compile_render_plan(plan, resolver));
    }
    const auto rejected = [&](const CameraPlan& camera) {
        auto invalid = plan;
        invalid.camera = camera;
        CHECK(camera_contract_problem(camera).has_value());
        CHECK_FALSE(compiler::compile(invalid, resolver));
        CHECK_FALSE(compile_render_plan(invalid, resolver));
        invalid.camera = CameraPlan{};
        invalid.camera_samples = std::vector<CameraSamplePlan>{{0., camera}};
        CHECK_FALSE(compiler::compile(invalid, resolver));
        CHECK_FALSE(compile_render_plan(invalid, resolver));
    };
    for (float fov : {9.99f, 120.01f, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
        auto camera = CameraPlan{};
        camera.fov_deg = fov;
        rejected(camera);
    }
    auto camera = CameraPlan{};
    camera.dof_iris_blades = 6;
    rejected(camera);
    camera = CameraPlan{};
    camera.dof_highlight_gain = 1.f;
    rejected(camera);
    camera = CameraPlan{};
    camera.iris_blades = 5;
    rejected(camera);
    camera = CameraPlan{};
    camera.dof_focus_distance = 0.f;
    rejected(camera);
    camera = CameraPlan{};
    camera.position[1] = std::numeric_limits<float>::quiet_NaN();
    rejected(camera);
}

TEST_CASE("Camera projection preserves oversized image pixels outside the source canvas") {
    using namespace chronon3d::render_plan;
    const auto root = std::filesystem::path("output/camera_motion/oversized_image");
    std::filesystem::create_directories(root);
    Framebuffer image(192, 128);
    image.clear(Color{0.1f, 0.3f, 0.7f, 1.f});
    REQUIRE(save_png(image, (root / "background.png").string()));

    RenderPlan plan;
    plan.job_id = "oversized-camera-image";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{1}};
    CameraPlan camera;
    camera.position = {0.f, 0.f, -100.f};
    camera.fov_deg = 50.f;
    plan.camera = camera;
    LayerPlan background;
    background.id = "background";
    background.type = LayerType::Image;
    background.asset = (root / "background.png").string();
    background.size = {400.f, 300.f};
    background.size_dimensions = 2;
    background.position = {0.f, 0.f, 100.f};
    background.position_dimensions = 3;
    background.enable_3d = true;
    plan.layers.push_back(background);
    assets::AssetResolver resolver;
    resolver.mount(std::filesystem::current_path());
    const auto prepared = compile_render_plan(plan, resolver);
    const std::string compile_error = prepared ? std::string{} : prepared.error().message;
    REQUIRE_MESSAGE(prepared, compile_error);
    auto renderer = test::make_renderer();
    const auto frame = renderer.render_compiled(prepared->compiled_composition, Frame{0});
    REQUIRE(frame);
    // The authored plane projects beyond every viewport edge. Cropping its
    // producer to 96x64 before projection used to leave only a central patch.
    for (const auto point : {Vec2{4.f, 4.f}, Vec2{91.f, 4.f},
                             Vec2{4.f, 59.f}, Vec2{91.f, 59.f}}) {
        const auto pixel = frame->get_pixel(static_cast<int>(point.x), static_cast<int>(point.y));
        CHECK(pixel.a == doctest::Approx(1.f));
        CHECK(pixel.b > pixel.r);
    }
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
