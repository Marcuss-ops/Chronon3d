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

// TICKET-007.w (gate-compliance metadata — see docs/FOLLOWUP_TICKETS.md).
//   Owner: chronon3d-owners.
//   Motivation: pre-existing rot; focal_length_from_fov doesn't differentiate FOV values.
//
//   Data introduzione: 2026-06-20.  Deadline rimozione: 2026-09-30.
// Re-enabled in PR-C after impl-side fix in
// chronon3d::camera_math::focal_from_camera(): prior code always returned
// camera.zoom (default 1000) when projection_mode was Fov because the zoom
// fallback fired before the projection_mode branch. Now projection_mode=Fov
// takes priority, giving focal = (h/2)/tan(fov/2) correctly.
TEST_CASE("Camera2_5D projection: wider FOV creates smaller focal length") {
    const f32 h = 720.0f;

    const f32 focal35 = focal_length_from_fov(h, 35.0f);
    const f32 focal70 = focal_length_from_fov(h, 70.0f);

    CHECK(focal35 > focal70);
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
    cam35.position = {0, 0, -1000};
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

TEST_CASE("Camera2_5D projection: direct point projection keeps X and Y orientation") {
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, 0.0f};
    cam.zoom = 1000.0f;

    Mat4 view = Mat4(1.0f);

    Vec2 screen{};
    f32 depth{};

    bool ok_center = project_world_point_2_5d(
        cam, view, false, 1000.0f,
        Vec3{0.0f, 0.0f, 1000.0f},
        screen, depth
    );
    CHECK(ok_center);
    CHECK(screen.x == doctest::Approx(0.0f));
    CHECK(screen.y == doctest::Approx(0.0f));

    bool ok_right = project_world_point_2_5d(
        cam, view, false, 1000.0f,
        Vec3{100.0f, 0.0f, 1000.0f},
        screen, depth
    );
    CHECK(ok_right);
    CHECK(screen.x > 0.0f);

    bool ok_up = project_world_point_2_5d(
        cam, view, false, 1000.0f,
        Vec3{0.0f, 100.0f, 1000.0f},
        screen, depth
    );
    CHECK(ok_up);
    // Contract Y sign: inverted — a point above camera centre (world Y > 0)
    // projects to a negative screen Y (above centre in screen coordinates).
    CHECK(screen.y < 0.0f);
}

// Regression lock for ADR-015 Decision 1 — producer-side screen-space TRS
// invariant. Companion to matrix-fix corpus commit c03ce2a2.
// Asserts: after project_layer_2_5d(), out.transform.scale.z == 1.0f,
// out.transform.rotation is identity (Quat(1,0,0,0)), and out.transform.anchor
// is origin (Vec3(0,0,0)) regardless of the input layer_transform's
// scale.z / rotation / anchor values. Locks the three producer-side
// normalize-out writes at include/chronon3d/math/camera_2_5d_projection.hpp:
//   out.transform.scale.z = 1.0f;
//   out.transform.rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f);
//   out.transform.anchor   = Vec3(0.0f, 0.0f, 0.0f);
// A future refactor that drops any one of these writes (because "1.0f is the
// Transform default" or "rotation default is identity") is caught by the
// non-identity-input subcase below, since the producer initially
// `out.transform = layer_transform;` propagates the input verbatim and only
// the explicit writes reset scale.z / rotation / anchor.
TEST_CASE("Camera2_5D projection: project_layer_2_5d normalizes out.transform to screen-space TRS (ADR-015 regression lock)") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    SUBCASE("identity input: out.scale.z=1 + rotation=identity + anchor=origin (baseline control)") {
        Transform tr;
        auto out = project_layer_2_5d(tr, cam, 1280, 720);
        CHECK(out.visible);
        CHECK(out.transform.scale.z == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.w == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.x == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.y == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.z == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.x == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.y == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.z == doctest::Approx(0.0f));
    }

    SUBCASE("non-default input scale.z=7.5: out.scale.z reset to 1.0f (scale.z-write lock)") {
        Transform tr;
        tr.position = {0, 0, -500};  // visible (in front of camera)
        tr.scale = {1.0f, 1.0f, 7.5f};  // deliberately wrong scale.z
        auto out = project_layer_2_5d(tr, cam, 1280, 720);
        CHECK(out.visible);
        CHECK(out.transform.scale.z == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.w == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.x == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.y == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.z == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.x == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.y == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.z == doctest::Approx(0.0f));
    }

    SUBCASE("non-default input rotation (Z-axis pi/2): out.rotation reset to identity (rotation-write lock)") {
        // glm::quat constructor order is (w, x, y, z). Z-axis pi/2 -> (cos(pi/4), 0, 0, sin(pi/4)).
        const f32 half = 0.5f * 1.5707963f;
        const f32 s = std::sin(half);
        const f32 c = std::cos(half);
        Transform tr;
        tr.rotation = Quat(c, 0.0f, 0.0f, s);
        auto out = project_layer_2_5d(tr, cam, 1280, 720);
        CHECK(out.visible);
        CHECK(out.transform.scale.z == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.w == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.x == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.y == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.z == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.x == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.y == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.z == doctest::Approx(0.0f));
    }

    SUBCASE("non-default input anchor (300,-200,50): out.anchor reset to origin (anchor-write lock)") {
        Transform tr;
        tr.position = {100.0f, 0.0f, 0.0f};
        tr.anchor = {300.0f, -200.0f, 50.0f};  // deliberately wrong off-origin anchor
        auto out = project_layer_2_5d(tr, cam, 1280, 720);
        CHECK(out.visible);
        CHECK(out.transform.scale.z == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.w == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.x == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.y == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.z == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.x == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.y == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.z == doctest::Approx(0.0f));
    }

    SUBCASE("all-non-default input (combined worst case): all 3 normalize-out writes verified simultaneously") {
        const f32 half = 0.5f * 1.5707963f;
        const f32 s = std::sin(half);
        const f32 c = std::cos(half);
        Transform tr;
        tr.position  = {100.0f, 50.0f, -500.0f};
        tr.scale     = {2.0f, 3.0f, 4.0f};   // scale.z = 4.0f, must be reset
        tr.rotation  = Quat(c, s, 0.0f, 0.0f);  // arbitrary (w,x,y,z), must be reset to identity
        tr.anchor    = {100.0f, 200.0f, 300.0f};  // off-origin, must be reset
        auto out = project_layer_2_5d(tr, cam, 1280, 720);
        CHECK(out.visible);
        CHECK(out.transform.scale.z == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.w == doctest::Approx(1.0f));
        CHECK(out.transform.rotation.x == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.y == doctest::Approx(0.0f));
        CHECK(out.transform.rotation.z == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.x == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.y == doctest::Approx(0.0f));
        CHECK(out.transform.anchor.z == doctest::Approx(0.0f));
    }
}
