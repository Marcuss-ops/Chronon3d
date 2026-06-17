#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <chronon3d/scene/model/core/transform_resolver.hpp>
#include <cmath>
using namespace chronon3d;


TEST_CASE("Camera Shot Validator logic check") {
    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -1000.0f};
    camera.zoom = 1000.0f;

    Viewport viewport{1920.0f, 1080.0f};

    TransformResolverResult resolved;
    ResolvedTransform3D t_target;
    t_target.local.position = {0.0f, 0.0f, 0.0f};
    t_target.world_matrix = t_target.local.to_mat4();
    resolved.resolved["camera_target"] = t_target;

    ResolvedTransform3D t_front;
    t_front.local.position = {0.0f, 0.0f, -100.0f};
    t_front.world_matrix = t_front.local.to_mat4();
    resolved.resolved["front_card"] = t_front;

    ResolvedTransform3D t_back;
    t_back.local.position = {0.0f, 0.0f, 100.0f};
    t_back.world_matrix = t_back.local.to_mat4();
    resolved.resolved["back_card"] = t_back;

    CameraShotValidator validator;
    validator.register_layer_size("front_card", {300.0f, 190.0f})
             .register_layer_size("back_card", {400.0f, 250.0f})
             .require_target_centered("camera_target", 3.0f)
             .require_visible("front_card", 0.95f)
             .require_depth_order({"front_card", "back_card"});

    CameraShotReport report = validator.validate(camera, resolved, viewport);
    CHECK(report.passed);
    CHECK(report.target_center_error_px == doctest::Approx(0.0f));
}
