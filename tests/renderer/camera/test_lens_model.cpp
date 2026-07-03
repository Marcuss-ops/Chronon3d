#include <doctest/doctest.h>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/math/camera_projection_contract.hpp>
using namespace chronon3d;


// =========================================================================
// LensModel — defaults and basic properties
// =========================================================================

TEST_CASE("LensModel: default construction") {
    LensModel lens;
    CHECK(lens.focal_length == doctest::Approx(50.0f));
    CHECK(lens.f_stop       == doctest::Approx(2.8f));
    CHECK(lens.close_focus  == doctest::Approx(300.0f));
    CHECK(lens.sensor_width == doctest::Approx(36.0f));
    CHECK(lens.sensor_height == doctest::Approx(24.0f));
    CHECK(lens.gate_fit     == GateFit::Fill);
}

TEST_CASE("LensModel: sensor_aspect") {
    LensModel lens;
    CHECK(lens.sensor_aspect() == doctest::Approx(1.5f)); // 36/24 = 1.5
}

TEST_CASE("LensModel: horizontal_fov_deg for 50mm on FF") {
    LensModel lens;
    // 50mm on 36mm sensor: HFOV = 2*atan(18/50) = 39.6°
    f32 hfov = lens.horizontal_fov_deg();
    CHECK(hfov == doctest::Approx(39.6f).epsilon(0.1f));
}

TEST_CASE("LensModel: aperture_diameter") {
    LensModel lens;
    lens.f_stop = 2.8f;
    lens.focal_length = 50.0f;
    CHECK(lens.aperture_diameter_mm() == doctest::Approx(17.857f).epsilon(0.01f));

    lens.f_stop = 16.0f;
    CHECK(lens.aperture_diameter_mm() == doctest::Approx(3.125f).epsilon(0.01f));
}

// =========================================================================
// LensModel — GateFit modes
// =========================================================================

TEST_CASE("LensModel: gate_fit Fill — wider sensor than viewport crops") {
    LensModel lens;
    lens.focal_length = 50.0f;
    lens.sensor_width = 36.0f;
    lens.sensor_height = 24.0f;  // 3:2 sensor
    lens.gate_fit = GateFit::Fill;

    // 1920x1080 = 16:9 viewport
    // Sensor 3:2 is wider than 16:9, so Fill crops top/bottom.
    // Effective sensor width = full 36mm, so focal_px = 50 * 1920/36 = 2667
    f32 fp = lens.focal_pixels(1920.0f, 1080.0f);
    CHECK(fp == doctest::Approx(2666.67f).epsilon(1.0f));
}

TEST_CASE("LensModel: gate_fit Fill — viewport wider than sensor") {
    LensModel lens;
    lens.focal_length = 50.0f;
    lens.sensor_width = 24.0f;
    lens.sensor_height = 36.0f;  // portrait sensor, 2:3 aspect
    lens.gate_fit = GateFit::Fill;

    // 1920x1080 = 16:9 viewport (1.78:1)
    // Sensor is 2:3 (0.67:1) — narrower than viewport.
    // Fill mode: sensor width fills viewport width.
    // focal_px = 50 * 1920/24 = 4000
    f32 fp = lens.focal_pixels(1920.0f, 1080.0f);
    CHECK(fp == doctest::Approx(4000.0f).epsilon(1.0f));
}

TEST_CASE("LensModel: gate_fit Overscan — entire sensor visible") {
    LensModel lens;
    lens.focal_length = 50.0f;
    lens.sensor_width = 36.0f;
    lens.sensor_height = 24.0f;  // 3:2 sensor
    lens.gate_fit = GateFit::Overscan;

    // 1920x1080 = 16:9 viewport
    // Sensor 3:2 is wider than 16:9, so Overscan uses full sensor width.
    // focal_px = 50 * 1920/36 = 2667
    f32 fp = lens.focal_pixels(1920.0f, 1080.0f);
    CHECK(fp == doctest::Approx(2666.67f).epsilon(1.0f));
}

