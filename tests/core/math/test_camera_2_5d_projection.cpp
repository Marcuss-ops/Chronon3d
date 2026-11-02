#include <doctest/doctest.h>
#include "core/math/camera_projection_test_helpers.hpp"
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/camera_projection_resolver.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <cmath>
using namespace chronon3d;

TEST_CASE("camera acceptance: a known card projects to the four canonical corners") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.optics_mode = CameraOpticsMode::Zoom;
    cam.zoom = 1000.0f;

    constexpr f32 width = 1920.0f;
    constexpr f32 height = 1080.0f;
    constexpr f32 card_width = 200.0f;
    constexpr f32 card_height = 150.0f;
    const Mat4 world = glm::translate(Mat4(1.0f), Vec3{100.0f, 0.0f, 0.0f})
                     * glm::rotate(Mat4(1.0f), glm::radians(35.0f), Vec3{1.0f, 0.0f, 0.0f});

    CameraProjectionInput input;
    input.world_transform = world;
    input.layer_size = {card_width, card_height};
    input.camera = cam;
    input.viewport = {width, height};
    const auto projected = CameraProjectionResolver::project_layer(input);
    REQUIRE(projected.visible);
    REQUIRE(projected.corner_count == 4);

    const auto projected_layer = project_layer_2_5d(
        from_mat4(world), world, cam, width, height, false,
        {card_width, card_height}, BackfaceMode::DoubleSided);
    REQUIRE(projected_layer.visible);

    // The render path historically consumed this bbox-derived TRS. A tilted
    // perspective card is a homography, so all four authored corners must be
    // preserved; +100 in X must also remain a single +100 world translation.
    // This acceptance assertion locks the projection matrix as the sole
    // placement value; no bbox-derived TRS may approximate these corners.
    const Mat4 canonical_matrix = projected_layer.projection_matrix;
    for (int i = 0; i < 4; ++i) {
        const Vec4 local[4] = {
            {-card_width * 0.5f, -card_height * 0.5f, 0.0f, 1.0f},
            { card_width * 0.5f, -card_height * 0.5f, 0.0f, 1.0f},
            { card_width * 0.5f,  card_height * 0.5f, 0.0f, 1.0f},
            {-card_width * 0.5f,  card_height * 0.5f, 0.0f, 1.0f},
        };
        const Vec4 clip = canonical_matrix * local[i];
        const Vec2 canonical{clip.x / clip.w, clip.y / clip.w};
        INFO("corner " << i << " matrix=(" << canonical.x << ", " << canonical.y
             << ") resolver=(" << projected.corners[i].x << ", "
             << projected.corners[i].y << ")");
        CHECK(canonical.x == doctest::Approx(projected.corners[i].x).epsilon(0.01));
        CHECK(canonical.y == doctest::Approx(projected.corners[i].y).epsilon(0.01));
    }
}


TEST_CASE("Camera2_5D projection: subject at z=0 has scale 1 with default zoom") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, 0};
    tr.scale = {1, 1, 1};

    auto out = project_test_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth == doctest::Approx(1000.0f));
    CHECK(out.perspective_scale == doctest::Approx(1.0f));
    CHECK(out.perspective_scale == doctest::Approx(1.0f));
}

TEST_CASE("Camera2_5D projection: near layer appears larger") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, -500};
    tr.scale = {1, 1, 1};

    auto out = project_test_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth == doctest::Approx(500.0f));
    CHECK(out.perspective_scale == doctest::Approx(2.0f));
    CHECK(out.perspective_scale == doctest::Approx(2.0f));
}

TEST_CASE("Camera2_5D projection: far layer appears smaller") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, 1000};
    tr.scale = {1, 1, 1};

    auto out = project_test_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth == doctest::Approx(2000.0f));
    CHECK(out.perspective_scale == doctest::Approx(0.5f));
    CHECK(out.perspective_scale == doctest::Approx(0.5f));
}

