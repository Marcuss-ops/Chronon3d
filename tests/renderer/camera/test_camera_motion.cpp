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
#include <fstream>
#include <map>
#include <stdexcept>
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
    first.near = 2.f;
    first.far = 2500.f;
    CameraPlan last = first;
    last.position[0] = 200.f;
    last.rotation_deg[2] = -170.f;
    last.fov_deg = 75.f;
    last.near = 4.f;
    last.far = 5000.f;
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
        CHECK(sampled.near_plane == doctest::Approx(expected.near_plane));
        CHECK(sampled.far_plane == doctest::Approx(expected.far_plane));
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

TEST_CASE("typed inline mesh animation remains visible under a small camera yaw") {
    using namespace chronon3d::render_plan;

    RenderPlan plan;
    plan.job_id = "animated-inline-mesh-camera-yaw-regression";
    plan.canvas = {.width = 640, .height = 360, .fps = FrameRate{30, 1},
                   .duration = Frame{150}};
    CameraPlan camera_start;
    camera_start.position = {0.f, 0.f, -1000.f};
    camera_start.rotation_deg = {-0.01f, -0.02f, 0.01f};
    camera_start.fov_deg = 50.f;
    CameraPlan camera_orbit_mid = camera_start;
    camera_orbit_mid.position = {-5.f, 3.f, -1000.f};
    camera_orbit_mid.rotation_deg = {0.01f, 0.f, -0.01f};
    CameraPlan camera_end = camera_start;
    camera_end.position = {10.f, 5.f, -1000.f};
    camera_end.rotation_deg = {0.f, 0.02f, -0.01f};
    plan.camera = camera_start;
    plan.camera_samples = std::vector<CameraSamplePlan>{
        {0.0, camera_start}, {74.0, camera_orbit_mid}, {149.0, camera_end}};

    LayerPlan layer;
    layer.id = "animated-triangle";
    layer.type = LayerType::Mesh;
    layer.color = {1.f, 0.2f, 0.1f, 1.f};
    InlineMeshPlan mesh;
    mesh.vertices = {
        {{{-50.f, -50.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.f, 0.f}}},
        {{{ 50.f, -50.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{1.f, 0.f}}},
        {{{-50.f,  50.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.f, 1.f}}},
    };
    mesh.indices = {0, 1, 2};
    for (int frame = 0; frame < 150; ++frame) {
        const float dx = 200.f * static_cast<float>(frame) / 149.f;
        const float dy = 100.f * static_cast<float>(frame) / 149.f;
        mesh.animation.push_back({
            frame,
            {{{-50.f + dx, -50.f + dy, 0.f}},
             {{ 50.f + dx, -50.f + dy, 0.f}},
             {{-50.f + dx,  50.f + dy, 0.f}}},
            {}});
    }
    layer.inline_mesh = std::move(mesh);
    plan.layers.push_back(std::move(layer));

    assets::AssetResolver resolver;
    const auto compiled = compiler::compile(plan, resolver);
    REQUIRE(compiled);
    auto renderer = test::make_renderer();
    for (int frame = 0; frame < 150; ++frame) {
        CAPTURE(frame);
        const auto rendered = renderer.render_program(*compiled, Frame{frame});
        REQUIRE(rendered);
        std::size_t visible = 0;
        for (int y = 0; y < rendered->height(); ++y)
            for (int x = 0; x < rendered->width(); ++x)
                visible += rendered->get_pixel(x, y).a > 0.05f;
        INFO("visible pixels=" << visible);
        CHECK(visible > 100);
    }
}

TEST_CASE("2D image behind inline mesh preserves the complete mesh coverage") {
    using namespace chronon3d::render_plan;

    const auto asset_root = std::filesystem::path("output/camera_motion/image_mesh_occlusion");
    std::filesystem::create_directories(asset_root);
    Framebuffer background(128, 96);
    background.clear(Color{0.05f, 0.2f, 0.95f, 1.f});
    REQUIRE(save_png(background, (asset_root / "background.png").string()));

    const auto make_plan = [&](bool with_image, bool plane_3d = false) {
        RenderPlan plan;
        plan.job_id = plane_3d ? "plane3d-behind-inline-mesh"
            : (with_image ? "image-behind-inline-mesh" : "inline-mesh-control");
        plan.canvas = {.width = 128, .height = 96, .fps = FrameRate{30, 1},
                       .duration = Frame{1}};
        CameraPlan camera;
        camera.position = {0.f, 0.f, -1000.f};
        camera.fov_deg = 50.f;
        plan.camera = camera;
        if (with_image) {
            LayerPlan image;
            image.id = "background-image";
            image.type = LayerType::Image;
            image.asset = (asset_root / "background.png").string();
            image.size = plane_3d ? std::array<float, 2>{160.f, 120.f}
                                  : std::array<float, 2>{128.f, 96.f};
            image.size_dimensions = 2;
            image.enable_3d = plane_3d;
            if (plane_3d) {
                image.position = {0.f, 0.f, 100.f};
                image.position_dimensions = 3;
            }
            plan.layers.push_back(std::move(image));
        }
        LayerPlan layer;
        layer.id = "foreground-mesh";
        layer.type = LayerType::Mesh;
        layer.color = {1.f, 0.05f, 0.02f, 1.f};
        InlineMeshPlan mesh;
        mesh.vertices = {
            {{{-90.f, -70.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.f, 0.f}}},
            {{{ 90.f, -70.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{1.f, 0.f}}},
            {{{  0.f,  85.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.5f, 1.f}}},
        };
        mesh.indices = {0, 1, 2};
        layer.inline_mesh = std::move(mesh);
        plan.layers.push_back(std::move(layer));
        return plan;
    };

    assets::AssetResolver resolver;
    resolver.mount(std::filesystem::current_path());
    auto renderer = test::make_renderer();
    const auto render = [&](bool with_image, bool plane_3d = false) {
        const auto compiled = compiler::compile(make_plan(with_image, plane_3d), resolver);
        const std::string error = compiled ? std::string{} : compiled.error().message;
        REQUIRE_MESSAGE(compiled, error);
        return renderer.render_program(*compiled, Frame{0});
    };
    const auto isolated = render(false);
    const auto composed = render(true);
    const auto plane_composed = render(true, true);
    REQUIRE(isolated);
    REQUIRE(composed);
    REQUIRE(plane_composed);

    std::size_t isolated_red = 0;
    std::size_t composed_red = 0;
    std::size_t plane_red = 0;
    std::size_t visible_blue_background = 0;
    std::size_t coverage_mismatch = 0;
    std::size_t plane_coverage_mismatch = 0;
    std::size_t isolated_interior = 0;
    std::size_t composed_interior_mismatch = 0;
    std::size_t plane_interior_mismatch = 0;
    int mismatch_x = -1;
    int mismatch_y = -1;
    for (int y = 0; y < 96; ++y) {
        for (int x = 0; x < 128; ++x) {
            const auto a = isolated->get_pixel(x, y);
            const auto b = composed->get_pixel(x, y);
            const auto c = plane_composed->get_pixel(x, y);
            const bool a_red = a.r > 0.1f && a.r > 2.f * a.g && a.r > 2.f * a.b;
            const bool b_red = b.r > 0.1f && b.r > 2.f * b.g && b.r > 2.f * b.b;
            const bool c_red = c.r > 0.1f && c.r > 2.f * c.g && c.r > 2.f * c.b;
            isolated_red += a_red;
            composed_red += b_red;
            plane_red += c_red;
            visible_blue_background += b.b > 0.2f && b.b > 2.f * b.r;
            if (a_red != b_red) {
                ++coverage_mismatch;
                mismatch_x = x;
                mismatch_y = y;
            }
            plane_coverage_mismatch += a_red != c_red;
        }
    }
    for (int y = 1; y < 95; ++y) {
        for (int x = 1; x < 127; ++x) {
            const auto red_at = [&](const Framebuffer& frame, int px, int py) {
                const auto pixel = frame.get_pixel(px, py);
                return pixel.r > 0.1f && pixel.r > 2.f * pixel.g && pixel.r > 2.f * pixel.b;
            };
            bool interior = red_at(*isolated, x, y);
            for (int oy = -1; oy <= 1 && interior; ++oy)
                for (int ox = -1; ox <= 1 && interior; ++ox)
                    interior = red_at(*isolated, x + ox, y + oy);
            if (!interior) continue;
            ++isolated_interior;
            composed_interior_mismatch += !red_at(*composed, x, y);
            plane_interior_mismatch += !red_at(*plane_composed, x, y);
        }
    }
    INFO("isolated/image/plane red pixels=" << isolated_red << "/" << composed_red << "/"
         << plane_red << ", edge coverage mismatch=" << coverage_mismatch << "/"
         << plane_coverage_mismatch << ", visible blue background="
         << visible_blue_background);
    INFO("mismatch location=" << mismatch_x << "," << mismatch_y);
    INFO("interior pixels=" << isolated_interior << ", interior mismatches="
         << composed_interior_mismatch);
    if (mismatch_x >= 0) {
        const auto a = isolated->get_pixel(mismatch_x, mismatch_y);
        const auto b = composed->get_pixel(mismatch_x, mismatch_y);
        INFO("isolated=" << a.r << "," << a.g << "," << a.b << "," << a.a
             << " composed=" << b.r << "," << b.g << "," << b.b << "," << b.a);
    }
    CHECK(isolated_red > 100);
    CHECK(composed_red > 100);
    CHECK(coverage_mismatch <= 1);
    CHECK(plane_red > 100);
    CHECK(plane_coverage_mismatch <= 1);
    CHECK(isolated_interior > 20);
    CHECK(composed_interior_mismatch == 0);
    CHECK(plane_interior_mismatch == 0);
    CHECK(visible_blue_background > 0);
}

TEST_CASE("typed camera depth resolves crossing mesh and 2.5D plane independent of layer order") {
    using namespace chronon3d::render_plan;

    const auto asset_root = std::filesystem::path("output/camera_motion/typed_depth_order");
    std::filesystem::create_directories(asset_root);
    Framebuffer background(128, 96);
    background.clear(Color{0.f, 0.f, 1.f, 1.f});
    REQUIRE(save_png(background, (asset_root / "blue-plane.png").string()));

    const auto make_plan = [&](bool plane_first, bool with_plane = true) {
        RenderPlan plan;
        plan.job_id = plane_first ? "depth-crossing-plane-first" : "depth-crossing-mesh-first";
        plan.canvas = {.width = 128, .height = 96, .fps = FrameRate{30, 1},
                       .duration = Frame{1}};
        CameraPlan camera;
        camera.position = {0.f, 0.f, -1000.f};
        camera.fov_deg = 50.f;
        camera.far = 10000.f;
        plan.camera = camera;

        LayerPlan plane;
        plane.id = "blue-2.5d-plane";
        plane.type = LayerType::Image;
        plane.asset = (asset_root / "blue-plane.png").string();
        plane.size = {160.f, 120.f};
        plane.size_dimensions = 2;
        plane.position = {0.f, 0.f, 0.f};
        plane.position_dimensions = 3;
        plane.enable_3d = true;

        LayerPlan mesh_layer;
        mesh_layer.id = "crossing-red-mesh";
        mesh_layer.type = LayerType::Mesh;
        mesh_layer.color = {1.f, 0.f, 0.f, 1.f};
        InlineMeshPlan mesh;
        mesh.vertices = {
            {{{-90.f, -70.f, -30.f}}, {{0.f, 0.f, 1.f}}, {{0.f, 0.f}}},
            {{{ 90.f, -70.f,  30.f}}, {{0.f, 0.f, 1.f}}, {{1.f, 0.f}}},
            {{{  0.f,  85.f,   0.f}}, {{0.f, 0.f, 1.f}}, {{0.5f, 1.f}}},
        };
        mesh.indices = {0, 1, 2};
        mesh_layer.inline_mesh = std::move(mesh);

        if (plane_first) {
            if (with_plane) plan.layers.push_back(std::move(plane));
            plan.layers.push_back(std::move(mesh_layer));
        } else {
            plan.layers.push_back(std::move(mesh_layer));
            if (with_plane) plan.layers.push_back(std::move(plane));
        }
        return plan;
    };

    assets::AssetResolver resolver;
    resolver.mount(std::filesystem::current_path());
    auto renderer = test::make_renderer();
    const auto render = [&](bool plane_first, bool with_plane = true) {
        const auto compiled = compiler::compile(make_plan(plane_first, with_plane), resolver);
        const std::string error = compiled ? std::string{} : compiled.error().message;
        REQUIRE_MESSAGE(compiled, error);
        return renderer.render_program(*compiled, Frame{0});
    };
    const auto plane_first = render(true);
    const auto mesh_first = render(false);
    const auto mesh_only = render(false, false);
    REQUIRE(plane_first);
    REQUIRE(mesh_first);
    REQUIRE(mesh_only);

    std::size_t differing_pixels = 0;
    float max_channel_delta = 0.f;
    std::size_t red_coverage = 0;
    std::size_t blue_coverage = 0;
    std::size_t mesh_occluded_by_plane = 0;
    for (int y = 0; y < plane_first->height(); ++y) {
        for (int x = 0; x < plane_first->width(); ++x) {
            const auto a = plane_first->get_pixel(x, y);
            const auto b = mesh_first->get_pixel(x, y);
            const auto isolated = mesh_only->get_pixel(x, y);
            const float delta = std::max({
                std::abs(a.r - b.r), std::abs(a.g - b.g),
                std::abs(a.b - b.b), std::abs(a.a - b.a)});
            max_channel_delta = std::max(max_channel_delta, delta);
            differing_pixels += delta > 1e-5f;
            red_coverage += a.r > 0.8f && a.r > 2.f * a.b;
            blue_coverage += a.b > 0.1f && a.b > 2.f * a.r;
            const bool isolated_red = isolated.r > 0.8f && isolated.r > 2.f * isolated.b;
            const bool composed_blue = a.b > 0.1f && a.b > 2.f * a.r;
            mesh_occluded_by_plane += isolated_red && composed_blue;
        }
    }
    INFO("order-dependent pixels=" << differing_pixels << ", max channel delta="
         << max_channel_delta << ", red coverage=" << red_coverage
         << ", blue coverage=" << blue_coverage
         << ", mesh pixels hidden by plane=" << mesh_occluded_by_plane);
    CHECK(red_coverage > 20);
    CHECK(blue_coverage > 20);
    CHECK(mesh_occluded_by_plane > 5);
    CHECK(differing_pixels == 0);
}

TEST_CASE("Prepared composition view matches typed program raw camera and shutter frames") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.schema = kRenderPlanSchemaV3;
    plan.job_id = "camera-video-compatibility-raw-parity";
    plan.canvas = {.width = 64, .height = 48, .fps = FrameRate{24, 1}, .duration = Frame{3}};
    plan.temporal_motion_blur = MotionBlurPlan{180.f, 4};

    CameraPlan first;
    first.position = {-120.f, 0.f, -1000.f};
    CameraPlan last = first;
    last.position[0] = 120.f;
    plan.camera = first;
    plan.camera_samples = std::vector<CameraSamplePlan>{{0.0, first}, {2.0, last}};

    LayerPlan near_card;
    near_card.id = "near-card";
    near_card.type = LayerType::Color;
    near_card.color = {0.92f, 0.24f, 0.12f, 1.f};
    near_card.position = {0.f, 0.f, 0.f};
    near_card.position_dimensions = 3;
    near_card.size = {640.f, 440.f};
    near_card.size_dimensions = 2;
    near_card.enable_3d = true;
    plan.layers.push_back(near_card);

    LayerPlan far_card = near_card;
    far_card.id = "far-card";
    far_card.color = {0.12f, 0.42f, 0.92f, 1.f};
    far_card.position = {180.f, 0.f, 120.f};
    plan.layers.push_back(far_card);

    assets::AssetResolver resolver;
    auto program = compiler::compile(plan, resolver);
    REQUIRE(program);
    auto prepared = compile_render_plan(plan, resolver, {}, &*program);
    REQUIRE(prepared);

    auto typed_renderer = test::make_renderer();
    auto compatibility_renderer = test::make_renderer();
    for (int frame : {0, 1, 2}) {
        INFO("frame=" << frame);
        const auto typed = typed_renderer.render_program(*program, Frame{frame});
        REQUIRE(typed);
        const auto compatibility = compatibility_renderer.render_compiled(prepared->compiled_composition, Frame{frame});
        REQUIRE(compatibility);
        CHECK(test::framebuffer_hash(*typed) == test::framebuffer_hash(*compatibility));
    }
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
            {"id", "plane"}, {"type", "color"}, {"color", {0.8, 0.2, 0.1, 1.0}},
            {"transform_samples", nlohmann::json::array({{
                {"frame", 0.123456789012345}, {"position", {2., 3., 4.}},
                {"scale", {1., 1., 1.}}, {"rotation_deg", {0., 0., 0.}}, {"opacity", .75}
            }})}
        }})},
        {"output", {{"path", "resolved.png"}, {"format", "png"}}}
    };
    const auto decoded = decode_render_plan(json);
    INFO((decoded ? "" : decoded.error().message));
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
    // A runtime override must not turn an exact Motion bake into an
    // interpolated approximation at a different shutter calendar.
    auto mismatched = settings;
    mismatched.motion_blur.shutter_phase_deg = 0.f;
    renderer.set_settings(mismatched);
    CHECK_THROWS_AS(renderer.render_program(*offline, Frame{1}), std::invalid_argument);
    CHECK_THROWS_AS(renderer.render_program(*runtime, Frame{1}), std::invalid_argument);
    mismatched = settings;
    mismatched.motion_blur.pattern = TemporalSamplePattern::Uniform;
    renderer.set_settings(mismatched);
    CHECK_THROWS_AS(renderer.render_program(*offline, Frame{1}), std::invalid_argument);
    auto static_camera_plan = plan;
    static_camera_plan.camera_samples.reset();
    const auto layer_only = compiler::compile(static_camera_plan, resolver);
    REQUIRE(layer_only);
    CHECK_THROWS_AS(renderer.render_program(*layer_only, Frame{1}), std::invalid_argument);
    mismatched = settings;
    ++mismatched.motion_blur.jitter_seed;
    renderer.set_settings(mismatched);
    CHECK_THROWS_AS(renderer.render_program(*offline, Frame{1}), std::invalid_argument);
    renderer.set_settings(settings);
    auto invalid = plan;
    invalid.layers[0].transform_samples[1].frame = invalid.layers[0].transform_samples[0].frame;
    CHECK_FALSE(compiler::compile(invalid, resolver));
    CHECK_FALSE(compile_render_plan(invalid, resolver));

    // A sorted, otherwise valid bake may still omit a shutter instant. Do
    // not silently interpolate it and claim exact nonlinear Motion sampling.
    for (const std::size_t index : {std::size_t{0}, times.size() / 2, times.size() - 1}) {
        auto incomplete = plan;
        incomplete.layers[0].transform_samples.erase(incomplete.layers[0].transform_samples.begin() + index);
        const auto typed = compiler::compile(incomplete, resolver);
        REQUIRE_FALSE(typed);
        CHECK(typed.error().message.find("missing a required resolved shutter or presentation sample") != std::string::npos);
        const auto prepared = compile_render_plan(incomplete, resolver);
        REQUIRE_FALSE(prepared);
        CHECK(prepared.error().code == "incomplete_resolved_shutter_samples");
        incomplete.camera_samples.reset();
        CHECK_FALSE(compiler::compile(incomplete, resolver, options));
    }
    // Camera and layers form one exact bake: missing camera instants must
    // fail at both compiler boundaries rather than being interpolated.
    for (const std::size_t index : {std::size_t{0}, times.size() / 2, times.size() - 1}) {
        auto incomplete = plan;
        incomplete.camera_samples->erase(incomplete.camera_samples->begin() + index);
        const auto typed = compiler::compile(incomplete, resolver);
        REQUIRE_FALSE(typed);
        CHECK(typed.error().message.find("camera is missing a required resolved shutter or presentation sample") != std::string::npos);
        const auto prepared = compile_render_plan(incomplete, resolver);
        REQUIRE_FALSE(prepared);
        CHECK(prepared.error().message.find("camera is missing a required resolved shutter or presentation sample") != std::string::npos);
    }
    auto legacy = plan;
    legacy.layers[0].transform_samples.clear();
    legacy.camera_samples->erase(legacy.camera_samples->begin() + times.size() / 2);
    CHECK(compiler::compile(legacy, resolver));
    CHECK(compile_render_plan(legacy, resolver));

    for (const auto type : {LayerType::Image, LayerType::Video, LayerType::Color, LayerType::Text,
                            LayerType::Shape, LayerType::Particles, LayerType::Adjustment}) {
        LayerPlan visual;
        visual.type = type;
        visual.transform_samples.push_back({0.0, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f},
                                            {0.f, 0.f, 0.f}, 1.f});
        CHECK_FALSE(transform_samples_problem(visual));
    }
    for (const auto type : {LayerType::Light, LayerType::Mesh}) {
        LayerPlan nonVisual;
        nonVisual.type = type;
        nonVisual.transform_samples.push_back({0.0, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f},
                                               {0.f, 0.f, 0.f}, 1.f});
        CHECK(transform_samples_problem(nonVisual).has_value());
    }
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
    camera.iris_blades = 6;
    rejected(camera);
    // Highlight weighting has a kernel on every DOF path, so the same boundary
    // must accept it instead of failing closed on an unimplemented shape.
    camera = CameraPlan{};
    camera.dof_highlight_gain = 1.f;
    CHECK_FALSE(camera_contract_problem(camera).has_value());
    {
        auto accepted = plan;
        accepted.camera = camera;
        CHECK(compiler::compile(accepted, resolver));
        CHECK(compile_render_plan(accepted, resolver));
    }
    for (float gain : {-0.01f, 8.01f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()}) {
        camera = CameraPlan{};
        camera.dof_highlight_gain = gain;
        rejected(camera);
    }
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