TEST_CASE("LensModel: gate_fit Stretch — uses sensor dimensions directly") {
    LensModel lens;
    lens.focal_length = 50.0f;
    lens.sensor_width = 36.0f;
    lens.sensor_height = 24.0f;
    lens.gate_fit = GateFit::Stretch;

    // Always full sensor width regardless of viewport.
    f32 fp = lens.focal_pixels(1920.0f, 1080.0f);
    CHECK(fp == doctest::Approx(2666.67f).epsilon(1.0f));

    // Different viewport aspect doesn't change result.
    f32 fp2 = lens.focal_pixels(1920.0f, 1200.0f);
    CHECK(fp2 == doctest::Approx(2666.67f).epsilon(1.0f));
}

// =========================================================================
// LensPresets
// =========================================================================

TEST_CASE("LensPresets: full_frame_50mm defaults") {
    auto lens = LensPresets::full_frame_50mm();
    CHECK(lens.focal_length   == doctest::Approx(50.0f));
    CHECK(lens.f_stop         == doctest::Approx(1.8f));
    CHECK(lens.sensor_width   == doctest::Approx(36.0f));
    CHECK(lens.sensor_height  == doctest::Approx(24.0f));
    CHECK(lens.close_focus    == doctest::Approx(450.0f));
    CHECK(lens.gate_fit       == GateFit::Fill);
}

TEST_CASE("LensPresets: anamorphic_50mm") {
    auto lens = LensPresets::anamorphic_50mm();
    CHECK(lens.focal_length   == doctest::Approx(50.0f));
    CHECK(lens.sensor_width   == doctest::Approx(21.95f));
    CHECK(lens.sensor_height  == doctest::Approx(18.59f));
}

TEST_CASE("LensPresets: mft_25mm") {
    auto lens = LensPresets::mft_25mm();
    CHECK(lens.focal_length   == doctest::Approx(25.0f));
    CHECK(lens.sensor_width   == doctest::Approx(17.3f));
    CHECK(lens.sensor_height  == doctest::Approx(13.0f));
}

TEST_CASE("LensPresets: imax_50mm") {
    auto lens = LensPresets::imax_50mm();
    CHECK(lens.focal_length   == doctest::Approx(50.0f));
    CHECK(lens.sensor_width   == doctest::Approx(52.63f));
    CHECK(lens.gate_fit       == GateFit::Fill);
}

TEST_CASE("LensPresets: arri_35mm") {
    auto lens = LensPresets::arri_35mm();
    CHECK(lens.focal_length   == doctest::Approx(50.0f));
    CHECK(lens.f_stop         == doctest::Approx(2.8f));
    CHECK(lens.close_focus    == doctest::Approx(450.0f));
    CHECK(lens.sensor_width   == doctest::Approx(27.03f));
    CHECK(lens.sensor_height  == doctest::Approx(14.25f));
    CHECK(lens.gate_fit       == GateFit::Fill);
}

TEST_CASE("LensPresets: different focal lengths via presets") {
    CHECK(LensPresets::full_frame_24mm().focal_length  == doctest::Approx(24.0f));
    CHECK(LensPresets::full_frame_35mm().focal_length  == doctest::Approx(35.0f));
    CHECK(LensPresets::full_frame_85mm().focal_length  == doctest::Approx(85.0f));
    CHECK(LensPresets::full_frame_135mm().focal_length == doctest::Approx(135.0f));
}

// =========================================================================
// Integration with Camera2_5D
// =========================================================================

TEST_CASE("Camera2_5D: lens field exists") {
    Camera2_5D cam;
    CHECK(cam.lens.focal_length == doctest::Approx(50.0f));
    CHECK(cam.lens.sensor_width == doctest::Approx(36.0f));
    CHECK(cam.lens.sensor_height == doctest::Approx(24.0f));
    CHECK(cam.lens.f_stop == doctest::Approx(2.8f));
}

