#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_framing.hpp>
#include <chronon3d/scene/transform/transform_resolver.hpp>

using namespace chronon3d;

TEST_CASE("Camera Framing logic fit") {
    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -500.0f};
    camera.point_of_interest = {0.0f, 0.0f, 0.0f};
    camera.point_of_interest_enabled = true;
    camera.zoom = 1000.0f;

    Viewport viewport{1920.0f, 1080.0f};

    TransformResolverResult resolved;
    ResolvedTransform3D t_large;
    t_large.local.position = {0.0f, 0.0f, 0.0f};
    t_large.local.scale = {10.0f, 10.0f, 1.0f};
    t_large.world_matrix = t_large.local.to_mat4();
    resolved.resolved["large_card"] = t_large;

    CameraFramingOptions options;
    options.max_iterations = 5;
    options.dolly_step = 100.0f;

    Camera2_5D fitted = fit_camera_to_layers(camera, {"large_card"}, resolved, viewport, options);
    
    CHECK(fitted.position.z < camera.position.z);
}
