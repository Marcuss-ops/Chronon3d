// ============================================================================
// golden_projection_test.cpp
//
// C7 — Golden projection test suite (DOC 02 / TICKET-035 freeze acceptance).
//
// One TEST_CASE with 6 SUBCASEs, one per canonical projection mode.  Each
// SUBCASE projects a known world point through the contract and asserts the
// analytical screen / depth / perspective_scale values with a tight tolerance
// (1e-3 relative).
//
// Hash strategy: NOT used.  Hashing f32 projection outputs across
// heterogeneous CI hosts is brittle (FMA / fenv drift produces last-ULP
// changes that flip the hash).  Tolerance-only assertions (per the
// pre-merge thinker-with-files-gemini verdict) are the canonical strategy
// for floating-point golden tests.  The analytical values are documented
// here AND in the commit message so any future regression in the projection
// formulas shows up as a CI failure with a captured-diagnostic.
//
// Reference inputs (fixed across the suite):
//   - World point:  (100, 50, 0)     (centre X / Y of the viewport before
//                                     projection; X right, Y up)
//   - Camera pos:   (0, 0, -1000)    (looking down +Z toward origin)
//   - Viewport:     1920 × 1080
//   - Depth:        1000             (point is 1000 units in front of camera)
//
// ============================================================================

#ifndef DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#endif
#include <doctest/doctest.h>

#include <chronon3d/math/camera_projection_contract.hpp>   // FocalPx, project_world_point
#include <chronon3d/scene/camera/camera_v1/evaluated_projection.hpp>  // EvaluatedProjection, make_evaluated_projection
#include <chronon3d/scene/model/camera/lens_model.hpp>      // LensModel, LensPresets, GateFit, ViewportRect
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>     // Camera2_5D, CameraOpticsMode

#include <cmath>

using namespace chronon3d;
using chronon3d::camera_math::project_world_point;
using chronon3d::camera_math::Viewport2D;
using chronon3d::camera_v1::make_evaluated_projection;
using chronon3d::camera_v1::EvaluatedProjection;

namespace {

// Reference frame: identity rotation, camera looks down +Z, point in front.
constexpr Vec3  kWorldPoint{100.0f, 50.0f, 0.0f};
constexpr Vec3  kCameraPos{0.0f, 0.0f, -1000.0f};
constexpr f32   kVpW = 1920.0f;
constexpr f32   kVpH = 1080.0f;
constexpr f32   kDepth = 1000.0f;          // = kCameraPos.z*-1, point in front of camera
constexpr f32   kEps = 1e-3f;              // 0.1% relative tolerance

// Helper: build a minimal Camera2_5D anchored at kCameraPos looking at the
// origin.  All projection modes share the same starting pose; only the
// optics / lens configuration varies.
Camera2_5D make_base_camera() {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = kCameraPos;
    return cam;
}

} // namespace