TEST_CASE("Camera contract frontier matrix accepts and rejects identical payloads") {
    using namespace chronon3d::render_plan;
    RenderPlan plan;
    plan.job_id = "camera-contract-frontier";
    plan.canvas = {.width = 96, .height = 64, .fps = FrameRate{30, 1}, .duration = Frame{1}};
    plan.camera = CameraPlan{};
    LayerPlan layer;
    layer.id = "subject";
    layer.type = LayerType::Color;
    plan.layers.push_back(layer);
    assets::AssetResolver resolver;

    const auto accepted = [&](const CameraPlan& camera, const char* label) {
        INFO(label);
        CHECK_FALSE(camera_contract_problem(camera));
        auto candidate = plan;
        candidate.camera = camera;
        CHECK(compiler::compile(candidate, resolver));
        CHECK(compile_render_plan(candidate, resolver));
    };
    const auto rejected = [&](const CameraPlan& camera, const char* label) {
        INFO(label);
        CHECK(camera_contract_problem(camera).has_value());
        auto candidate = plan;
        candidate.camera = camera;
        CHECK_FALSE(compiler::compile(candidate, resolver));
        CHECK_FALSE(compile_render_plan(candidate, resolver));
        candidate.camera.reset();
        candidate.camera_samples = std::vector<CameraSamplePlan>{{0.0, camera}};
        CHECK_FALSE(compiler::compile(candidate, resolver));
        CHECK_FALSE(compile_render_plan(candidate, resolver));
    };

    CameraPlan camera;
    accepted(camera, "baseline");
    for (float fov : {10.f, 120.f}) {
        camera = CameraPlan{};
        camera.fov_deg = fov;
        accepted(camera, "fov endpoint");
    }
    for (float focal : {0.f, 12.f, 200.f}) {
        camera = CameraPlan{};
        camera.focal_length_mm = focal;
        accepted(camera, "focal endpoint");
    }
    for (float sensor : {1.f, 100.f}) {
        camera = CameraPlan{};
        camera.sensor_width_mm = sensor;
        camera.sensor_height_mm = sensor;
        accepted(camera, "sensor endpoint");
    }
    for (float stop : {0.f, 0.7f, 32.f}) {
        camera = CameraPlan{};
        camera.f_stop = stop;
        accepted(camera, "f-stop endpoint");
    }
    for (float focus : {1e-6f, 1e7f}) {
        camera = CameraPlan{};
        camera.dof_focus_distance = focus;
        accepted(camera, "focus endpoint");
    }
    for (float blur : {0.f, 256.f}) {
        camera = CameraPlan{};
        camera.dof_max_blur = blur;
        accepted(camera, "max blur endpoint");
    }
    for (float shift : {-1.f, 1.f}) {
        camera = CameraPlan{};
        camera.lens_shift = {shift, -shift};
        accepted(camera, "lens shift endpoint");
    }
    for (float gain : {0.f, 8.f}) {
        camera = CameraPlan{};
        camera.dof_highlight_gain = gain;
        accepted(camera, "highlight gain endpoint");
    }
    camera = CameraPlan{};
    camera.type = "orthographic";
    camera.zoom = 2.f;
    accepted(camera, "orthographic zoom");

    // Every admitted renderer-facing field reaches the adapter unchanged.
    CameraPlan consumed;
    consumed.focal_length_mm = 200.f;
    consumed.sensor_width_mm = 100.f;
    consumed.sensor_height_mm = 100.f;
    consumed.lens_shift = {1.f, -1.f};
    consumed.f_stop = 32.f;
    consumed.dof_enabled = true;
    consumed.dof_focus_distance = 1e7f;
    consumed.dof_max_blur = 256.f;
    consumed.dof_highlight_gain = 8.f;
    const auto value = chronon3d::render_plan::detail::make_camera_value(consumed, plan.canvas);
    CHECK(value.optics_mode == CameraOpticsMode::PhysicalLens);
    CHECK(value.lens.focal_length == 200.f);
    CHECK(value.lens.sensor_width == 100.f);
    CHECK(value.lens.sensor_height == 100.f);
    CHECK(value.lens.f_stop == 32.f);
    CHECK(value.lens_shift == Vec2(1.f, -1.f));
    CHECK(value.dof.enabled);
    CHECK(value.dof.focus_distance == 1e7f);
    CHECK(value.dof.max_blur == 256.f);
    CHECK(value.dof.highlight_gain == 8.f);

    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float fov : {9.99f, 120.01f, inf, nan}) {
        camera = CameraPlan{};
        camera.fov_deg = fov;
        rejected(camera, "fov out of range");
    }
    for (float focal : {11.99f, 200.01f, inf, nan}) {
        camera = CameraPlan{};
        camera.focal_length_mm = focal;
        rejected(camera, "focal out of range");
    }
    for (float sensor : {0.99f, 100.01f, inf, nan}) {
        camera = CameraPlan{};
        camera.sensor_width_mm = sensor;
        rejected(camera, "sensor out of range");
    }
    for (float stop : {0.69f, 32.01f, inf, nan}) {
        camera = CameraPlan{};
        camera.f_stop = stop;
        rejected(camera, "f-stop out of range");
    }
    for (float focus : {0.f, -1.f, 10000001.f, inf, nan}) {
        camera = CameraPlan{};
        camera.dof_focus_distance = focus;
        rejected(camera, "focus out of range");
    }
    for (float blur : {-0.01f, 256.01f, inf, nan}) {
        camera = CameraPlan{};
        camera.dof_max_blur = blur;
        rejected(camera, "max blur out of range");
    }
    for (float gain : {-0.01f, 8.01f, inf, nan}) {
        camera = CameraPlan{};
        camera.dof_highlight_gain = gain;
        rejected(camera, "highlight gain out of range");
    }
    for (float shift : {1.01f, -1.01f, nan}) {
        camera = CameraPlan{};
        camera.lens_shift = {shift, 0.f};
        rejected(camera, "lens shift out of range");
    }
    camera = CameraPlan{};
    camera.type = "fisheye";
    rejected(camera, "unsupported projection type");
    camera = CameraPlan{};
    camera.iris_blades = 1;
    rejected(camera, "polygonal iris");
    camera = CameraPlan{};
    camera.near = 0.f;
    rejected(camera, "zero near plane");
    camera = CameraPlan{};
    camera.far = camera.near;
    rejected(camera, "empty clipping interval");
    camera = CameraPlan{};
    camera.zoom = 0.f;
    rejected(camera, "non-positive zoom");
    camera = CameraPlan{};
    camera.position[1] = inf;
    rejected(camera, "non-finite position");
    camera = CameraPlan{};
    camera.rotation_deg[2] = nan;
    rejected(camera, "non-finite rotation");
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

    // A foreground plate must not discard the oversized background outside its
    // projected bounds, and fractional alpha must still reveal the background.
    Framebuffer foreground(64, 64);
    foreground.clear(Color{1.f, 0.f, 0.f, 0.5f});
    REQUIRE(save_png(foreground, (root / "foreground.png").string()));
    LayerPlan overlay;
    overlay.id = "foreground";
    overlay.type = LayerType::Image;
    overlay.asset = (root / "foreground.png").string();
    overlay.size = {32.f, 32.f};
    overlay.size_dimensions = 2;
    overlay.position = {0.f, 0.f, 0.f};
    overlay.position_dimensions = 3;
    overlay.enable_3d = true;
    plan.layers.push_back(overlay);
    const auto composed = compile_render_plan(plan, resolver);
    const std::string composed_error = composed ? std::string{} : composed.error().message;
    REQUIRE_MESSAGE(composed, composed_error);
    const auto composed_frame = renderer.render_compiled(composed->compiled_composition, Frame{0});
    REQUIRE(composed_frame);
    const auto corner = composed_frame->get_pixel(4, 4);
    CHECK(corner.a == doctest::Approx(1.f));
    CHECK(corner.b > corner.r);
    const auto centre = composed_frame->get_pixel(48, 32);
    CHECK(centre.a == doctest::Approx(1.f));
    CHECK(centre.r > corner.r);
    CHECK(centre.b > 0.05f);
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

