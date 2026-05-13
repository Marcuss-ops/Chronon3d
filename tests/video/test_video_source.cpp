#include <doctest/doctest.h>
#include <chronon3d/video/video_source.hpp>

using namespace chronon3d;
using namespace chronon3d::video;

TEST_CASE("VideoSource maps frame with hold last") {
    VideoSource src;
    src.source_start = 10;
    src.duration = 20;
    src.speed = 1.0f;
    src.loop_mode = VideoLoopMode::HoldLastFrame;

    CHECK(map_video_frame(0, src) == 10);
    CHECK(map_video_frame(5, src) == 15);
    CHECK(map_video_frame(19, src) == 29);
    CHECK(map_video_frame(20, src) == 29); // Hold last
    CHECK(map_video_frame(99, src) == 29);
}

TEST_CASE("VideoSource loops frames") {
    VideoSource src;
    src.source_start = 0;
    src.duration = 10;
    src.loop_mode = VideoLoopMode::Loop;

    CHECK(map_video_frame(0, src) == 0);
    CHECK(map_video_frame(9, src) == 9);
    CHECK(map_video_frame(10, src) == 0);
    CHECK(map_video_frame(15, src) == 5);
    CHECK(map_video_frame(20, src) == 0);
}

TEST_CASE("VideoSource ping-pong") {
    VideoSource src;
    src.source_start = 0;
    src.duration = 10;
    src.loop_mode = VideoLoopMode::PingPong;

    CHECK(map_video_frame(0, src) == 0);
    CHECK(map_video_frame(9, src) == 9);
    CHECK(map_video_frame(10, src) == 9); // Cycle is 20: 0-9, 9-0
    CHECK(map_video_frame(11, src) == 8);
    CHECK(map_video_frame(19, src) == 0);
    CHECK(map_video_frame(20, src) == 0);
}

TEST_CASE("VideoSource with speed") {
    VideoSource src;
    src.source_start = 0;
    src.duration = 100;
    src.speed = 2.0f;
    src.loop_mode = VideoLoopMode::None;

    CHECK(map_video_frame(0, src) == 0);
    CHECK(map_video_frame(10, src) == 20);
    CHECK(map_video_frame(50, src) == 99); // Hold last because speed * frame >= duration
}
