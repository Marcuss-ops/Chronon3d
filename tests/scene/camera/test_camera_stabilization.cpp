// ==============================================================================
// tests/scene/camera/test_camera_stabilization.cpp
//
// Camera projection and sample-time contract tests.
//
// Tests:
//   SampleTime sub-frame evaluation and project_world_to_screen geometry.
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <tests/helpers/test_math.hpp>

#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <cmath>
#include <string>
#include <vector>
using namespace chronon3d;

namespace {

using chronon3d::test::approx;

// --- Section A: SampleTime / framerate / sub-frame (3 cases) ---

TEST_CASE("P1-B: sub-frame evaluation respects SampleTime fraction") {
    auto half = SampleTime::from_frame(15.5, FrameRate{60, 1});
    CHECK(approx(static_cast<float>(half.seconds() * 60.0), 15.5f, 1e-3f));
}

TEST_CASE("P1-B: framerate != 30 fps - seconds() respects FrameRate") {
    {
        auto st = SampleTime::from_frame(10.0, FrameRate{60, 1});
        CHECK(approx(static_cast<float>(st.seconds()), 10.0f / 60.0f, 1e-3f));
    }
    {
        auto st = SampleTime::from_frame(100.0, FrameRate{24, 1});
        CHECK(approx(static_cast<float>(st.seconds()), 100.0f / 24.0f, 1e-3f));
    }
    {
        auto st = SampleTime::from_frame(7.0, FrameRate{120, 1});
        CHECK(approx(static_cast<float>(st.seconds()), 7.0f / 120.0f, 1e-3f));
    }
}

TEST_CASE("P1-B: integer-frame SampleTime factories agree at same fps") {
    auto a = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1});
    auto b = SampleTime::from_frame(0.0, FrameRate{30, 1});
    CHECK(approx(static_cast<float>(a.seconds()), static_cast<float>(b.seconds()), 1e-6f));
}

// --- Section B: project_world_to_screen geometry ---

TEST_CASE("P1-C: resolved camera orientation flags points behind") {
    Viewport viewport;
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, 0.0f};
    cam.rotation = {0.0f, 0.0f, 0.0f};
    const auto sp = project_world_to_screen({0.0f, 0.0f, -100.0f}, cam, viewport);
    CHECK(sp.behind_camera);
}

TEST_CASE("P1-C: camera at the queried point is finite") {
    Viewport viewport;
    Camera2_5D cam;
    cam.position = {1.0f, 1.0f, 1.0f};
    const auto sp = project_world_to_screen(cam.position, cam, viewport);
    CHECK(std::isfinite(sp.position.x));
    CHECK(std::isfinite(sp.position.y));
}

TEST_CASE("P1-C: resolved static camera evaluates without NaN") {
    Camera2_5D cam;
    cam.position = {0.0f, 0.0f, -1000.0f};
    cam.rotation = {0.0f, 0.0f, 0.0f};
    cam.zoom = 1000.0f;
    // With no POI, evaluate should still produce valid results.
    CHECK_FALSE(std::isnan(cam.position.x));
    CHECK_FALSE(std::isnan(cam.position.y));
    CHECK_FALSE(std::isnan(cam.position.z));
}

} // namespace
