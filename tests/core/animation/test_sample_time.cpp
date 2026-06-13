#include <doctest/doctest.h>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>

using namespace chronon3d;

// ===========================================================================
// SampleTime — factories and conversions
// ===========================================================================
TEST_CASE("SampleTime: from_frame factory") {
    auto t = SampleTime::from_frame(12.375, 30.0);
    CHECK(t.frame == doctest::Approx(12.375));
    CHECK(t.seconds == doctest::Approx(12.375 / 30.0));
    CHECK(t.fps == doctest::Approx(30.0));
}

TEST_CASE("SampleTime: from_seconds factory") {
    auto t = SampleTime::from_seconds(2.5, 30.0);
    CHECK(t.seconds == doctest::Approx(2.5));
    CHECK(t.frame == doctest::Approx(75.0));  // 2.5 * 30 = 75
}

TEST_CASE("SampleTime: from_frame_int (backward compat)") {
    auto t = SampleTime::from_frame_int(45, 30.0);
    CHECK(t.frame == doctest::Approx(45.0));
    CHECK(t.seconds == doctest::Approx(1.5));
}

// ===========================================================================
// SampleTimeKey — deterministic hashing
// ===========================================================================
TEST_CASE("SampleTimeKey: same time → same key") {
    auto k1 = SampleTimeKey::from_sample_time(SampleTime::from_frame(12.375, 30.0));
    auto k2 = SampleTimeKey::from_sample_time(SampleTime::from_frame(12.375, 30.0));
    CHECK(k1 == k2);
    CHECK(k1.ticks == k2.ticks);
}

TEST_CASE("SampleTimeKey: different times → different keys") {
    auto k1 = SampleTimeKey::from_sample_time(SampleTime::from_frame(12.0, 30.0));
    auto k2 = SampleTimeKey::from_sample_time(SampleTime::from_frame(12.5, 30.0));
    CHECK(k1 != k2);
}

TEST_CASE("SampleTimeKey: quantised sub-frame produces distinct keys") {
    auto k0 = SampleTimeKey::from_sample_time(SampleTime::from_frame(0.0, 30.0));
    auto k1 = SampleTimeKey::from_sample_time(
        SampleTime::from_frame(1.0 / 8.0, 30.0));  // 0.125 frames → 8192 ticks
    CHECK(k0 != k1);
    // 0.125 * 65536 = 8192
    CHECK(k1.ticks == 8192);
}

TEST_CASE("SampleTimeKey: from_frame_int matches integer frame") {
    auto k = SampleTimeKey::from_frame_int(42);
    CHECK(k.ticks == 42 * SampleTimeKey::kTicksPerFrame);
}

TEST_CASE("SampleTimeKey: ordering works") {
    auto k0 = SampleTimeKey::from_sample_time(SampleTime::from_frame(0.0));
    auto k5 = SampleTimeKey::from_sample_time(SampleTime::from_frame(5.0));
    CHECK(k0 < k5);
}

// ===========================================================================
// AnimatedValue<T> sub-frame evaluation — THE DECISIVE TEST
// ===========================================================================
TEST_CASE("AnimatedValue: sub-frame produces different values than integer frame") {
    // Per il verdetto: "render frame 0 con 8 sample; le otto valutazioni
    // devono produrre otto posizioni differenti."
    AnimatedValue<Vec3> v(Vec3{0, 0, 0});
    v.key(0, Vec3{0, 0, -1000});
    v.key(1, Vec3{100, 0, -1000});

    // Evaluate at 8 sub-frame positions within frame 0
    std::vector<Vec3> values;
    for (int s = 0; s < 8; ++s) {
        // Simulate motion blur: sample at frame + (s + 0.5) / 8 * 0.5
        // (180° shutter: half frame exposure)
        double t = (static_cast<double>(s) + 0.5) / 8.0 * 0.5;
        auto st = SampleTime::from_frame(t, 30.0);
        values.push_back(v.evaluate(st));
    }

    // Check that NOT all values are identical — this is the critical test!
    bool all_same = true;
    for (int s = 1; s < 8; ++s) {
        if (glm::length(values[s] - values[0]) > 0.001f) {
            all_same = false;
            break;
        }
    }
    CHECK_FALSE(all_same);  // <— THE decisive assertion from the vendetta doc

    // Check that values are monotonically increasing in X
    for (int s = 1; s < 8; ++s) {
        CHECK(values[s].x >= values[s-1].x);
    }

    // The last sample should be closer to the final keyframe
    CHECK(values[7].x > values[0].x);
}

