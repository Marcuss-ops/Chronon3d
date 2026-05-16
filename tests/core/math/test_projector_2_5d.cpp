#include <doctest/doctest.h>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/math/transform.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::renderer;

// ProjectionContext requires look_at convention (negative view_z = visible).
// Camera2_5D with point_of_interest_enabled uses math::look_at → correct convention.
static ProjectionContext make_ctx(float zoom = 1000.0f, int w = 1280, int h = 720) {
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.zoom = zoom;
    cam.point_of_interest = {0.0f, 0.0f, 0.0f};
    cam.point_of_interest_enabled = true;  // forces math::look_at → view_z < 0 for visible
    return make_projection_context(cam, w, h);
}

TEST_CASE("ProjectionContext: point at z=0 is visible with depth 1000") {
    auto ctx = make_ctx();
    auto p = ctx.project_point({0.0f, 0.0f, 0.0f});
    CHECK(p.visible);
    CHECK(p.depth == doctest::Approx(1000.0f).epsilon(1.0f));
}

TEST_CASE("ProjectionContext: point at origin projects near viewport center") {
    auto ctx = make_ctx();
    auto p = ctx.project_point({0.0f, 0.0f, 0.0f});
    CHECK(p.visible);
    CHECK(p.screen.x == doctest::Approx(640.0f).epsilon(2.0f));
    CHECK(p.screen.y == doctest::Approx(360.0f).epsilon(2.0f));
}

TEST_CASE("ProjectionContext: layer at z=-500 is closer (depth 500) than z=0 (depth 1000)") {
    auto ctx = make_ctx(1000.0f);
    auto p_ref = ctx.project_point({0.0f, 0.0f, 0.0f});   // depth ~1000
    auto p_near = ctx.project_point({0.0f, 0.0f, -500.0f}); // depth ~500
    CHECK(p_ref.visible);
    CHECK(p_near.visible);
    CHECK(p_near.depth < p_ref.depth);
    CHECK(p_near.depth == doctest::Approx(500.0f).epsilon(5.0f));
}

TEST_CASE("ProjectionContext: layer at z=500 is farther (depth 1500)") {
    auto ctx = make_ctx(1000.0f);
    auto p_ref = ctx.project_point({0.0f, 0.0f, 0.0f});
    auto p_far = ctx.project_point({0.0f, 0.0f, 500.0f});
    CHECK(p_ref.visible);
    CHECK(p_far.visible);
    CHECK(p_far.depth > p_ref.depth);
    CHECK(p_far.depth == doctest::Approx(1500.0f).epsilon(5.0f));
}

TEST_CASE("ProjectionContext: point behind camera (z < -1000) is not visible") {
    auto ctx = make_ctx();
    auto p = ctx.project_point({0.0f, 0.0f, -1001.0f}); // behind camera
    CHECK_FALSE(p.visible);
}

TEST_CASE("ProjectionContext: project_card — flat card at z=0 is visible and centered") {
    auto ctx = make_ctx();
    Transform tr;
    tr.position = {0.0f, 0.0f, 0.0f};
    auto card = ctx.project_card(tr.to_mat4(), {200.0f, 100.0f});
    CHECK(card.visible);
    CHECK(card.depth == doctest::Approx(1000.0f).epsilon(5.0f));
    // TL (local x-) projects left of TR (local x+)
    CHECK(card.corners[0].x < card.corners[1].x);
    // With GLM lookAt: local y- → screen y+ (below center), local y+ → screen y- (above center)
    CHECK(card.corners[0].y > card.corners[2].y);
}

TEST_CASE("ProjectionContext: project_card — card offset right projects offset right") {
    auto ctx = make_ctx();
    Transform tr_center, tr_right;
    tr_center.position = {0.0f, 0.0f, 0.0f};
    tr_right.position  = {200.0f, 0.0f, 0.0f};
    auto c0 = ctx.project_card(tr_center.to_mat4(), {100.0f, 100.0f});
    auto c1 = ctx.project_card(tr_right.to_mat4(),  {100.0f, 100.0f});
    CHECK(c0.visible);
    CHECK(c1.visible);
    // All corners of right card should be right of the same corners of center card
    for (int i = 0; i < 4; ++i) {
        CHECK(c1.corners[i].x > c0.corners[i].x);
    }
}

TEST_CASE("ProjectionContext: project_card — near card has larger projected size than far card") {
    auto ctx = make_ctx(1000.0f);
    Transform tr_near, tr_far;
    tr_near.position = {0.0f, 0.0f, -500.0f}; // closer to camera
    tr_far.position  = {0.0f, 0.0f,  500.0f}; // farther
    auto near_card = ctx.project_card(tr_near.to_mat4(), {200.0f, 200.0f});
    auto far_card  = ctx.project_card(tr_far.to_mat4(),  {200.0f, 200.0f});
    CHECK(near_card.visible);
    CHECK(far_card.visible);
    // Near card width in screen pixels should be larger than far card width
    const float near_w = near_card.corners[1].x - near_card.corners[0].x;
    const float far_w  = far_card.corners[1].x  - far_card.corners[0].x;
    CHECK(near_w > far_w);
}

