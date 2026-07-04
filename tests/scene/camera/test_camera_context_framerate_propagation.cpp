// ==============================================================================
// tests/scene/camera/test_camera_context_framerate_propagation.cpp
//
// TICKET-A3-CTX-FRAMERATE (Agent3 mission DoD gate (e)) — regression lock
// that locks the public contract of `camera_v1::*Context::at(...)` factories:
//
//   CameraEvalContext::at(Frame f, FrameRate frame_rate, ...)
//   CameraMotionContext::at(Frame f, FrameRate frame_rate)
//
// MUST propagate the caller-supplied FrameRate bit-exactly into
// SampleTime arithmetic — NO default-init fallback to FrameRate{30, 1}
// anywhere on the factory call chain.
//
// The companion source change lives in
// src/scene/camera/camera_v1/shot_timeline.cpp — the previously-terse
// first-arm comment "CAM-05: explicit FrameRate (TODO: plumb from
// composition/project)" is replaced with a forward-exact call to
// CameraEvalContext::at(local_frame, fps) using the resolver's `fps`
// parameter (no fixture constexpr {30, 1} inside the resolver).  This
// test locks BOTH ends of the same propagation contract:
//
//   1. The two factory functions (CameraEvalContext::at, CameraMotionContext::at)
//      do not silently substitute FrameRate{30, 1} when a non-30 caller
//      FrameRate is passed in.
//   2. The default viewport (vp_w=1920, vp_h=1080) is preserved when
//      callers omit viewport arguments.
//   3. Default `transforms == nullptr` / `base_* == zero` members are
//      preserved (no implicit lookups).
//
// Discriminating observation: ctx.sample_time.seconds() differs FPS-by-FPS.
// A regression that re-introduced a FrameRate{30,1} default-init in the
// factory would surface as a CHECK failure on every fps != 30 because
// the fuzzer-expected value (7 / fps.denom-precision) would collide
// against the silently-substituted 7 / 30.0 = 0.2333...
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/time.hpp>   // FrameRate (complete type for brace-init + .num/.den access)

#include <algorithm>   // std::sort, std::unique (camera_v1_context_at_unique_per_fps)
#include <iterator>    // std::distance
#include <vector>

using namespace chronon3d;
using namespace chronon3d::camera_v1;

namespace {

// Pure registry: FrameRate values spread across cinematic (24), PAL (25),
// NTSC (30), high-frame-rate (50/60/120) bands. Each value MUST yield a
// different `sample_time.seconds()` for the same Frame{7} (the discriminating
// observation). If any of these collide post-fix, either:
//   (a) the factory silently substituted FrameRate{30, 1} (the regression
//       this test exists to catch), or
//   (b) `SampleTime::from_frame_int` arithmetic has been changed, or
//   (c) the FrameRate struct has been changed (num/den swapped).
struct FuzzValue {
    FrameRate fps;
    double    expected_seconds_at_frame_7;
};

const std::vector<FuzzValue> kFuzz = {
    {FrameRate{24, 1},   7.0 / 24.0},   // cinema
    {FrameRate{25, 1},   7.0 / 25.0},   // PAL
    {FrameRate{30, 1},   7.0 / 30.0},   // NTSC (matches the removed kTimelineFps)
    {FrameRate{50, 1},   7.0 / 50.0},   // PAL high-frame
    {FrameRate{60, 1},   7.0 / 60.0},   // HD / streaming
    {FrameRate{120, 1},  7.0 / 120.0},  // high-frame-rate
};

} // namespace

