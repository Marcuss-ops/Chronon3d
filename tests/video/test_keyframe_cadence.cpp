// ═══════════════════════════════════════════════════════════════════════════
// tests/video/test_keyframe_cadence.cpp
//
// Producer-side guard for the keyframe interval, the dimension that shipped
// broken and was caught only by the CONSUMER's output-facts gateway:
//
//   clip.render failed with
//   "rendered output violates the output contract: keyframe interval 24 != 48"
//
// because both native encoder paths derived gop_size from the frame rate alone
// (one keyframe per second = 24 @ 24 fps) while VELOX_ASSEMBLY_READY_V1 fixes
// `keyframe_interval: 48` on its 24 fps timeline (a 2-second cadence).
//
// The lesson recorded in the ticket is explicitly that a gateway test is NOT
// enough: the gateway could not fail while the producer's binary was stale, so
// the producer needs its own assertion. This file pins the cadence authority;
// tests/video/test_native_av_sink_lifecycle.cpp additionally decodes the real
// muxed output and asserts the emitted keyframe spacing.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/media/video/keyframe_cadence.hpp>

#include <doctest/doctest.h>

using chronon3d::media::video::kKeyframeIntervalSeconds;
using chronon3d::media::video::keyframe_interval_frames;

TEST_CASE("keyframe cadence: the 2-second GOP is the assembly-ready contract value") {
    // VELOX_ASSEMBLY_READY_V1 fixes keyframe_interval: 48 on a 24 fps
    // timeline. 48 / 24 fps == 2 s, which is the cadence this helper encodes.
    CHECK(kKeyframeIntervalSeconds == doctest::Approx(2.0));
    CHECK(keyframe_interval_frames(24, 1) == 48);
}

TEST_CASE("keyframe cadence: the GOP is a time cadence, not a frame count") {
    // Deriving the GOP from the frame rate alone (fps) is the defect; deriving
    // it from the TIME cadence keeps the contract value at the certified rate
    // and stays coherent at every other rate.
    CHECK(keyframe_interval_frames(1, 1) == 2);
    CHECK(keyframe_interval_frames(24, 1) == 48);
    CHECK(keyframe_interval_frames(25, 1) == 50);   // 2 s of PAL
    CHECK(keyframe_interval_frames(30, 1) == 60);
    CHECK(keyframe_interval_frames(50, 1) == 100);
    CHECK(keyframe_interval_frames(60, 1) == 120);
    // Fractional rates: 23.976 fps * 2 s = 47.95 -> 48 frames, i.e. the same
    // contract interval as the exact 24 fps rate (the certified timeline).
    CHECK(keyframe_interval_frames(24000, 1001) == 48);
    CHECK(keyframe_interval_frames(30000, 1001) == 60);
    CHECK(keyframe_interval_frames(60000, 1001) == 120);
}

TEST_CASE("keyframe cadence: a degenerate rate never disables keyframes") {
    // A stream with no keyframe is undecodable from any seek point, so a bad
    // rate degrades to "keyframe every frame" rather than to "no keyframe".
    // The caller's validation owns rejecting the rate itself.
    CHECK(keyframe_interval_frames(0, 1) == 1);
    CHECK(keyframe_interval_frames(24, 0) == 1);
    CHECK(keyframe_interval_frames(-24, 1) == 1);
    CHECK(keyframe_interval_frames(24, -1) == 1);
    CHECK(keyframe_interval_frames(0, 0) == 1);
}

TEST_CASE("keyframe cadence: the contract interval is never below one frame") {
    // Guards the std::max(1, ...) floor against a sub-1-fps rate rounding to 0.
    for (int num : {1, 2, 3, 7, 12}) {
        CHECK(keyframe_interval_frames(num, 1000) >= 1);
    }
}