TEST_CASE("ProjectionContext: project_card — identity (no rotation) produces rectangle") {
    auto ctx = make_ctx(1000.0f);
    Transform tr;
    tr.position = {0.0f, 0.0f, 0.0f};
    auto card = ctx.project_card(tr.to_mat4(), {200.0f, 100.0f});
    CHECK(card.visible);
    // All four corners should form a proper rectangle (horizontal and vertical edges equal)
    const float top_w    = card.corners[1].x - card.corners[0].x;
    const float bottom_w = card.corners[2].x - card.corners[3].x;
    CHECK(top_w == doctest::Approx(bottom_w).epsilon(0.01f));
    const float left_h   = card.corners[3].y - card.corners[0].y;
    const float right_h  = card.corners[2].y - card.corners[1].y;
    CHECK(left_h == doctest::Approx(right_h).epsilon(0.01f));
}

TEST_CASE("ProjectionContext: project_card — Y rotation produces non-rectangular quad") {
    // A card rotated around Y should project as a trapezoid (not a rectangle)
    auto ctx = make_ctx(1000.0f);
    Transform tr;
    tr.position = {0.0f, 0.0f, 0.0f};
    tr.rotation = math::from_euler({0.0f, 30.0f, 0.0f});  // 30° Y rotation
    auto card = ctx.project_card(tr.to_mat4(), {200.0f, 100.0f});
    CHECK(card.visible);
    // Left edge (corners[0]→[3]) and right edge (corners[1]→[2]) should have different widths
    const float left_x  = (card.corners[0].x + card.corners[3].x) * 0.5f;
    const float right_x = (card.corners[1].x + card.corners[2].x) * 0.5f;
    // After Y rotation one side is closer to camera → projected larger → asymmetric
    CHECK(std::abs(right_x - left_x) > 1.0f);
    // The card is still symmetric vertically (Y rotation doesn't affect top/bottom balance)
    const float top_mid    = (card.corners[0].x + card.corners[1].x) * 0.5f;
    const float bottom_mid = (card.corners[3].x + card.corners[2].x) * 0.5f;
    CHECK(top_mid == doctest::Approx(bottom_mid).epsilon(2.0f));
}

TEST_CASE("ProjectionContext: project_card — card behind camera is not visible") {
    auto ctx = make_ctx();
    Transform tr;
    tr.position = {0.0f, 0.0f, -1500.0f};  // behind camera (camera at z=-1000)
    auto card = ctx.project_card(tr.to_mat4(), {200.0f, 100.0f});
    CHECK_FALSE(card.visible);
}

TEST_CASE("ProjectionContext: project_card — depth is average of projected corner depths") {
    auto ctx = make_ctx(1000.0f);
    Transform tr;
    tr.position = {0.0f, 0.0f, 0.0f};
    // Flat card at z=0 with camera at z=-1000 → all corners at depth ~1000
    auto card = ctx.project_card(tr.to_mat4(), {200.0f, 100.0f});
    CHECK(card.visible);
    CHECK(card.depth == doctest::Approx(1000.0f).epsilon(5.0f));
}

TEST_CASE("ProjectionContext: project_card — Camera2_5D FOV mode gives correct focal") {
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.projection_mode = Camera2_5DProjectionMode::Fov;
    cam.fov_deg = 50.0f;
    cam.point_of_interest = {0.0f, 0.0f, 0.0f};
    cam.point_of_interest_enabled = true;
    auto ctx = make_projection_context(cam, 1280, 720);
    // focal = (720/2) / tan(25°) ≈ 360 / 0.4663 ≈ 772
    CHECK(ctx.focal == doctest::Approx(772.0f).epsilon(5.0f));
    auto p = ctx.project_point({0.0f, 0.0f, 0.0f});
    CHECK(p.visible);
}

TEST_CASE("ProjectionContext: project_line_clipped — segment in front of camera returns true") {
    auto ctx = make_ctx();
    Vec2 p0, p1;
    bool ok = ctx.project_line_clipped({-100.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}, p0, p1);
    CHECK(ok);
    // Both projected points should be within a reasonable screen area (not degenerate)
    CHECK(p0.x > 0.0f);
    CHECK(p1.x > 0.0f);
    // The two endpoints are distinct (different x)
    CHECK(std::abs(p0.x - p1.x) > 1.0f);
}

TEST_CASE("ProjectionContext: project_line_clipped — both points behind camera returns false") {
    auto ctx = make_ctx();
    Vec2 p0, p1;
    // Points at z = -2000 are behind camera (camera at z=-1000, looking toward +z)
    bool ok = ctx.project_line_clipped({0.0f, 0.0f, -2000.0f}, {100.0f, 0.0f, -2000.0f}, p0, p1);
    CHECK_FALSE(ok);
}

TEST_CASE("ProjectionContext: project_line_clipped — straddling near plane clips and returns true") {
    auto ctx = make_ctx();
    Vec2 p0, p1;
    // One endpoint in front (z=0), one behind camera (z=-2000)
    bool ok = ctx.project_line_clipped({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -2000.0f}, p0, p1);
    CHECK(ok);
}
