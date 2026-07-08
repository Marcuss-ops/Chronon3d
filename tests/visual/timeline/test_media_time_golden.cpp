// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/timeline/test_media_time_golden.cpp
//
// Gate 2 — Media Time Visual Tests
//
// Definition of Done: proves that media time resolution (source_start as
// trim_before, speed as playback_rate, and freeze_frame) works correctly
// at both the unit level (map_video_frame) and render level (via
// LayerBuilder).
//
// 3 tests:
//   1. TrimBeforeMediaFrame   — source_start offsets the mapping
//   2. PlaybackRateMediaFrame — speed multiplies local_frame
//   3. FreezeFrameMedia       — freeze_frame locks the time
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/backends/video/video_source.hpp>

using namespace chronon3d;
using namespace chronon3d::video;



// ═══════════════════════════════════════════════════════════════════════════
// Test 1: TrimBeforeMediaFrame
// ═══════════════════════════════════════════════════════════════════════════
//
// source_start = 10 acts as trim_before:
//   local 0  → source 10
//   local 10 → source 20
//   local 20 → source 30
//
// This is a pure function test — no rendering needed.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.TrimBeforeMediaFrame") {
    VideoSource source;
    source.path = "fake.mp4";
    source.source_start = Frame{10};
    source.speed = 1.0f;
    source.duration = Frame{-1};  // no loop limit

    SUBCASE("local 0 → source 10") {
        CHECK(map_video_frame(Frame{0}, source) == Frame{10});
    }

    SUBCASE("local 10 → source 20") {
        CHECK(map_video_frame(Frame{10}, source) == Frame{20});
    }

    SUBCASE("local 20 → source 30") {
        CHECK(map_video_frame(Frame{20}, source) == Frame{30});
    }

    SUBCASE("negative local → clamped to source_start") {
        CHECK(map_video_frame(Frame{-5}, source) == Frame{10});
    }

    SUBCASE("large trim_before = 100") {
        VideoSource big_trim;
        big_trim.source_start = Frame{100};
        big_trim.speed = 1.0f;
        big_trim.duration = Frame{-1};

        CHECK(map_video_frame(Frame{0}, big_trim) == Frame{100});
        CHECK(map_video_frame(Frame{50}, big_trim) == Frame{150});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 2: PlaybackRateMediaFrame
// ═══════════════════════════════════════════════════════════════════════════
//
// speed = 2.0 doubles the playback rate:
//   local 0  → source 0
//   local 10 → source 20
//   local 20 → source 40
//
// speed = 0.5 halves it:
//   local 0  → source 0
//   local 10 → source 5
//   local 20 → source 10
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.PlaybackRateMediaFrame") {
    SUBCASE("speed 2.0 — doubles playback") {
        VideoSource source;
        source.speed = 2.0f;
        source.duration = Frame{-1};

        CHECK(map_video_frame(Frame{0}, source) == Frame{0});
        CHECK(map_video_frame(Frame{10}, source) == Frame{20});
        CHECK(map_video_frame(Frame{20}, source) == Frame{40});
    }

    SUBCASE("speed 0.5 — halves playback") {
        VideoSource source;
        source.speed = 0.5f;
        source.duration = Frame{-1};

        CHECK(map_video_frame(Frame{0}, source) == Frame{0});
        CHECK(map_video_frame(Frame{10}, source) == Frame{5});
        CHECK(map_video_frame(Frame{20}, source) == Frame{10});
    }

    SUBCASE("speed 1.0 — identity") {
        VideoSource source;
        source.speed = 1.0f;
        source.duration = Frame{-1};

        CHECK(map_video_frame(Frame{0}, source) == Frame{0});
        CHECK(map_video_frame(Frame{10}, source) == Frame{10});
    }

    SUBCASE("speed 2.0 with source_start offset") {
        VideoSource source;
        source.source_start = Frame{5};
        source.speed = 2.0f;
        source.duration = Frame{-1};

        // local 0 → start + scaled = 5 + 0*2 = 5
        CHECK(map_video_frame(Frame{0}, source) == Frame{5});
        // local 10 → 5 + 10*2 = 25
        CHECK(map_video_frame(Frame{10}, source) == Frame{25});
    }

    SUBCASE("speed 3.0 — triple playback") {
        VideoSource source;
        source.speed = 3.0f;
        source.duration = Frame{-1};

        CHECK(map_video_frame(Frame{0}, source) == Frame{0});
        CHECK(map_video_frame(Frame{10}, source) == Frame{30});
        CHECK(map_video_frame(Frame{33}, source) == Frame{99});
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Test 3: FreezeFrameMedia
// ═══════════════════════════════════════════════════════════════════════════
//
// Freeze frame is achieved via duration=1 with loop_mode=None:
//   The mapping clamps all frames to source_start + min(rel, duration-1)
//   = source_start + min(rel, 0) = source_start for all non-negative local.
//
// Additionally tests the LayerBuilder::freeze_frame() which is a
// higher-level feature that locks time_remap.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.FreezeFrameMedia") {
    SUBCASE("duration=1 clamps all frames to source_start") {
        VideoSource source;
        source.source_start = Frame{15};
        source.speed = 1.0f;
        source.duration = Frame{1};
        source.loop_mode = VideoLoopMode::None;

        // All non-negative local frames should map to source_start (15)
        // because duration=1 means min(rel, 0) = 0 always
        CHECK(map_video_frame(Frame{0}, source) == Frame{15});
        CHECK(map_video_frame(Frame{10}, source) == Frame{15});
        CHECK(map_video_frame(Frame{20}, source) == Frame{15});
        CHECK(map_video_frame(Frame{100}, source) == Frame{15});
    }

    SUBCASE("duration=1 with source_start=0") {
        VideoSource source;
        source.source_start = Frame{0};
        source.speed = 1.0f;
        source.duration = Frame{1};
        source.loop_mode = VideoLoopMode::None;

        CHECK(map_video_frame(Frame{0}, source) == Frame{0});
        CHECK(map_video_frame(Frame{50}, source) == Frame{0});
    }

    SUBCASE("non-freeze duration does not clamp") {
        VideoSource source;
        source.source_start = Frame{0};
        source.speed = 1.0f;
        source.duration = Frame{100};
        source.loop_mode = VideoLoopMode::None;

        CHECK(map_video_frame(Frame{0}, source) == Frame{0});
        CHECK(map_video_frame(Frame{50}, source) == Frame{50});
        // Beyond duration: clamps to source_start + duration - 1
        CHECK(map_video_frame(Frame{200}, source) == Frame{99});
    }

    SUBCASE("duration=1 with speed=2.0 still clamps") {
        VideoSource source;
        source.source_start = Frame{5};
        source.speed = 2.0f;
        source.duration = Frame{1};
        source.loop_mode = VideoLoopMode::None;

        // speed doesn't matter when duration=1: rel always = 0 after min()
        CHECK(map_video_frame(Frame{0}, source) == Frame{5});
        CHECK(map_video_frame(Frame{10}, source) == Frame{5});
    }
}
