// ==============================================================================
// tests/backends/software/utils/test_projection_utils.cpp
//
// Internal tests for renderer::project_2_5d (chronon3d/backend/projection_utils).
//
// Moved out of tests/scene/camera/test_camera_hierarchy.cpp which previously
// mixed PUBLIC camera-hierarchy checks with PRIVATE projection-utility probing.
// The scene-hierarchy coupling is now decoupled; the projection utility is
// verified here in isolation against the backend module's own internals.
// ==============================================================================
#include <doctest/doctest.h>

#include <cmath>

#include <chronon3d/math/glm_types.hpp>
#include "src/backends/software/utils/projection_utils.hpp"

using namespace chronon3d;
using chronon3d::renderer::project_2_5d;

namespace {

// Identity view-matrix helper (no rotation, no translation).
Mat4 identity_view() {
    return Mat4(1.0f);
}

// Translate the world point by an offset then look at it from the negative-Z.
Mat4 look_at_view(const Vec3& target) {
    Mat4 v = Mat4(1.0f);
    v = glm::translate(v, Vec3{0.0f, 0.0f, 1000.0f - target.z});
    return v;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// project_2_5d — minimal API surface
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("project_2_5d: identity view, point centered in viewport") {
    const Mat4 view = identity_view();
    const f32 focal = 100.0f;
    const f32 vp_cx = 50.0f;
    const f32 vp_cy = 50.0f;
    bool ok = false;

    // World point directly in front of the camera (Z = -10) at the optical axis.
    const Vec2 p = project_2_5d({0.0f, 0.0f, -10.0f}, view, focal, vp_cx, vp_cy, ok);

    CHECK(ok);
    CHECK(p.x == doctest::Approx(vp_cx).epsilon(1e-3f));
    CHECK(p.y == doctest::Approx(vp_cy).epsilon(1e-3f));
}

TEST_CASE("project_2_5d: world X offset produces viewport X offset / focal") {
    const Mat4 view = identity_view();
    const f32 focal = 100.0f;
    const f32 vp_cx = 1920.0f * 0.5f;
    const f32 vp_cy = 1080.0f * 0.5f;
    bool ok = false;

    // World point offset +200 in X at depth -1000 → screen offset = 200 / (1000/focal) = 20 px.
    const Vec2 p = project_2_5d({200.0f, 0.0f, -1000.0f}, view, focal, vp_cx, vp_cy, ok);

    CHECK(ok);
    CHECK(p.x == doctest::Approx(vp_cx + 20.0f).epsilon(1e-3f));
    CHECK(p.y == doctest::Approx(vp_cy).epsilon(1e-3f));
}

TEST_CASE("project_2_5d: world Y offset produces viewport Y offset / focal") {
    const Mat4 view = identity_view();
    const f32 focal = 200.0f;
    const f32 vp_cx = 100.0f;
    const f32 vp_cy = 100.0f;
    bool ok = false;

    // World point offset -100 in Y at depth -1000 → screen offset = -100 * (200/1000) = -20 px.
    const Vec2 p = project_2_5d({0.0f, -100.0f, -1000.0f}, view, focal, vp_cx, vp_cy, ok);

    CHECK(ok);
    CHECK(p.x == doctest::Approx(vp_cx).epsilon(1e-3f));
    CHECK(p.y == doctest::Approx(vp_cy - 20.0f).epsilon(1e-3f));
}

TEST_CASE("project_2_5d: point behind camera returns ok=false") {
    const Mat4 view = identity_view();
    const f32 focal = 100.0f;
    bool ok = true;

    // World point at Z = +10 — behind the camera (camera looks toward -Z).
    const Vec2 p = project_2_5d({0.0f, 0.0f, 10.0f}, view, focal,
                                  50.0f, 50.0f, ok);

    CHECK_FALSE(ok);
    // Returned value is implementation-defined when behind the camera;
    // we just check no NaN leaks out.
    CHECK_FALSE(std::isnan(p.x));
    CHECK_FALSE(std::isnan(p.y));
}

TEST_CASE("project_2_5d: different focal scales the screen offset linearly") {
    const Mat4 view = identity_view();
    const f32 vp_cx = 100.0f, vp_cy = 100.0f;
    bool ok_a = false, ok_b = false;
    const Vec3 wp{200.0f, 0.0f, -1000.0f};

    const Vec2 a = project_2_5d(wp, view, /*focal=*/100.0f, vp_cx, vp_cy, ok_a);
    const Vec2 b = project_2_5d(wp, view, /*focal=*/200.0f, vp_cx, vp_cy, ok_b);

    CHECK(ok_a);
    CHECK(ok_b);
    // Doubling focal should double the screen offset.
    CHECK((b.x - vp_cx) == doctest::Approx(2.0f * (a.x - vp_cx)).epsilon(1e-3f));
}

// ─────────────────────────────────────────────────────────────────────────────
// project_2_5d — integration with a view matrix produced by look_at_view
// (mimics resolve_camera_hierarchy → project_point flow).
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("project_2_5d: target resolved by look_at_view projects to viewport centre") {
    const Vec3 target_world{0.0f, 0.0f, 0.0f};
    const Mat4 view = look_at_view(target_world);
    const f32 focal = 1080.0f * 0.5f / std::tan(glm::radians(50.0f) * 0.5f);
    const f32 vp_cx = 1280.0f * 0.5f;
    const f32 vp_cy = 720.0f * 0.5f;
    bool ok = false;

    const Vec2 p = project_2_5d(target_world, view, focal, vp_cx, vp_cy, ok);

    CHECK(ok);
    CHECK(p.x == doctest::Approx(vp_cx).epsilon(0.5f));
    CHECK(p.y == doctest::Approx(vp_cy).epsilon(0.5f));
}