TEST_CASE("Camera2_5D: lens can be set to a preset") {
    Camera2_5D cam;
    cam.lens = LensPresets::full_frame_85mm();
    CHECK(cam.lens.focal_length == doctest::Approx(85.0f));
    CHECK(cam.lens.sensor_width == doctest::Approx(36.0f));
    CHECK(cam.lens.gate_fit == GateFit::Fill);

    // Lens values are independent of DoF.
    cam.dof.use_physical_model = true;
    CHECK(cam.dof.use_physical_model);
}

TEST_CASE("Camera2_5D: lens set to anamorphic preset") {
    Camera2_5D cam;
    cam.lens = LensPresets::anamorphic_50mm();
    CHECK(cam.lens.sensor_width == doctest::Approx(21.95f));
    f32 sensor_aspect = cam.lens.sensor_aspect();
    CHECK(sensor_aspect == doctest::Approx(1.181f).epsilon(0.001f));
}

// =========================================================================
// Integration with camera_projection_contract
// =========================================================================

TEST_CASE("focal_from_camera: lens mode with gate-fit Fill") {
    Camera2_5D cam;
    cam.lens = LensPresets::full_frame_50mm();
    cam.dof.use_physical_model = true;
    cam.optics_mode = CameraOpticsMode::Zoom;

    // FF 50mm on 1920x1080 with Fill gate fit:
    // focal_px = 50 * 1920 / 36 = 2666.67
    f32 focal = camera_math::focal_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(focal == doctest::Approx(2666.67f).epsilon(1.0f));
}

TEST_CASE("focal_from_camera: lens mode with anamorphic preset") {
    Camera2_5D cam;
    cam.lens = LensPresets::anamorphic_50mm();
    cam.dof.use_physical_model = true;
    cam.optics_mode = CameraOpticsMode::Zoom;

    // Anamorphic 50mm on 1920x1080 with Fill gate fit:
    // focal_px = 50 * 1920 / 21.95 = 4373.6
    f32 focal = camera_math::focal_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(focal == doctest::Approx(4373.6f).epsilon(1.0f));
}

TEST_CASE("focal_from_camera: legacy Fov mode unchanged") {
    Camera2_5D cam;
    cam.optics_mode = CameraOpticsMode::FieldOfView;
    cam.fov_deg = 50.0f;
    // Fov mode ignores lens model when use_physical_model is false.
    f32 focal = camera_math::focal_from_camera(cam, 1080.0f);
    // focal = (1080/2) / tan(25°) = 540 / 0.4663 = 1158
    CHECK(focal == doctest::Approx(1158.0f).epsilon(1.0f));
}

TEST_CASE("focal_from_camera: legacy Zoom mode unchanged") {
    Camera2_5D cam;
    cam.optics_mode = CameraOpticsMode::Zoom;
    cam.zoom = 1000.0f;
    f32 focal = camera_math::focal_from_camera(cam, 1080.0f);
    CHECK(focal == doctest::Approx(1000.0f));
}

// =========================================================================
// AnimatedCamera2_5D integration
// =========================================================================

TEST_CASE("AnimatedCamera2_5D: propagates LensModel") {
    AnimatedCamera2_5D acam;
    acam.dof_enabled = true;
    acam.use_physical_model = true;
    acam.focal_length.set(85.0f);
    acam.sensor_width.set(36.0f);
    acam.sensor_height.set(24.0f);
    acam.f_stop.set(1.4f);
    acam.close_focus.set(850.0f);

    Camera2_5D cam = acam.evaluate(Frame{0});
    CHECK(cam.lens.focal_length == doctest::Approx(85.0f));
    CHECK(cam.lens.sensor_width == doctest::Approx(36.0f));
    CHECK(cam.lens.sensor_height == doctest::Approx(24.0f));
    CHECK(cam.lens.f_stop == doctest::Approx(1.4f));
    CHECK(cam.lens.close_focus == doctest::Approx(850.0f));
    CHECK(cam.lens.gate_fit == GateFit::Fill);
}