TEST_CASE("Golden projection: 6 canonical modes project expected analytical values") {

    // ──────────────────────────────────────────────────────────────────
    // 1) Zoom — `cam.zoom = 1000` is treated as focal_x = focal_y = zoom.
    //    For point (100, 50, 0) at depth 1000 with focal=1000:
    //      scale_x = scale_y = 1000 / 1000 = 1.0
    //      centered = (100, -50)
    //      screen  = (1060, 490)
    //      perspective_scale = 1.0
    // ──────────────────────────────────────────────────────────────────
    SUBCASE("Mode 1/6: Zoom") {
        Camera2_5D cam = make_base_camera();
        cam.optics_mode = CameraOpticsMode::Zoom;
        cam.zoom = 1000.0f;

        const auto p = project_world_point(cam, kWorldPoint, Viewport2D{kVpW, kVpH});
        REQUIRE(p.visible);
        CHECK(p.depth == doctest::Approx(kDepth).epsilon(kEps));
        CHECK(p.perspective_scale == doctest::Approx(1.0f).epsilon(kEps));
        CHECK(p.screen.x == doctest::Approx(1060.0f).epsilon(kEps));
        CHECK(p.screen.y == doctest::Approx(490.0f).epsilon(kEps));
    }

    // ──────────────────────────────────────────────────────────────────
    // 2) FOV (50°) — focal = (1080/2)/tan(25°) ≈ 1158.008  (X == Y, spherical)
    //    For point (100, 50, 0) at depth 1000:
    //      scale_x = scale_y = 1158.008 / 1000 ≈ 1.158
    //      centered = (100 * 1.158, -50 * 1.158) = (115.80, -57.90)
    //      screen  = (1075.80, 482.10)
    //      perspective_scale = 1.158
    // ──────────────────────────────────────────────────────────────────
    SUBCASE("Mode 2/6: FOV 50°") {
        Camera2_5D cam = make_base_camera();
        cam.optics_mode = CameraOpticsMode::FieldOfView;
        cam.fov_deg = 50.0f;

        const auto p = project_world_point(cam, kWorldPoint, Viewport2D{kVpW, kVpH});
        REQUIRE(p.visible);
        CHECK(p.depth == doctest::Approx(kDepth).epsilon(kEps));
        CHECK(p.perspective_scale == doctest::Approx(1.158008f).epsilon(kEps));
        CHECK(p.screen.x == doctest::Approx(1075.80f).epsilon(kEps));
        CHECK(p.screen.y == doctest::Approx(482.10f).epsilon(kEps));

        // Snap-check: FOV mode also produces a sensible EvaluatedProjection.
        const auto snap = make_evaluated_projection(cam, Viewport2D{kVpW, kVpH});
        CHECK_FALSE(snap.is_physical_lens);
        CHECK_FALSE(snap.is_anamorphic);
        CHECK(snap.focal_x_px == doctest::Approx(snap.focal_y_px).epsilon(kEps));
    }

    // ──────────────────────────────────────────────────────────────────
    // 3) PhysicalLens (ARRI Alexa 35, 27.03×14.25mm, 50mm, Fill)
    //    Sensor aspect = 27.03/14.25 ≈ 1.897; viewport 16:9 = 1.778.
    //    Fill: effective_viewport = {1920, 1080} (passthrough).
    //      focal_x = 50 * 1920 / 27.03 = 3552.35
    //      focal_y = 50 * 1080 / 14.25 = 3789.47
    //    For point (100, 50, 0) at depth 1000:
    //      scale_x = 3552.35 / 1000 = 3.552
    //      scale_y = 3789.47 / 1000 = 3.789
    //      centered = (355.24, -189.47)
    //      screen  = (1315.24, 350.53)
    //      perspective_scale = 3.789  (focal_y / depth)
    // ──────────────────────────────────────────────────────────────────
    SUBCASE("Mode 3/6: PhysicalLens (ARRI Alexa 35)") {
        Camera2_5D cam = make_base_camera();
        cam.optics_mode = CameraOpticsMode::PhysicalLens;
        cam.lens = LensPresets::arri_35mm();   // 50mm, Fill, sa=1.897

        const FocalPx fxy = camera_math::focal_xy_from_camera(cam, kVpW, kVpH);
        CHECK(fxy.x == doctest::Approx(3552.35f).epsilon(kEps));
        CHECK(fxy.y == doctest::Approx(3789.47f).epsilon(kEps));

        const auto p = project_world_point(cam, kWorldPoint, Viewport2D{kVpW, kVpH});
        REQUIRE(p.visible);
        CHECK(p.depth == doctest::Approx(kDepth).epsilon(kEps));
        CHECK(p.perspective_scale == doctest::Approx(3.789f).epsilon(kEps));
        CHECK(p.screen.x == doctest::Approx(1315.24f).epsilon(kEps));
        CHECK(p.screen.y == doctest::Approx(350.53f).epsilon(kEps));

        // Snap-check the snapshot pipeline.
        const auto snap = make_evaluated_projection(cam, Viewport2D{kVpW, kVpH});
        CHECK(snap.is_physical_lens);
        // ARRI Alexa 35 in Fill mode: sensor aspect 1.897 != viewport aspect 1.778
        // => per-axis focal_x (3551.61) != focal_y (3789.47) => is_anamorphic = true.
        // (The spherical / non-anamorphic assumption only holds when
        //  sensor_aspect == viewport_aspect.)
        CHECK(snap.is_anamorphic);
        CHECK(snap.focal_x_px == doctest::Approx(3551.61f).epsilon(kEps));
        CHECK(snap.focal_y_px == doctest::Approx(3789.47f).epsilon(kEps));
        CHECK(snap.active_viewport.width  == doctest::Approx(kVpW));
        CHECK(snap.active_viewport.height == doctest::Approx(kVpH));
    }

    // ──────────────────────────────────────────────────────────────────
    // 4) GateFit::Stretch (full-frame 50mm, 36×24mm sensor, 50mm)
    //    Stretch uses the REQUESTED viewport directly (not effective):
    //      focal_x = 50 * 1920 / 36 = 2666.67
    //      focal_y = 50 * 1080 / 24 = 2250
    //    For point (100, 50, 0) at depth 1000:
    //      scale_x = 2666.67 / 1000 = 2.667
    //      scale_y = 2250 / 1000 = 2.25
    //      centered = (266.67, -112.50)
    //      screen  = (1226.67, 427.50)
    //      perspective_scale = 2.25
    //    is_anamorphic = true (focal_x != focal_y).
    // ──────────────────────────────────────────────────────────────────
    SUBCASE("Mode 4/6: GateFit::Stretch (full-frame 50mm)") {
        Camera2_5D cam = make_base_camera();
        cam.optics_mode = CameraOpticsMode::PhysicalLens;
        cam.lens = LensPresets::full_frame_50mm();
        cam.lens.gate_fit = GateFit::Stretch;

        const FocalPx fxy = camera_math::focal_xy_from_camera(cam, kVpW, kVpH);
        CHECK(fxy.x == doctest::Approx(2666.67f).epsilon(kEps));
        CHECK(fxy.y == doctest::Approx(2250.0f).epsilon(kEps));
        CHECK(fxy.x != doctest::Approx(fxy.y).epsilon(0.01f));

        const auto p = project_world_point(cam, kWorldPoint, Viewport2D{kVpW, kVpH});
        REQUIRE(p.visible);
        CHECK(p.depth == doctest::Approx(kDepth).epsilon(kEps));
        CHECK(p.perspective_scale == doctest::Approx(2.25f).epsilon(kEps));
        CHECK(p.screen.x == doctest::Approx(1226.67f).epsilon(kEps));
        CHECK(p.screen.y == doctest::Approx(427.50f).epsilon(kEps));

        // Snap-check: Stretch is anamorphic (per-axis independent), active
        // viewport equals requested (Stretch intentionally fills the canvas).
        const auto snap = make_evaluated_projection(cam, Viewport2D{kVpW, kVpH});
        CHECK(snap.is_anamorphic);
        CHECK(snap.focal_x_px == doctest::Approx(2666.67f).epsilon(kEps));
        CHECK(snap.focal_y_px == doctest::Approx(2250.0f).epsilon(kEps));
        CHECK(snap.active_viewport.width  == doctest::Approx(kVpW));
        CHECK(snap.active_viewport.height == doctest::Approx(kVpH));
    }

    // ──────────────────────────────────────────────────────────────────
    // 5) GateFit::Overscan (full-frame 50mm, 36×24mm sensor)
    //    Sensor 3:2 (sa=1.5) narrower than 16:9 (vp_a=1.778) → PILLARBOX.
    //    effective_viewport = {x=150, y=0, w=1620, h=1080}
    //    focal_x = 50 * 1620 / 36 = 2250 (uses eff.width for Uniform branch)
    //    focal_y = 50 * 1080 / 24 = 2250
    //    For point (100, 50, 0) at depth 1000:
    //      scale_x = scale_y = 2250 / 1000 = 2.25
    //      centered = (225.0, -112.5)
    //      screen  = (1185.0, 427.5)
    //      perspective_scale = 2.25
    //    Principal point stays canvas-centred (150 + 1620/2 = 960).
    // ──────────────────────────────────────────────────────────────────
    SUBCASE("Mode 5/6: GateFit::Overscan (pillarbox on full-frame 50mm)") {
        Camera2_5D cam = make_base_camera();
        cam.optics_mode = CameraOpticsMode::PhysicalLens;
        cam.lens = LensPresets::full_frame_50mm();
        cam.lens.gate_fit = GateFit::Overscan;

        // Step 1: the Overscan subrect is correctly produced.
        const ViewportRect eff = cam.lens.effective_viewport(kVpW, kVpH);
        CHECK(eff.x      == doctest::Approx(150.0f).epsilon(kEps));
        CHECK(eff.y      == doctest::Approx(0.0f).epsilon(kEps));
        CHECK(eff.width  == doctest::Approx(1620.0f).epsilon(kEps));
        CHECK(eff.height == doctest::Approx(1080.0f).epsilon(kEps));

        // Step 2: focal is computed against the effective (post-bars) size.
        const FocalPx fxy = camera_math::focal_xy_from_camera(cam, kVpW, kVpH);
        CHECK(fxy.x == doctest::Approx(2250.0f).epsilon(kEps));
        CHECK(fxy.y == doctest::Approx(2250.0f).epsilon(kEps));

        // Step 3: projection lands on the expected screen pixel.
        const auto p = project_world_point(cam, kWorldPoint, Viewport2D{kVpW, kVpH});
        REQUIRE(p.visible);
        CHECK(p.depth == doctest::Approx(kDepth).epsilon(kEps));
        CHECK(p.perspective_scale == doctest::Approx(2.25f).epsilon(kEps));
        CHECK(p.screen.x == doctest::Approx(1185.0f).epsilon(kEps));
        CHECK(p.screen.y == doctest::Approx(427.50f).epsilon(kEps));

        // Step 4: snapshot exposes the pillarbox + canonical principal point.
        const auto snap = make_evaluated_projection(cam, Viewport2D{kVpW, kVpH});
        CHECK_FALSE(snap.is_anamorphic);                     // uniform axes under Overscan
        CHECK(snap.active_viewport.width  == doctest::Approx(1620.0f).epsilon(kEps));
        CHECK(snap.active_viewport.height == doctest::Approx(1080.0f).epsilon(kEps));
        CHECK(snap.principal_point_px.x == doctest::Approx(960.0f).epsilon(kEps));
        CHECK(snap.principal_point_px.y == doctest::Approx(540.0f).epsilon(kEps));
    }

    // ──────────────────────────────────────────────────────────────────
    // 6) Anamorphic squeeze (anamorphic_50mm, 21.95×18.59mm, 50mm, 2× squeeze)
    //    sa = 21.95/18.59 ≈ 1.181; vp_a = 1.778 (viewport wider than sensor).
    //    Fill: effective_viewport = {1920, 1080} (passthrough).
    //      raw_x = 50 * 1920 / 21.95 = 4373.58
    //      raw_y = 50 * 1080 / 18.59 = 2904.79
    //      lens_factor = pixel_aspect(1.0) * anamorphic_squeeze(2.0) = 2.0
    //      focal_x = 4373.58 * 2.0 = 8747.15
    //      focal_y = 2904.79 (no squeeze on Y)
    //    For point (100, 50, 0) at depth 1000:
    //      scale_x = 8747.15 / 1000 = 8.747
    //      scale_y = 2904.79 / 1000 = 2.905
    //      centered = (874.72, -145.24)
    //      screen  = (1834.72, 394.76)
    //      perspective_scale = 2.905  (still focal_y / depth)
    // ──────────────────────────────────────────────────────────────────
    SUBCASE("Mode 6/6: Anamorphic 2× squeeze (anamorphic_50mm)") {
        Camera2_5D cam = make_base_camera();
        cam.optics_mode = CameraOpticsMode::PhysicalLens;
        cam.lens = LensPresets::anamorphic_50mm();   // 50mm, 2× squeeze, Fill

        const FocalPx fxy = camera_math::focal_xy_from_camera(cam, kVpW, kVpH);
        CHECK(fxy.x == doctest::Approx(8747.15f).epsilon(kEps));
        CHECK(fxy.y == doctest::Approx(2904.79f).epsilon(kEps));
        // The anamorphic squeeze multiplies focal_x by EXACTLY 2.0, but the
        // per-axis base ratio (vp_w/sensor_w) / (vp_h/sensor_h) for vp=1920x1080
        // on sensor 21.95x18.59 is (87.47/58.10) ≈ 1.506.  So focal_x/focal_y =
        // 1.506 * 2.0 = 3.011.  Asserting "== 2" would be wrong; the squeeze
        // factor of 2x is verified by the per-axis raw_X / raw_Y comparison below.
        CHECK(fxy.x == doctest::Approx(3.011f * fxy.y).epsilon(0.01f));
        // Squeeze factor is exactly 2.0 on the X base (the lens_factor itself):
        // raw_x / raw_y = 1.506, then focal_x = raw_x * 2.0, focal_y = raw_y.
        // So focal_x / raw_x = 2.0 exactly.
        const f32 raw_x = fxy.x / 2.0f;
        const f32 raw_y = fxy.y;
        CHECK(raw_x == doctest::Approx(4373.58f).epsilon(kEps));
        CHECK(raw_y == doctest::Approx(2904.79f).epsilon(kEps));
        CHECK(raw_x == doctest::Approx(1.506f * raw_y).epsilon(0.01f));

        const auto p = project_world_point(cam, kWorldPoint, Viewport2D{kVpW, kVpH});
        REQUIRE(p.visible);
        CHECK(p.depth == doctest::Approx(kDepth).epsilon(kEps));
        CHECK(p.perspective_scale == doctest::Approx(2.905f).epsilon(kEps));
        CHECK(p.screen.x == doctest::Approx(1834.72f).epsilon(kEps));
        CHECK(p.screen.y == doctest::Approx(394.76f).epsilon(kEps));

        // Snap-check: anamorphic true, squeeze + pixel_aspect flow through.
        const auto snap = make_evaluated_projection(cam, Viewport2D{kVpW, kVpH});
        CHECK(snap.is_physical_lens);
        CHECK(snap.is_anamorphic);
        CHECK(snap.pixel_aspect        == doctest::Approx(1.0f).epsilon(kEps));
        CHECK(snap.anamorphic_squeeze  == doctest::Approx(2.0f).epsilon(kEps));
        // Same ratio as the FocalPx block above: focal_x / focal_y = 3.011
        // (1.506 base ratio * 2.0 squeeze), NOT 2.0.  The anamorphic squeeze
        // applies only to the X base; the per-axis raw ratio is preserved.
        CHECK(snap.focal_x_px == doctest::Approx(3.011f * snap.focal_y_px).epsilon(0.01f));
    }
}