TEST_CASE("Camera2_5D projection: surface bounds are independent of TRS scale") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.scale = {1.0f, 1.0f, 1.0f};
    CameraProjectionInput small;
    small.world_transform = glm::translate(Mat4(1.0f), Vec3{0.0f, 0.0f, -1000.0f});
    small.layer_size = {1.0f, 1.0f};
    small.camera = cam;
    small.viewport = {1280.0f, 720.0f};

    auto large = small;
    large.layer_size = {800.0f, 400.0f};
    large.world_transform = glm::translate(Mat4(1.0f), Vec3{0.0f, 0.0f, -1000.0f})
                          * glm::rotate(Mat4(1.0f), glm::radians(80.0f), Vec3{1.0f, 0.0f, 0.0f});
    small.world_transform = glm::translate(Mat4(1.0f), Vec3{0.0f, 0.0f, -1000.0f})
                          * glm::rotate(Mat4(1.0f), glm::radians(80.0f), Vec3{1.0f, 0.0f, 0.0f});

    const auto small_projection = CameraProjectionResolver::project_layer(small);
    const auto large_projection = CameraProjectionResolver::project_layer(large);
    REQUIRE(small_projection.visible);
    REQUIRE(large_projection.visible);
    // The resolver must clip/project the declared surface, not infer its
    // geometry from the TRS scale.  A large card rotated through the same
    // near-edge pose therefore has a materially different projected height.
    const auto projected_height = [](const ProjectedLayer& layer) {
        f32 min_y = layer.corners[0].y;
        f32 max_y = layer.corners[0].y;
        for (int i = 1; i < layer.corner_count; ++i) {
            min_y = std::min(min_y, layer.corners[i].y);
            max_y = std::max(max_y, layer.corners[i].y);
        }
        return max_y - min_y;
    };
    CHECK(projected_height(large_projection) > projected_height(small_projection) * 10.0f);
}

TEST_CASE("Camera2_5D projection: layer behind camera is culled") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, -1200};

    auto out = project_test_layer_2_5d(tr, cam, 1280, 720);

    CHECK_FALSE(out.visible);
}

TEST_CASE("Camera2_5D projection: camera pan affects near layer more than far layer") {
    Camera2_5D cam0;
    cam0.enabled = true;
    cam0.position = {0, 0, -1000};
    cam0.zoom = 1000.0f;

    Camera2_5D cam1 = cam0;
    cam1.position.x = 100.0f;

    Transform near_tr;
    near_tr.position = {0, 0, -500};

    Transform far_tr;
    far_tr.position = {0, 0, 1000};

    auto near0 = project_test_layer_2_5d(near_tr, cam0, 1280, 720);
    auto near1 = project_test_layer_2_5d(near_tr, cam1, 1280, 720);

    auto far0 = project_test_layer_2_5d(far_tr, cam0, 1280, 720);
    auto far1 = project_test_layer_2_5d(far_tr, cam1, 1280, 720);

    const auto projected_origin_x = [](const ProjectedLayer2_5D& layer) {
        const Vec4 p = layer.projection_matrix * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
        return p.x / p.w;
    };
    const f32 near_delta = std::abs(projected_origin_x(near1) - projected_origin_x(near0));
    const f32 far_delta  = std::abs(projected_origin_x(far1) - projected_origin_x(far0));

    CHECK(near_delta > far_delta);
}

// TICKET-007.w (gate-compliance metadata — see docs/FOLLOWUP_TICKETS.md).
//   Owner: chronon3d-owners.
//   Motivation: keep the FOV optical mode responsive to authored FOV changes.
//
//   Data introduzione: 2026-06-20.  Deadline rimozione: 2026-09-30.
// Re-enabled in PR-C after impl-side fix in
// chronon3d::camera_math::focal_from_camera(): prior code always returned
// camera.zoom (default 1000) when projection_mode was Fov because the zoom
// fallback fired before the projection_mode branch. Now projection_mode=Fov
// takes priority, giving focal = (h/2)/tan(fov/2) correctly.
TEST_CASE("Camera2_5D projection: wider FOV creates smaller focal length") {
    Camera2_5D cam35;
    Camera2_5D cam70;
    cam35.optics_mode = CameraOpticsMode::FieldOfView;
    cam70.optics_mode = CameraOpticsMode::FieldOfView;
    cam35.fov_deg = 35.0f;
    cam70.fov_deg = 70.0f;
    const f32 focal35 = camera_math::focal_from_camera(cam35, 1280.0f, 720.0f);
    const f32 focal70 = camera_math::focal_from_camera(cam70, 1280.0f, 720.0f);

    CHECK(focal35 > focal70);
}