TEST_CASE(
    "camera_v1_context_at_propagates_framerate_bit_exact — "
    "TICKET-A3-CTX-FRAMERATE DoD gate (e): CameraEvalContext::at() and "
    "CameraMotionContext::at() propagate the caller-supplied FrameRate to "
    "SampleTime arithmetic with no default-init fallback and no magic 30 fps "
    "sentinel. The discriminator observation is ctx.sample_time.seconds() — "
    "every fuzz value != 30 yields a distinct seconds-value that would "
    "collide if the factory silently substituted FrameRate{30, 1}.") {

    for (const auto& fz : kFuzz) {
        CAPTURE(fz.fps.numerator);    // FrameRate data field (see core/types/time.hpp:9-14)
        CAPTURE(fz.fps.denominator);  // FrameRate data field (see core/types/time.hpp:9-14)

        // ── CameraEvalContext ────────────────────────────────────────
        CameraEvalContext ectx = CameraEvalContext::at(Frame{7}, fz.fps);
        // Bit-exact integer copy.
        CHECK(static_cast<int>(ectx.frame) == 7);
        // Discriminator: sample_time MUST equal 7 / (num/den), NOT 7 / 30.
        CHECK(ectx.sample_time.seconds() ==
              doctest::Approx(fz.expected_seconds_at_frame_7).epsilon(1e-9));
        // Default viewport preserved when caller omits it.
        CHECK(ectx.viewport_width  == 1920);
        CHECK(ectx.viewport_height == 1080);
        // No implicit lookups.
        CHECK(ectx.transforms == nullptr);

        // ── CameraMotionContext ──────────────────────────────────────
        CameraMotionContext mctx = CameraMotionContext::at(Frame{7}, fz.fps);
        CHECK(static_cast<int>(mctx.frame) == 7);
        CHECK(mctx.sample_time.seconds() ==
              doctest::Approx(fz.expected_seconds_at_frame_7).epsilon(1e-9));
        // base_* defaults preserved (no implicit lookups).
        CHECK(mctx.base_position.x == doctest::Approx(0.0f).epsilon(1e-9));
        CHECK(mctx.base_position.y == doctest::Approx(0.0f).epsilon(1e-9));
        CHECK(mctx.base_position.z == doctest::Approx(0.0f).epsilon(1e-9));
        CHECK(mctx.base_target.x   == doctest::Approx(0.0f).epsilon(1e-9));
        CHECK(mctx.base_target.y   == doctest::Approx(0.0f).epsilon(1e-9));
        CHECK(mctx.base_target.z   == doctest::Approx(0.0f).epsilon(1e-9));
        CHECK(mctx.base_zoom            == doctest::Approx(1.0f).epsilon(1e-9));
        CHECK(mctx.base_fov_deg         == doctest::Approx(45.0f).epsilon(1e-9));
        CHECK(mctx.base_focus_distance  == doctest::Approx(1000.0f).epsilon(1e-9));
        CHECK(mctx.scene_unit_scale     == doctest::Approx(1.0f).epsilon(1e-9));
    }

    // Caller-supplied viewport (4K) propagates without leaking the default.
    CameraEvalContext custom_vp = CameraEvalContext::at(
        Frame{0}, FrameRate{60, 1}, /*vp_w=*/3840, /*vp_h=*/2160);
    CHECK(custom_vp.viewport_width  == 3840);
    CHECK(custom_vp.viewport_height == 2160);
    // Verify the same FPS at Frame{0} is bit-exact zero (no off-by-one).
    CHECK(static_cast<int>(custom_vp.frame) == 0);
    CHECK(custom_vp.sample_time.seconds() == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE(
    "camera_v1_context_at_unique_per_fps — "
    "TICKET-A3-CTX-FRAMERATE DoD gate (e) discriminator: ctx.sample_time.seconds() "
    "MUST differ across all 6 fuzzed FrameRate values at Frame{7}.  If two "
    "values collide, either the factory silently substitutes a default fps, or "
    "the SampleTime arithmetic has lost fps-sensitivity.  This is the "
    "public-API-level consistency invariant.") {

    std::vector<double> seen_seconds;
    for (const auto& fz : kFuzz) {
        CameraEvalContext ectx = CameraEvalContext::at(Frame{7}, fz.fps);
        seen_seconds.push_back(ectx.sample_time.seconds());
    }
    // Sort-then-unique: a duplicate would compress 6 distinct values to fewer.
    std::vector<double> sorted = seen_seconds;
    std::sort(sorted.begin(), sorted.end());
    auto last_unique = std::unique(sorted.begin(), sorted.end());
    const std::size_t distinct = static_cast<std::size_t>(
        std::distance(sorted.begin(), last_unique));
    CAPTURE(distinct);
    CHECK(distinct == kFuzz.size());
}