TEST_CASE("AnimatedCamera2_5D: animation of lens works via evaluate") {
    AnimatedCamera2_5D acam;
    acam.dof_enabled = true;
    acam.use_physical_model = true;
    acam.focal_length
        .key(Frame{0}, 50.0f)
        .key(Frame{60}, 85.0f);

    Camera2_5D cam0 = acam.evaluate(Frame{0});
    CHECK(cam0.lens.focal_length == doctest::Approx(50.0f));

    Camera2_5D cam60 = acam.evaluate(Frame{60});
    CHECK(cam60.lens.focal_length == doctest::Approx(85.0f));
}

// =========================================================================
// AnimatedCamera2_5D — sensor_height, close_focus, gate_fit propagation
// =========================================================================

TEST_CASE("AnimatedCamera2_5D: sensor_height keyframe interpolates") {
    AnimatedCamera2_5D acam;
    acam.sensor_height
        .key(Frame{0}, 24.0f)
        .key(Frame{60}, 18.0f);

    Camera2_5D cam0  = acam.evaluate(Frame{0});
    Camera2_5D cam30 = acam.evaluate(Frame{30});
    Camera2_5D cam60 = acam.evaluate(Frame{60});

    CHECK(cam0.lens.sensor_height  == doctest::Approx(24.0f));
    CHECK(cam30.lens.sensor_height == doctest::Approx(21.0f));  // linear midpoint
    CHECK(cam60.lens.sensor_height == doctest::Approx(18.0f));
}

TEST_CASE("AnimatedCamera2_5D: close_focus keyframe interpolates") {
    AnimatedCamera2_5D acam;
    acam.close_focus
        .key(Frame{0}, 450.0f)
        .key(Frame{60}, 850.0f);

    Camera2_5D cam0  = acam.evaluate(Frame{0});
    Camera2_5D cam30 = acam.evaluate(Frame{30});
    Camera2_5D cam60 = acam.evaluate(Frame{60});

    CHECK(cam0.lens.close_focus  == doctest::Approx(450.0f));
    CHECK(cam30.lens.close_focus == doctest::Approx(650.0f));  // linear midpoint
    CHECK(cam60.lens.close_focus == doctest::Approx(850.0f));
}

TEST_CASE("AnimatedCamera2_5D: gate_fit propagates through evaluate") {
    AnimatedCamera2_5D acam;

    // Default
    Camera2_5D cam_default = acam.evaluate(Frame{0});
    CHECK(cam_default.lens.gate_fit == GateFit::Fill);

    // Overscan
    acam.gate_fit = GateFit::Overscan;
    Camera2_5D cam_over = acam.evaluate(Frame{0});
    CHECK(cam_over.lens.gate_fit == GateFit::Overscan);

    // Stretch
    acam.gate_fit = GateFit::Stretch;
    Camera2_5D cam_stretch = acam.evaluate(Frame{0});
    CHECK(cam_stretch.lens.gate_fit == GateFit::Stretch);

    // Fill (back to default)
    acam.gate_fit = GateFit::Fill;
    Camera2_5D cam_fill = acam.evaluate(Frame{0});
    CHECK(cam_fill.lens.gate_fit == GateFit::Fill);
}

TEST_CASE("AnimatedCamera2_5D: sensor_height is_animated detection") {
    AnimatedCamera2_5D acam;
    CHECK_FALSE(acam.is_animated());

    acam.sensor_height.key(Frame{0}, 24.0f).key(Frame{60}, 18.0f);
    CHECK(acam.is_animated());
    CHECK(acam.is_time_dependent());
}

TEST_CASE("AnimatedCamera2_5D: close_focus is_animated detection") {
    AnimatedCamera2_5D acam;
    CHECK_FALSE(acam.is_animated());

    acam.close_focus.key(Frame{0}, 450.0f).key(Frame{60}, 850.0f);
    CHECK(acam.is_animated());
    CHECK(acam.is_time_dependent());
}