TEST_CASE("camera focal axes follow one gate-fit contract") {
    Camera2_5D camera;
    camera.optics_mode = CameraOpticsMode::PhysicalLens;
    camera.lens.focal_length = 50.0f;
    camera.lens.sensor_width = 36.0f;
    camera.lens.sensor_height = 36.0f;
    camera.lens.gate_fit = GateFit::Fill;
    const auto square = camera_math::focal_xy_from_camera(camera, 1000.0f, 1000.0f);
    CHECK(square.x == doctest::Approx(square.y).epsilon(1e-6));

    camera.lens.gate_fit = GateFit::Stretch;
    const auto stretched = camera_math::focal_xy_from_camera(camera, 1920.0f, 1080.0f);
    CHECK(stretched.x / stretched.y == doctest::Approx(1920.0f / 1080.0f).epsilon(1e-6));
}

TEST_CASE("camera authoring defaults use the canonical vertical FOV") {
    CHECK(Camera{}.fov_deg == doctest::Approx(Camera2_5D{}.fov_deg));
}

// TICKET-007.x (gate-compliance metadata — see docs/FOLLOWUP_TICKETS.md).
//   Owner: chronon3d-owners.
//   Motivation: pre-existing rot; perspective_scale dependency on FOV mode.
//
//   Data introduzione: 2026-06-20.  Deadline rimozione: 2026-09-30.
// Re-enabled in PR-C after impl-side fix in
// chronon3d::camera_math::focal_from_camera(): projection_mode=Fov now
// produces a focal length that scales linearly with FOV, so
// perspective_scale = focal/depth tracks FOV correctly.
TEST_CASE("Camera2_5D projection: FOV mode changes perspective scale") {
    Camera2_5D cam35;
    cam35.enabled = true;
    cam35.optics_mode = CameraOpticsMode::FieldOfView;
    cam35.position = {0, 0, -1000};
    cam35.fov_deg = 35.0f;

    Camera2_5D cam70 = cam35;
    cam70.fov_deg = 70.0f;

    Transform tr;
    tr.position = {0, 0, 0};
    tr.scale = {1, 1, 1};

    auto out35 = project_test_layer_2_5d(tr, cam35, 1280, 720);
    auto out70 = project_test_layer_2_5d(tr, cam70, 1280, 720);

    CHECK(out35.visible);
    CHECK(out70.visible);
    CHECK(out35.perspective_scale > out70.perspective_scale);
}

TEST_CASE("Camera2_5D projection: camera rotation affects the projection matrix") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;
    cam.rotation = {0.0f, 0.0f, 15.0f};

    Transform tr;
    tr.position = {100, 0, 0};
    tr.scale = {1, 1, 1};

    auto out = project_test_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(std::abs(out.projection_matrix[0][1]) > 0.0001f);
}

TEST_CASE("Camera2_5D projection: camera tilt produces a projective warp") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;
    cam.rotation = {20.0f, 0.0f, 0.0f};

    Transform tr;
    tr.position = {0, 0, 0};
    tr.scale = {1, 1, 1};

    auto out = project_test_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    const bool has_offset = std::abs(out.projection_matrix[3][0]) > 0.0001f
                         || std::abs(out.projection_matrix[3][1]) > 0.0001f;
    CHECK(has_offset);
}

TEST_CASE("Camera2_5D projection: rotated camera still keeps front layers visible") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;
    cam.rotation = {0.0f, 20.0f, 0.0f};

    Transform tr;
    tr.position = {0, 0, 0};
    tr.scale = {1, 1, 1};

    auto out = project_test_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth > 0.0f);
}

TEST_CASE("Camera2_5D projection: direct point projection keeps X and Y orientation") {
    // Routed through the single projection authority (the renderer contract);
    // the legacy project_world_point_2_5d helper was removed.
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, 0.0f};
    cam.zoom = 1000.0f;

    const camera_math::Viewport2D viewport{1280.0f, 720.0f};

    const auto center = camera_math::project_world_point(
        cam, Vec3{0.0f, 0.0f, 1000.0f}, viewport);
    CHECK(center.visible);
    CHECK(center.depth == doctest::Approx(1000.0f));
    CHECK(center.screen.x == doctest::Approx(640.0f));
    CHECK(center.screen.y == doctest::Approx(360.0f));

    const auto right = camera_math::project_world_point(
        cam, Vec3{100.0f, 0.0f, 1000.0f}, viewport);
    CHECK(right.visible);
    CHECK(right.screen.x > 640.0f);

    const auto up = camera_math::project_world_point(
        cam, Vec3{0.0f, 100.0f, 1000.0f}, viewport);
    CHECK(up.visible);
    // Contract Y sign: inverted — a point above camera centre (world Y > 0)
    // projects above centre in screen coordinates.
    CHECK(up.screen.y < 360.0f);
}