TEST_CASE("AnimatedValue f32: sub-frame evaluation") {
    AnimatedValue<f32> v(0.0f);
    v.key(0, 0.0f).key(60, 120.0f);

    // At frame 30 (midpoint): value should be 60
    CHECK(v.evaluate(SampleTime::from_frame(30.0)) == doctest::Approx(60.0f));

    // At frame 30.5: slightly past midpoint
    CHECK(v.evaluate(SampleTime::from_frame(30.5)) > doctest::Approx(60.0f));

    // At frame 30 with integer key: should also work
    CHECK(v.evaluate(SampleTime::from_frame_int(30)) == doctest::Approx(60.0f));
}

TEST_CASE("AnimatedValue: Frame overload still works (backward compat)") {
    AnimatedValue<f32> v(0.0f);
    v.key(0, 0.0f).key(60, 120.0f);

    // Old-style Frame evaluation must still return correct values
    CHECK(v.value_at(30) == doctest::Approx(60.0f));
    CHECK(v.evaluate(30) == doctest::Approx(60.0f));
}

TEST_CASE("AnimatedValue: Frame and SampleTime agree at integer frames") {
    AnimatedValue<Vec3> v(Vec3{0, 0, 0});
    v.key(0, Vec3{0, 0, 0});
    v.key(60, Vec3{100, 200, 300}, EasingCurve{Easing::InOutCubic});

    for (int f = 0; f <= 60; f += 10) {
        Vec3 frame_val = v.evaluate(Frame{f});
        Vec3 sample_val = v.evaluate(SampleTime::from_frame_int(f));
        CHECK(glm::length(frame_val - sample_val) < 0.001f);
    }
}

// ===========================================================================
// AnimatedCamera2_5D sub-frame evaluation
// ===========================================================================
TEST_CASE("AnimatedCamera2_5D: sub-frame evaluation") {
    AnimatedCamera2_5D cam;
    cam.position
        .key(0, Vec3{0, 0, -1000})
        .key(60, Vec3{100, 0, -1000});

    // Integer frame
    Camera2_5D c0 = cam.evaluate(Frame{0});
    Camera2_5D c30 = cam.evaluate(Frame{30});
    Camera2_5D c60 = cam.evaluate(Frame{60});

    CHECK(c0.position.x == doctest::Approx(0.0f));
    CHECK(c30.position.x == doctest::Approx(50.0f));
    CHECK(c60.position.x == doctest::Approx(100.0f));

    // Sub-frame: frame 30.5 should be slightly past midpoint
    Camera2_5D c30_5 = cam.evaluate(SampleTime::from_frame(30.5));
    CHECK(c30_5.position.x > 50.0f);
    CHECK(c30_5.position.x < 100.0f);
}

TEST_CASE("AnimatedCamera2_5D: Frame and SampleTime agree at integer frames") {
    AnimatedCamera2_5D cam;
    cam.position
        .key(0, Vec3{0, 0, -1000})
        .key(60, Vec3{100, 200, -500});

    for (int f = 0; f <= 60; f += 15) {
        Camera2_5D frame_cam = cam.evaluate(Frame{f});
        Camera2_5D sample_cam = cam.evaluate(SampleTime::from_frame_int(f));
        CHECK(glm::length(frame_cam.position - sample_cam.position) < 0.001f);
    }
}

// ===========================================================================
// CameraRig sub-frame evaluation
// ===========================================================================
TEST_CASE("CameraRig: sub-frame evaluation") {
    CameraRig rig;
    rig.mode = CameraRigMode::OneNode;
    rig.target.set(Vec3{0, 0, 0});
    rig.tilt.key(0, 0.0f).key(60, 60.0f);

    Camera2_5D c0 = rig.evaluate(Frame{0});
    Camera2_5D c30 = rig.evaluate(Frame{30});

    CHECK(c0.rotation.x == doctest::Approx(0.0f));
    CHECK(c30.rotation.x == doctest::Approx(30.0f));

    // Sub-frame
    Camera2_5D c15_5 = rig.evaluate(SampleTime::from_frame(15.5));
    CHECK(c15_5.rotation.x == doctest::Approx(15.5f).epsilon(0.01f));
}
