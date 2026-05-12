#include <doctest/doctest.h>
#include <chronon3d/renderer/video_frame_provider.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// source_time computation (no ffmpeg needed)
// ---------------------------------------------------------------------------
TEST_CASE("VideoFrameProvider: source_time basic") {
    VideoDesc v;
    v.trim_start    = 2.0f;
    v.playback_rate = 1.0f;
    v.loop          = false;

    // frame 30 at 30fps = 1 second into composition
    float t = VideoFrameProvider::source_time(v, 30, 30.0f, 0.0f);
    CHECK(t == doctest::Approx(3.0f));  // 2.0 + 1.0
}

TEST_CASE("VideoFrameProvider: source_time with playback_rate") {
    VideoDesc v;
    v.trim_start    = 0.0f;
    v.playback_rate = 2.0f;
    v.loop          = false;

    // frame 30 at 30fps → comp time 1.0s × 2.0 = 2.0s source
    float t = VideoFrameProvider::source_time(v, 30, 30.0f, 0.0f);
    CHECK(t == doctest::Approx(2.0f));
}

TEST_CASE("VideoFrameProvider: source_time loops correctly") {
    VideoDesc v;
    v.trim_start    = 0.0f;
    v.playback_rate = 1.0f;
    v.loop          = true;

    // comp frame 60 at 30fps = 2.0s; video is 1.5s → loops to 0.5s
    float t = VideoFrameProvider::source_time(v, 60, 30.0f, 1.5f);
    CHECK(t == doctest::Approx(0.5f));
}

TEST_CASE("VideoFrameProvider: source_time trim_start 0 frame 0") {
    VideoDesc v;
    v.trim_start = 0.0f;
    v.playback_rate = 1.0f;
    float t = VideoFrameProvider::source_time(v, 0, 30.0f, 0.0f);
    CHECK(t == doctest::Approx(0.0f));
}

TEST_CASE("VideoFrameProvider: source_time zero fps returns trim_start") {
    VideoDesc v;
    v.trim_start = 5.0f;
    float t = VideoFrameProvider::source_time(v, 100, 0.0f, 0.0f);
    CHECK(t == doctest::Approx(5.0f));
}

// ---------------------------------------------------------------------------
// VideoDesc defaults
// ---------------------------------------------------------------------------
TEST_CASE("VideoDesc: default values") {
    VideoDesc v;
    CHECK(v.trim_start    == doctest::Approx(0.0f));
    CHECK(v.playback_rate == doctest::Approx(1.0f));
    CHECK_FALSE(v.loop);
    CHECK(v.opacity       == doctest::Approx(1.0f));
}

// ---------------------------------------------------------------------------
// ffmpeg_available (just doesn't crash — result depends on system)
// ---------------------------------------------------------------------------
TEST_CASE("VideoFrameProvider: ffmpeg_available does not crash") {
    bool avail = VideoFrameProvider::ffmpeg_available();
    (void)avail;  // result may be true or false depending on system
    CHECK(true);
}

// ---------------------------------------------------------------------------
// frame_path returns empty when ffmpeg unavailable (skip if ffmpeg present)
// ---------------------------------------------------------------------------
TEST_CASE("VideoFrameProvider: frame_path with non-existent video") {
    if (VideoFrameProvider::ffmpeg_available()) {
        // ffmpeg available: skip — we can't test with a real video here
        return;
    }
    VideoFrameProvider vfp("output/.vcache_test");
    VideoDesc v; v.path = "nonexistent_video.mp4";
    std::string result = vfp.frame_path(v, 0, 30.0f, 0.0f);
    CHECK(result.empty());
}