// =========================================================================
// CameraRig — lens field propagation
// =========================================================================

TEST_CASE("CameraRig: lens model fields propagate through evaluate") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    // Override lens fields via CameraRigDOF
    rig.dof.sensor_height.set(18.0f);       // APS-C height
    rig.dof.close_focus.set(600.0f);
    rig.dof.gate_fit = GateFit::Overscan;

    Camera2_5D cam = rig.evaluate(Frame{0});

    CHECK(cam.lens.sensor_height == doctest::Approx(18.0f));
    CHECK(cam.lens.close_focus   == doctest::Approx(600.0f));
    CHECK(cam.lens.gate_fit      == GateFit::Overscan);

    // Also verify other lens fields still have defaults
    CHECK(cam.lens.focal_length  == doctest::Approx(50.0f));
    CHECK(cam.lens.sensor_width  == doctest::Approx(36.0f));
    CHECK(cam.lens.f_stop        == doctest::Approx(2.8f));
}

TEST_CASE("CameraRig: lens sensor_height keyframe interpolates") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    rig.dof.sensor_height
        .key(Frame{0}, 24.0f)
        .key(Frame{60}, 18.0f);

    Camera2_5D cam0  = rig.evaluate(Frame{0});
    Camera2_5D cam30 = rig.evaluate(Frame{30});
    Camera2_5D cam60 = rig.evaluate(Frame{60});

    CHECK(cam0.lens.sensor_height  == doctest::Approx(24.0f));
    CHECK(cam30.lens.sensor_height == doctest::Approx(21.0f));
    CHECK(cam60.lens.sensor_height == doctest::Approx(18.0f));
}

TEST_CASE("CameraRig: lens close_focus keyframe interpolates") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    rig.dof.close_focus
        .key(Frame{0}, 450.0f)
        .key(Frame{60}, 850.0f);

    Camera2_5D cam0  = rig.evaluate(Frame{0});
    Camera2_5D cam60 = rig.evaluate(Frame{60});

    CHECK(cam0.lens.close_focus  == doctest::Approx(450.0f));
    CHECK(cam60.lens.close_focus == doctest::Approx(850.0f));
}

TEST_CASE("CameraRig: lens gate_fit propagates through evaluate") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    // Overscan
    rig.dof.gate_fit = GateFit::Overscan;
    Camera2_5D cam = rig.evaluate(Frame{0});
    CHECK(cam.lens.gate_fit == GateFit::Overscan);

    // Stretch
    rig.dof.gate_fit = GateFit::Stretch;
    cam = rig.evaluate(Frame{0});
    CHECK(cam.lens.gate_fit == GateFit::Stretch);

    // Back to Fill
    rig.dof.gate_fit = GateFit::Fill;
    cam = rig.evaluate(Frame{0});
    CHECK(cam.lens.gate_fit == GateFit::Fill);
}

// =========================================================================
// CameraRig — ARRI Alexa 35 lens preset propagation
// =========================================================================

namespace {

// Helper: configure a CameraRigDOF from LensPresets::arri_35mm() values.
// The ARRI Alexa 35 has a Super 35 sensor (27.03 × 14.25 mm) with a 50mm cinema prime.
void set_arri_alexa_35_dof(CameraRigDOF& dof) {
    dof.focal_length.set(50.0f);
    dof.f_stop.set(2.8f);
    dof.sensor_width.set(27.03f);
    dof.sensor_height.set(14.25f);
    dof.close_focus.set(450.0f);
    dof.gate_fit = GateFit::Fill;
    dof.use_physical_model = true;
}

} // namespace

