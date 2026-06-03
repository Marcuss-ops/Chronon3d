#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_rig.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

struct ComparisonResult {
    bool success{true};
    float max_channel_diff{0.0f};
    float mean_error{0.0f};
    float mismatch_percentage{0.0f};
    std::string error_message;
};

ComparisonResult compare_images(const Framebuffer& rendered, const Framebuffer& golden) {
    ComparisonResult res;
    if (rendered.width() != golden.width() || rendered.height() != golden.height()) {
        res.success = false;
        res.error_message = "Dimension mismatch";
        return res;
    }

    double total_channel_diff = 0.0;
    int mismatched_pixels = 0;
    const int total_pixels = rendered.width() * rendered.height();

    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            const Color c1 = rendered.get_pixel(x, y).to_srgb();
            const Color c2 = golden.get_pixel(x, y);

            const float dr = std::abs(c1.r - c2.r);
            const float dg = std::abs(c1.g - c2.g);
            const float db = std::abs(c1.b - c2.b);
            const float da = std::abs(c1.a - c2.a);

            const float max_diff = std::max({dr, dg, db, da});
            res.max_channel_diff = std::max(res.max_channel_diff, max_diff);
            total_channel_diff += dr + dg + db + da;

            if (max_diff > 4.0f / 255.0f) {
                ++mismatched_pixels;
            }
        }
    }

    res.mean_error = static_cast<float>(total_channel_diff / (total_pixels * 4));
    res.mismatch_percentage = static_cast<float>(mismatched_pixels) / static_cast<float>(total_pixels);

    if (res.mismatch_percentage > 0.08f || res.mean_error >= 0.02f || res.max_channel_diff >= 100.0f / 255.0f) {
        res.success = false;
        res.error_message = "Threshold exceeded";
    }

    return res;
}

void verify_golden_or_create(const Framebuffer& rendered, const std::string& filename) {
    const std::filesystem::path golden_dir = "test_renders/golden/camera_rig";
    std::filesystem::create_directories(golden_dir);
    const std::filesystem::path golden_path = golden_dir / filename;

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(rendered, golden_path.string()));
        return;
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    const auto comp = compare_images(rendered, *golden);
    INFO(comp.error_message);
    CHECK(comp.success);
}

Composition make_center_composition() {
    return Composition({.name = "CameraRigCenter", .width = 960, .height = 540, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.18f);
        s.directional_light({0.25f, 1.0f, 0.45f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.8f);

        camera_rig::CameraRig rig;
        rig.controller_name = "rig_controller";
        rig.target_name = "rig_target";
        rig.controller_position = Vec3{140.0f, 18.0f, 0.0f};
        rig.camera_position = Vec3{0.0f, 0.0f, -1100.0f};
        rig.zoom.set(1000.0f);

        rig.apply(s, ctx.frame, [&](SceneBuilder& scene) {
            scene.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                    .grid_color = {0.28f, 0.48f, 0.98f, 0.08f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            scene.layer("subject", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f}).glow(GlowPresets::neon_blue(16.0f));
                l.rounded_rect("card", {
                    .size = {320.0f, 128.0f},
                    .radius = 26.0f,
                    .color = Color{0.12f, 0.02f, 0.18f, 1.0f}
                });
            });
        });

        return s.build();
    });
}

Composition make_orbit_composition() {
    return Composition({.name = "CameraRigOrbit", .width = 960, .height = 540, .duration = 121}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.16f);
        s.directional_light({-0.15f, 1.0f, -0.55f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.88f);

        camera_rig::CameraRig rig;
        rig.controller_name = "rig_controller";
        rig.target_name = "rig_target";
        rig.controller_position
            .key(0, Vec3{110.0f, 18.0f, 0.0f})
            .key(120, Vec3{180.0f, 28.0f, 0.0f});
        rig.controller_rotation
            .key(0, Vec3{0.0f, -18.0f, 0.0f})
            .key(120, Vec3{0.0f, 18.0f, 0.0f});
        rig.camera_position = Vec3{0.0f, 0.0f, -1120.0f};
        rig.zoom.set(1000.0f);

        rig.apply(s, ctx.frame, [&](SceneBuilder& scene) {
            scene.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.015f, 0.015f, 0.035f, 1.0f},
                    .grid_color = {0.18f, 0.75f, 1.0f, 0.09f},
                    .spacing = 50.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            scene.layer("subject", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("card", {
                    .size = {280.0f, 140.0f},
                    .radius = 22.0f,
                    .color = Color{0.99f, 0.44f, 0.82f, 1.0f}
                });
            });

            scene.layer("accent", [](LayerBuilder& l) {
                l.enable_3d().position({240.0f, -20.0f, -180.0f});
                l.circle("dot", {.radius = 24.0f, .color = Color{0.15f, 0.85f, 1.0f, 1.0f}});
            });
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("CameraRig: bakes two-node parent and target names") {
    camera_rig::CameraRig rig;
    rig.controller_name = "rig_controller";
    rig.target_name = "rig_target";
    rig.controller_position = Vec3{140.0f, 18.0f, 0.0f};
    rig.camera_position = Vec3{0.0f, 0.0f, -1100.0f};

    const Camera2_5D cam = rig.bake(0);

    CHECK(cam.enabled);
    CHECK(cam.parent_name == "rig_controller");
    CHECK(cam.target_name == "rig_target");
    CHECK(cam.point_of_interest_enabled);
    CHECK(cam.position.z == doctest::Approx(-1100.0f));
}

TEST_CASE("CameraRig: resolves target anchor through the camera target null") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    camera_rig::CameraRig rig;
    rig.controller_name = "rig_controller";
    rig.target_name = "rig_target";
    rig.controller_position = Vec3{140.0f, 18.0f, 0.0f};
    rig.controller_rotation = Vec3{0.0f, 0.0f, 0.0f};
    rig.target_position = Vec3{200.0f, 30.0f, 0.0f};
    rig.target_anchor = Vec3{40.0f, 20.0f, 0.0f};
    rig.camera_position = Vec3{0.0f, 0.0f, -1100.0f};

    rig.apply(s, 0, [](SceneBuilder&) {});

    auto scene = s.build();
    auto resolved_layers = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());
    REQUIRE(resolved_layers.size() == 2);

    auto resolved_cam = resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());
    CHECK(resolved_cam.camera.point_of_interest.x == doctest::Approx(200.0f).epsilon(0.0001f));
    CHECK(resolved_cam.camera.point_of_interest.y == doctest::Approx(30.0f).epsilon(0.0001f));
    CHECK(resolved_cam.camera.position.x == doctest::Approx(140.0f).epsilon(0.0001f));
    CHECK(resolved_cam.camera.position.z == doctest::Approx(-1100.0f).epsilon(0.0001f));
}

TEST_CASE("CameraRig: center golden render stays readable and centered") {
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(make_center_composition(), 0);
    REQUIRE(fb != nullptr);
    verify_golden_or_create(*fb, "center.png");
}

TEST_CASE("CameraRig: orbit golden render changes the view in a controlled way") {
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(make_orbit_composition(), 120);
    REQUIRE(fb != nullptr);
    verify_golden_or_create(*fb, "orbit_120.png");
}

