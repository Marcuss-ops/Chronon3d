// tests/video/test_video_source_window.cpp
//
// A requested render window may outlive the decoded source. The decoder must
// never keep reading past EOF assuming frames exist: map_video_frame applies
// the source's declared end policy. This pins the two canonical policies used
// by the video node before it retries on EOS.

#include <doctest/doctest.h>

#include <chronon3d/backends/video/video_source.hpp>

using chronon3d::Frame;
using chronon3d::video::VideoLoopMode;
using chronon3d::video::VideoSource;
using chronon3d::video::map_video_frame;

namespace {

VideoSource source_with(VideoLoopMode mode, Frame duration) {
    VideoSource source;
    source.source_start = Frame{0};
    source.duration = duration;
    source.loop_mode = mode;
    return source;
}

} // namespace

TEST_CASE("video window clamps when the source is shorter than the request") {
    const auto source = source_with(VideoLoopMode::None, Frame{100});
    CHECK(map_video_frame(Frame{0}, source).integral() == 0);
    CHECK(map_video_frame(Frame{99}, source).integral() == 99);
    CHECK(map_video_frame(Frame{100}, source).integral() == 99);
    CHECK(map_video_frame(Frame{250}, source).integral() == 99);
}

TEST_CASE("video window loops only when the source authorizes Loop") {
    const auto source = source_with(VideoLoopMode::Loop, Frame{100});
    CHECK(map_video_frame(Frame{99}, source).integral() == 99);
    CHECK(map_video_frame(Frame{100}, source).integral() == 0);
    CHECK(map_video_frame(Frame{250}, source).integral() == 50);
}

TEST_CASE("video window respects source_start on loop and clamp") {
    auto source = source_with(VideoLoopMode::Loop, Frame{100});
    source.source_start = Frame{1000};
    CHECK(map_video_frame(Frame{0}, source).integral() == 1000);
    CHECK(map_video_frame(Frame{100}, source).integral() == 1000);
    CHECK(map_video_frame(Frame{150}, source).integral() == 1050);
}