TEST_CASE("Full Optical Studio v3 provider vs offline production render equivalence") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;

    std::filesystem::path plan_path = "docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json";
    if (!std::filesystem::exists(plan_path))
        plan_path = "Chronon3d/docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json";
    if (!std::filesystem::exists(plan_path))
        plan_path = "../../../docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json";
    if (!std::filesystem::exists(plan_path))
        plan_path = "../../../../Chronon3d/docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json";

    REQUIRE(std::filesystem::exists(plan_path));

    std::ifstream file(plan_path);
    REQUIRE(file.is_open());
    const auto json = nlohmann::ordered_json::parse(file);
    const auto decoded = decode_render_plan(json);
    REQUIRE(decoded);

    const auto& offline_plan = *decoded;
    REQUIRE(offline_plan.camera.has_value());
    REQUIRE(offline_plan.camera_samples.has_value());
    REQUIRE(offline_plan.camera_samples->size() == 1350);

    // Build sample lookup table matching the exact motion authoring evaluator
    std::map<double, CameraPlan> sample_lut;
    for (const auto& s : *offline_plan.camera_samples) {
        sample_lut[s.frame] = s.camera;
    }

    // Prepare runtime plan without pre-baked camera samples
    auto runtime_plan = offline_plan;
    runtime_plan.camera_samples.reset();

    RuntimeCameraSampleProvider provider{
        .fingerprint = "optical-studio-v3-runtime-provider",
        .sample = [&sample_lut](double frame) {
            auto it = sample_lut.find(frame);
            if (it != sample_lut.end()) return it->second;
            for (const auto& [t, c] : sample_lut) {
                if (std::abs(t - frame) < 1e-4) return c;
            }
            throw std::runtime_error("runtime camera provider: unexpected timestamp " + std::to_string(frame));
        }
    };

    const auto assets_root = std::filesystem::absolute(plan_path).parent_path().parent_path().parent_path().parent_path();
    assets::AssetResolver resolver;
    resolver.mount(assets_root);

    // Offline compilation (consumes baked samples)
    const auto offline_prog = compiler::compile(offline_plan, resolver);
    INFO((offline_prog ? "" : offline_prog.error().message));
    REQUIRE(offline_prog);

    // Runtime compilation (invokes live provider callback for each shutter subframe)
    compiler::CompileOptions options;
    options.runtime_camera = &provider;
    const auto runtime_prog = compiler::compile(runtime_plan, resolver, options);
    INFO((runtime_prog ? "" : runtime_prog.error().message));
    REQUIRE(runtime_prog);

    // Identity check: the live runtime provider compiles into the exact same program fingerprint
    CHECK(offline_prog->fingerprint() == runtime_prog->fingerprint());
    CHECK(offline_prog->nodes().size() == runtime_prog->nodes().size());
    CHECK(offline_prog->layers().size() == runtime_prog->layers().size());
    CHECK(offline_prog->passes().size() == runtime_prog->passes().size());

    // Sample across the acceptance sentinels (frames 30, 75, 149) and verify exact parameter blocks
    for (int sentinel_frame : {30, 75, 149}) {
        compiler::FrameParameterBlock offline_params;
        compiler::FrameParameterBlock runtime_params;
        compiler::TypedProgramSampler{}.sample_into(*offline_prog, sentinel_frame, offline_params);
        compiler::TypedProgramSampler{}.sample_into(*runtime_prog, sentinel_frame, runtime_params);

        CHECK(offline_params.layers.size() == runtime_params.layers.size());
        for (std::size_t i = 0; i < offline_params.layers.size(); ++i) {
            CHECK(offline_params.layers[i].matrix == runtime_params.layers[i].matrix);
            CHECK(offline_params.layers[i].opacity == runtime_params.layers[i].opacity);
        }
    }

    // Fail-closed checks:
    // 1. Offline plan with pre-existing camera_samples rejects runtime_camera
    compiler::CompileOptions conflict_options;
    conflict_options.runtime_camera = &provider;
    const auto conflict_result = compiler::compile(offline_plan, resolver, conflict_options);
    CHECK_FALSE(conflict_result);
    CHECK(conflict_result.error().message.find("runtime sampling requires a base camera, callable provider, stable identity and no offline samples") != std::string::npos);

    // 2. Runtime provider with empty fingerprint or null sample fails closed
    RuntimeCameraSampleProvider empty_provider;
    compiler::CompileOptions invalid_options;
    invalid_options.runtime_camera = &empty_provider;
    const auto invalid_result = compiler::compile(runtime_plan, resolver, invalid_options);
    CHECK_FALSE(invalid_result);
}
