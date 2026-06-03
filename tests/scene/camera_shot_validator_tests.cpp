#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/layer/layer_hierarchy.hpp>
#include <cmath>

using namespace chronon3d;

TEST_CASE("Camera Projection basic world-to-screen") {
    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0, 0, -1000};
    camera.zoom = 1000.0f;

    Vec2 viewport{1920.0f, 1080.0f};

    Vec2 p = project_world_to_screen(Vec3{0, 0, 0}, camera, viewport);
    CHECK(p.x == doctest::Approx(960.0f));
    CHECK(p.y == doctest::Approx(540.0f));

    Vec2 pr = project_world_to_screen(Vec3{100, 0, 0}, camera, viewport);
    CHECK(pr.x == doctest::Approx(1060.0f));
    CHECK(pr.y == doctest::Approx(540.0f));
}

TEST_CASE("Camera Shot Validator verifies rules") {
    std::pmr::monotonic_buffer_resource res;
    SceneBuilder s(&res);

    s.camera().set({
        .enabled = true,
        .position = {0, 0, -1000},
        .zoom = 1000.0f
    });

    s.layer("front_card", [](LayerBuilder& l) {
        l.enable_3d()
         .position({0, 0, -100})
         .rect("front_rect", RectParams{.size = {200.0f, 200.0f}});
    });

    s.layer("back_card", [](LayerBuilder& l) {
        l.enable_3d()
         .position({0, 0, 100})
         .rect("back_rect", RectParams{.size = {200.0f, 200.0f}});
    });

    auto scene = s.build();
    auto resolved_layers = resolve_layer_hierarchy(scene.layers(), 0, scene.resource());

    CameraShotValidator validator(Vec2{1920.0f, 1080.0f});
    validator.require_target_centered("target", 2.0f);
    validator.require_visible("front_card", 0.95f);
    validator.require_inside_safe_area("front_card", 0.90f);
    validator.require_projected_area_order({"front_card", "back_card"});

    CameraShotReport report = validator.validate(scene.camera_2_5d(), resolved_layers, Vec3{0, 0, 0});

    CHECK(report.passed);
    CHECK(report.target_center_error_px == doctest::Approx(0.0f));
    CHECK(report.failures.empty());
}

TEST_CASE("Camera Path Metrics and JSON Export") {
    std::vector<CameraPathPoint> path = {
        {0, Vec3{-320.0f, 0.0f, -1200.0f}, 0.5f},
        {30, Vec3{-150.0f, 0.0f, -1050.0f}, 1.2f},
        {60, Vec3{0.0f, 0.0f, -900.0f}, 0.8f}
    };

    CameraPathMetrics metrics = compute_path_metrics(path);
    std::string json = export_path_debug_json(path, metrics);

    CHECK(metrics.target_center_error_over_time.size() == 3);
    CHECK(metrics.target_center_error_over_time[0] == doctest::Approx(0.5f));
    CHECK(metrics.path_smoothness > 0.0f);
    CHECK(metrics.velocity_continuity > 0.0f);
    CHECK(json.find("camera_path") != std::string::npos);
    CHECK(json.find("metrics") != std::string::npos);
}

