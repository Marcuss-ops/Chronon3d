#include <doctest/doctest.h>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <cmath>

using namespace chronon3d;

TEST_CASE("Camera2_5D projection: subject at z=0 has scale 1 with default zoom") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, 0};
    tr.scale = {1, 1, 1};

    auto out = project_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth == doctest::Approx(1000.0f));
    CHECK(out.perspective_scale == doctest::Approx(1.0f));
    CHECK(out.transform.scale.x == doctest::Approx(1.0f));
    CHECK(out.transform.scale.y == doctest::Approx(1.0f));
}

TEST_CASE("Camera2_5D projection: near layer appears larger") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, -500};
    tr.scale = {1, 1, 1};

    auto out = project_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth == doctest::Approx(500.0f));
    CHECK(out.perspective_scale == doctest::Approx(2.0f));
    CHECK(out.transform.scale.x == doctest::Approx(2.0f));
    CHECK(out.transform.scale.y == doctest::Approx(2.0f));
}

TEST_CASE("Camera2_5D projection: far layer appears smaller") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, 1000};
    tr.scale = {1, 1, 1};

    auto out = project_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth == doctest::Approx(2000.0f));
    CHECK(out.perspective_scale == doctest::Approx(0.5f));
    CHECK(out.transform.scale.x == doctest::Approx(0.5f));
    CHECK(out.transform.scale.y == doctest::Approx(0.5f));
}

TEST_CASE("Camera2_5D projection: layer behind camera is culled") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform tr;
    tr.position = {0, 0, -1200};

    auto out = project_layer_2_5d(tr, cam, 1280, 720);

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

    auto near0 = project_layer_2_5d(near_tr, cam0, 1280, 720);
    auto near1 = project_layer_2_5d(near_tr, cam1, 1280, 720);

    auto far0 = project_layer_2_5d(far_tr, cam0, 1280, 720);
    auto far1 = project_layer_2_5d(far_tr, cam1, 1280, 720);

    const f32 near_delta = std::abs(near1.transform.position.x - near0.transform.position.x);
    const f32 far_delta  = std::abs(far1.transform.position.x - far0.transform.position.x);

    CHECK(near_delta > far_delta);
}

TEST_CASE("Camera2_5D projection: wider FOV creates smaller focal length") {
    const f32 h = 720.0f;

    const f32 focal35 = focal_length_from_fov(h, 35.0f);
    const f32 focal70 = focal_length_from_fov(h, 70.0f);

    CHECK(focal35 > focal70);
}

TEST_CASE("Camera2_5D projection: FOV mode changes perspective scale") {
    Camera2_5D cam35;
    cam35.enabled = true;
    cam35.position = {0, 0, -1000};
    cam35.projection_mode = Camera2_5DProjectionMode::Fov;
    cam35.fov_deg = 35.0f;

    Camera2_5D cam70 = cam35;
    cam70.fov_deg = 70.0f;

    Transform tr;
    tr.position = {0, 0, 0};
    tr.scale = {1, 1, 1};

    auto out35 = project_layer_2_5d(tr, cam35, 1280, 720);
    auto out70 = project_layer_2_5d(tr, cam70, 1280, 720);

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

    auto out = project_layer_2_5d(tr, cam, 1280, 720);

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

    auto out = project_layer_2_5d(tr, cam, 1280, 720);

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

    auto out = project_layer_2_5d(tr, cam, 1280, 720);

    CHECK(out.visible);
    CHECK(out.depth > 0.0f);
}