TEST_CASE("CameraRig: ARRI Alexa 35 lens preset propagates via evaluate") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    // Apply ARRI Alexa 35 lens configuration
    set_arri_alexa_35_dof(rig.dof);

    Camera2_5D cam = rig.evaluate(Frame{0});

    // ── Verify all lens fields match ARRI Alexa 35 specs ───────────────
    CHECK(cam.lens.focal_length   == doctest::Approx(50.0f));
    CHECK(cam.lens.f_stop         == doctest::Approx(2.8f));
    CHECK(cam.lens.close_focus    == doctest::Approx(450.0f));
    CHECK(cam.lens.sensor_width   == doctest::Approx(27.03f));
    CHECK(cam.lens.sensor_height  == doctest::Approx(14.25f));
    CHECK(cam.lens.gate_fit       == GateFit::Fill);

    // ── Computed properties from the preset should be correct ──────────
    // S35 sensor: 27.03 / 14.25 ≈ 1.897
    CHECK(cam.lens.sensor_aspect() == doctest::Approx(1.897f).epsilon(0.001f));

    // 50mm on 27.03mm sensor: HFOV = 2*atan(13.515/50) ≈ 30.26°
    CHECK(cam.lens.horizontal_fov_deg() == doctest::Approx(30.26f).epsilon(0.1f));

    // Aperture diameter: 50 / 2.8 ≈ 17.86mm
    CHECK(cam.lens.aperture_diameter_mm() == doctest::Approx(17.857f).epsilon(0.01f));

    // ── Physical model flag is propagated ──────────────────────────────
    CHECK(cam.dof.use_physical_model == true);

    // ── Other CameraRig defaults should be preserved ───────────────────
    CHECK(cam.zoom == doctest::Approx(1000.0f));
    CHECK(cam.fov_deg == doctest::Approx(50.0f));
}

TEST_CASE("CameraRig: ARRI Alexa 35 focal_pixels at 1920x1080") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.orbit_radius.set(1000.0f);

    set_arri_alexa_35_dof(rig.dof);

    Camera2_5D cam = rig.evaluate(Frame{0});

    // ARRI Alexa 35 S35 sensor (27.03×14.25mm, 1.897:1) on 1920×1080 (16:9, 1.778:1)
    // Sensor is wider (1.897 > 1.778) → Fill gate-fit crops top/bottom.
    // Effective sensor fraction = 1.778 / 1.897 = 0.937
    // focal_px = 50 * 1920 / (27.03 * 0.937) = 50 * 1920 / 25.33 ≈ 3789
    const f32 fp = cam.lens.focal_pixels(1920.0f, 1080.0f);
    CHECK(fp == doctest::Approx(3789.0f).epsilon(10.0f));
}

// =========================================================================
// GateFit — focal_pixels edge cases
// =========================================================================

TEST_CASE("LensModel: focal_pixels with zero dimensions returns fallback") {
    LensModel lens;
    f32 fp = lens.focal_pixels(0.0f, 1080.0f);
    CHECK(fp == doctest::Approx(1000.0f));
}

TEST_CASE("LensModel: focal_pixels with zero focal length returns fallback") {
    LensModel lens;
    lens.focal_length = 0.0f;
    f32 fp = lens.focal_pixels(1920.0f, 1080.0f);
    CHECK(fp == doctest::Approx(1000.0f));
}

TEST_CASE("LensModel: different gate fits give different focal lengths") {
    LensModel lens;
    lens.focal_length = 50.0f;
    lens.sensor_width = 36.0f;
    lens.sensor_height = 24.0f;

    // Create a very wide viewport (21:9 = 2560x1080)
    lens.gate_fit = GateFit::Fill;
    f32 fp_fill = lens.focal_pixels(2560.0f, 1080.0f);

    lens.gate_fit = GateFit::Overscan;
    f32 fp_overscan = lens.focal_pixels(2560.0f, 1080.0f);

    // For 21:9 viewport with 3:2 sensor:
    // Fill: sensor wider (1.5) < viewport (2.37) → full sensor width
    // Overscan: also full sensor width since sensor is narrower
    // For this case both should be the same
    CHECK(fp_fill == doctest::Approx(fp_overscan).epsilon(0.01f));
}
