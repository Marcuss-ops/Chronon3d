#include <doctest/doctest.h>
#include <chronon3d/animation/wiggle.hpp>
#include <chronon3d/scene/model/camera/camera_shake.hpp>
#include <cmath>

using namespace chronon3d;

// ── hash_noise tests ─────────────────────────────────────────────────────────

TEST_CASE("hash_noise: deterministic for same input") {
    f32 a = detail::hash_noise(42, 0);
    f32 b = detail::hash_noise(42, 0);
    CHECK(a == doctest::Approx(b));
}

TEST_CASE("hash_noise: different for different input") {
    f32 a = detail::hash_noise(1, 0);
    f32 b = detail::hash_noise(2, 0);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("hash_noise: different seeds produce different values") {
    f32 a = detail::hash_noise(1, 0);
    f32 b = detail::hash_noise(1, 100);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("hash_noise: output range [-1, 1]") {
    for (int i = 0; i < 100; ++i) {
        f32 v = detail::hash_noise(i, 7);
        CHECK(v >= -1.0f);
        CHECK(v <= 1.0f);
    }
}

// ── smooth_noise tests ───────────────────────────────────────────────────────

TEST_CASE("smooth_noise: deterministic") {
    f32 a = detail::smooth_noise(1.5f, 0);
    f32 b = detail::smooth_noise(1.5f, 0);
    CHECK(a == doctest::Approx(b));
}

TEST_CASE("smooth_noise: continuous (no discontinuities at integers)") {
    f32 left = detail::smooth_noise(4.999f, 0);
    f32 right = detail::smooth_noise(5.001f, 0);
    CHECK(std::abs(left - right) < 0.5f);  // smoothstep ensures continuity
}

// ── wiggle() scalar tests ────────────────────────────────────────────────────

TEST_CASE("wiggle: zero amplitude returns zero") {
    CHECK(wiggle(3.0f, 0.0f, 10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("wiggle: zero frequency returns zero") {
    CHECK(wiggle(0.0f, 15.0f, 10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("wiggle: output bounded by amplitude") {
    for (int i = 0; i < 200; ++i) {
        f32 v = wiggle(3.0f, 15.0f, static_cast<f32>(i) * 0.1f);
        CHECK(v >= -15.0f);
        CHECK(v <= 15.0f);
    }
}

TEST_CASE("wiggle: deterministic for same input") {
    f32 a = wiggle(3.0f, 15.0f, 42.0f, 0);
    f32 b = wiggle(3.0f, 15.0f, 42.0f, 0);
    CHECK(a == doctest::Approx(b));
}

TEST_CASE("wiggle: different seeds produce different values") {
    f32 a = wiggle(3.0f, 15.0f, 42.0f, 0);
    f32 b = wiggle(3.0f, 15.0f, 42.0f, 100);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("wiggle: intensity scales with amplitude") {
    f32 small = wiggle(3.0f, 5.0f, 20.0f);
    f32 large = wiggle(3.0f, 50.0f, 20.0f);
    CHECK(std::abs(large) > std::abs(small));
}

TEST_CASE("wiggle: Frame overload works") {
    f32 a = wiggle(3.0f, 15.0f, Frame{42}, 0);
    f32 b = wiggle(3.0f, 15.0f, 42.0f, 0);
    CHECK(a == doctest::Approx(b));
}

// ── wiggle3D() tests ─────────────────────────────────────────────────────────

TEST_CASE("wiggle3D: zero amplitude returns zero vector") {
    Vec3 v = wiggle3D(3.0f, 0.0f, 10.0f);
    CHECK(v.x == doctest::Approx(0.0f));
    CHECK(v.y == doctest::Approx(0.0f));
    CHECK(v.z == doctest::Approx(0.0f));
}

TEST_CASE("wiggle3D: components are different (decorrelated)") {
    Vec3 v = wiggle3D(3.0f, 15.0f, 42.0f);
    // Different seeds per axis should produce different values
    CHECK(v.x != doctest::Approx(v.y));
    CHECK(v.y != doctest::Approx(v.z));
}

TEST_CASE("wiggle3D: bounded by amplitude") {
    for (int i = 0; i < 100; ++i) {
        Vec3 v = wiggle3D(3.0f, 15.0f, static_cast<f32>(i) * 0.5f);
        CHECK(v.x >= -15.0f); CHECK(v.x <= 15.0f);
        CHECK(v.y >= -15.0f); CHECK(v.y <= 15.0f);
        CHECK(v.z >= -15.0f); CHECK(v.z <= 15.0f);
    }
}

TEST_CASE("wiggle3D: per-axis amplitude") {
    Vec3 amp{10.0f, 5.0f, 20.0f};
    Vec3 v = wiggle3D(3.0f, amp, 42.0f);
    CHECK(v.x >= -10.0f); CHECK(v.x <= 10.0f);
    CHECK(v.y >= -5.0f);  CHECK(v.y <= 5.0f);
    CHECK(v.z >= -20.0f); CHECK(v.z <= 20.0f);
}

// ── CameraShakeConfig tests ──────────────────────────────────────────────────

TEST_CASE("CameraShakeConfig: default config has no effect when amplitude is zero") {
    CameraShakeConfig cfg;
    cfg.position_amp = 0.0f;
    cfg.rotation_amp = 0.0f;
    cfg.zoom_amp = 0.0f;

    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.rotation = {0.0f, 0.0f, 0.0f};
    cam.zoom = 1000.0f;

    cfg.apply_to(cam, Frame{50});
    CHECK(cam.position.x == doctest::Approx(0.0f));
    CHECK(cam.position.y == doctest::Approx(0.0f));
    CHECK(cam.rotation.x == doctest::Approx(0.0f));
}

TEST_CASE("CameraShakeConfig: apply_to modifies position") {
    auto cfg = ShakePreset::handheld(1.0f);
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, -1000.0f};

    cfg.apply_to(cam, Frame{10});
    CHECK(cam.position.x != doctest::Approx(0.0f));
}

TEST_CASE("CameraShakeConfig: apply_to modifies rotation") {
    auto cfg = ShakePreset::handheld(1.0f);
    Camera2_5D cam;
    cam.rotation = {0.0f, 0.0f, 0.0f};

    cfg.apply_to(cam, Frame{10});
    CHECK(cam.rotation.x != doctest::Approx(0.0f));
}

TEST_CASE("CameraShakeConfig: fade_in attenuates at start") {
    CameraShakeConfig cfg;
    cfg.position_freq = 3.0f;
    cfg.position_amp = 15.0f;
    cfg.fade_in_frames = 10.0f;

    Camera2_5D cam1;
    cam1.position = {0.0f, 0.0f, -1000.0f};
    cfg.apply_to(cam1, Frame{1});

    Camera2_5D cam2;
    cam2.position = {0.0f, 0.0f, -1000.0f};
    cfg.apply_to(cam2, Frame{100});

    // Early frame should have smaller displacement than late frame
    f32 early_displacement = std::abs(cam1.position.x);
    f32 late_displacement = std::abs(cam2.position.x);
    CHECK(late_displacement > early_displacement);
}

TEST_CASE("CameraShakeConfig: fade_out attenuates at end") {
    CameraShakeConfig cfg;
    cfg.position_freq = 3.0f;
    cfg.position_amp = 15.0f;
    cfg.fade_out_frames = 10.0f;

    Camera2_5D cam_mid;
    cam_mid.position = {0.0f, 0.0f, -1000.0f};
    cfg.apply_to(cam_mid, Frame{100}, Frame{200});

    Camera2_5D cam_end;
    cam_end.position = {0.0f, 0.0f, -1000.0f};
    cfg.apply_to(cam_end, Frame{198}, Frame{200});

    // Near-end frame should have smaller displacement
    f32 mid_displacement = std::abs(cam_mid.position.x);
    f32 end_displacement = std::abs(cam_end.position.x);
    CHECK(mid_displacement > end_displacement);
}

// ── ShakePreset tests ────────────────────────────────────────────────────────

TEST_CASE("ShakePreset: handheld has reasonable params") {
    auto cfg = ShakePreset::handheld();
    CHECK(cfg.position_amp > 0.0f);
    CHECK(cfg.position_amp < 10.0f);
    CHECK(cfg.rotation_amp > 0.0f);
    CHECK(cfg.rotation_amp < 2.0f);
}

TEST_CASE("ShakePreset: earthquake is more violent than handheld") {
    auto h = ShakePreset::handheld();
    auto e = ShakePreset::earthquake();
    CHECK(e.position_amp > h.position_amp);
    CHECK(e.rotation_amp > h.rotation_amp);
    CHECK(e.position_freq > h.position_freq);
}

TEST_CASE("ShakePreset: drift is gentler than handheld") {
    auto h = ShakePreset::handheld();
    auto d = ShakePreset::drift();
    CHECK(d.position_freq < h.position_freq);
}

TEST_CASE("ShakePreset: explosion has fade_out") {
    auto cfg = ShakePreset::explosion();
    CHECK(cfg.fade_out_frames > 0.0f);
}

TEST_CASE("ShakePreset: intensity scaling") {
    auto a = ShakePreset::handheld(0.5f);
    auto b = ShakePreset::handheld(2.0f);
    CHECK(b.position_amp > a.position_amp);
    CHECK(b.rotation_amp > a.rotation_amp);
}

TEST_CASE("ShakePreset: all presets produce valid configs") {
    auto presets = {
        ShakePreset::handheld(),
        ShakePreset::earthquake(),
        ShakePreset::drift(),
        ShakePreset::walking(),
        ShakePreset::breathing(),
        ShakePreset::explosion(),
        ShakePreset::cinematic(),
    };

    for (const auto& cfg : presets) {
        CHECK(cfg.position_freq > 0.0f);
        CHECK(cfg.position_amp >= 0.0f);
        CHECK(cfg.rotation_freq > 0.0f);
        CHECK(cfg.rotation_amp >= 0.0f);
    }
}

TEST_CASE("ShakePreset: deterministic — same seed produces same result") {
    auto cfg = ShakePreset::handheld();
    Camera2_5D cam1, cam2;
    cam1.position = cam2.position = {0.0f, 0.0f, -1000.0f};

    cfg.apply_to(cam1, Frame{30});
    cfg.apply_to(cam2, Frame{30});

    CHECK(cam1.position.x == doctest::Approx(cam2.position.x));
    CHECK(cam1.position.y == doctest::Approx(cam2.position.y));
    CHECK(cam1.position.z == doctest::Approx(cam2.position.z));
}
