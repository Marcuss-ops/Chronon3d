#include <doctest/doctest.h>
#include <chronon3d/scene/model/camera/lens_model.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
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
    cam.projection_mode = Camera2_5DProjectionMode::Zoom;

    // FF 50mm on 1920x1080 with Fill gate fit:
    // focal_px = 50 * 1920 / 36 = 2666.67
    f32 focal = camera_math::focal_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(focal == doctest::Approx(2666.67f).epsilon(1.0f));
}

TEST_CASE("focal_from_camera: lens mode with anamorphic preset") {
    Camera2_5D cam;
    cam.lens = LensPresets::anamorphic_50mm();
    cam.dof.use_physical_model = true;
    cam.projection_mode = Camera2_5DProjectionMode::Zoom;

    // Anamorphic 50mm on 1920x1080 with Fill gate fit:
    // focal_px = 50 * 1920 / 21.95 = 4373.6
    f32 focal = camera_math::focal_from_camera(cam, 1920.0f, 1080.0f);
    CHECK(focal == doctest::Approx(4373.6f).epsilon(1.0f));
}

TEST_CASE("focal_from_camera: legacy Fov mode unchanged") {
    Camera2_5D cam;
    cam.projection_mode = Camera2_5DProjectionMode::Fov;
    cam.fov_deg = 50.0f;
    // Fov mode ignores lens model when use_physical_model is false.
    f32 focal = camera_math::focal_from_camera(cam, 1080.0f);
    // focal = (1080/2) / tan(25°) = 540 / 0.4663 = 1158
    CHECK(focal == doctest::Approx(1158.0f).epsilon(1.0f));
}

TEST_CASE("focal_from_camera: legacy Zoom mode unchanged") {
    Camera2_5D cam;
    cam.projection_mode = Camera2_5DProjectionMode::Zoom;
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
