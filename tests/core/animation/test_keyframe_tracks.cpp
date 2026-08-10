#include <doctest/doctest.h>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
using namespace chronon3d;


TEST_CASE("KeyframeTrack Basic Interpolation") {
    auto track = keyframes<f32>({
        {0,  0.0f,  Easing::Linear},
        {60, 100.0f, Easing::Linear}
    });

    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{-10}, FrameRate{30, 1})) == doctest::Approx(0.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}))   == doctest::Approx(0.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1}))  == doctest::Approx(50.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{60}, FrameRate{30, 1}))  == doctest::Approx(100.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{70}, FrameRate{30, 1}))  == doctest::Approx(100.0f));
}

TEST_CASE("KeyframeTrack Hold Easing") {
    auto track = keyframes<f32>({
        {0,  10.0f, Easing::Hold},
        {30, 20.0f, Easing::Hold},
        {60, 30.0f, Easing::Hold}
    });

    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}))  == doctest::Approx(10.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{15}, FrameRate{30, 1})) == doctest::Approx(10.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{29}, FrameRate{30, 1})) == doctest::Approx(10.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1})) == doctest::Approx(20.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{45}, FrameRate{30, 1})) == doctest::Approx(20.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{60}, FrameRate{30, 1})) == doctest::Approx(30.0f));
}

TEST_CASE("KeyframeTrack Vec3 Interpolation") {
    auto track = keyframes<Vec3>({
        {0,  {0, 0, 0}},
        {100, {100, 200, 300}}
    });

    Vec3 v = track.evaluate(SampleTime::from_frame_int(Frame{50}, FrameRate{30, 1}));
    CHECK(v.x == doctest::Approx(50.0f));
    CHECK(v.y == doctest::Approx(100.0f));
    CHECK(v.z == doctest::Approx(150.0f));
}

TEST_CASE("KeyframeTrack Color Interpolation") {
    auto track = keyframes<Color>({
        {0,   {1, 0, 0, 1}}, // Red
        {100, {0, 0, 1, 1}}  // Blue
    });

    Color c = track.evaluate(SampleTime::from_frame_int(Frame{50}, FrameRate{30, 1}));
    CHECK(c.r == doctest::Approx(0.5f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.5f));
    CHECK(c.a == doctest::Approx(1.0f));
}

TEST_CASE("KeyframeTrack New API Syntax") {
    auto track = keyframes<f32>({})
        .key(0,  -300.0f, Easing::OutCubic)
        .key(40, 0.0f,    Easing::OutBack)
        .key(90, 120.0f,  Easing::InOutSine);

    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{40}, FrameRate{30, 1})) == doctest::Approx(0.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{40}, FrameRate{30, 1})) == doctest::Approx(0.0f));
}

TEST_CASE("KeyframeTrack Empty Track Returns Default Value") {
    CHECK(keyframes<f32>({}).evaluate(SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})) == doctest::Approx(0.0f));
    CHECK(keyframes<Vec3>({}).evaluate(SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})).x == doctest::Approx(0.0f));
}

TEST_CASE("KeyframeTrack Duplicate Frames Use Last Keyframe At The Frame") {
    auto track = keyframes<f32>({})
        .key(0, 10.0f)
        .key(30, 20.0f)
        .key(30, 40.0f)
        .key(60, 100.0f);

    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{29}, FrameRate{30, 1})) == doctest::Approx(10.0f + (20.0f - 10.0f) * (29.0f / 30.0f)).epsilon(0.0001f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1})) == doctest::Approx(40.0f));
    CHECK(track.evaluate(SampleTime::from_frame_int(Frame{45}, FrameRate{30, 1})) == doctest::Approx(70.0f));
}

TEST_CASE("KeyframeTrack Legacy Compatibility") {
    // Verifying the old API still works
    f32 v = keyframes(SampleTime::from_frame_int(Frame{30}, FrameRate{30, 1}), { KF{0, 0.0f}, KF{60, 100.0f} });
    CHECK(v == doctest::Approx(50.0f));
}
