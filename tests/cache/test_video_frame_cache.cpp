#include <doctest/doctest.h>

#include <chronon3d/cache/video_frame_cache.hpp>
using namespace chronon3d;

using namespace chronon3d::cache;

TEST_CASE("VideoFrame allocates expected buffer sizes") {
    VideoFrame rgba(10, 10, VideoPixelFormat::RGBA8);
    CHECK(rgba.size() == 400);
    CHECK(rgba.expected_size() == 400);

    VideoFrame yuv(10, 10, VideoPixelFormat::YUV420P);
    CHECK(yuv.size() == 150);
    CHECK(yuv.expected_size() == 150);
}

TEST_CASE("VideoFrameCache stores and finds frames by digest") {
    VideoFrameCache cache;
    auto frame = std::make_shared<VideoFrame>(1920, 1080, VideoPixelFormat::YUV420P);

    VideoFrameKey key{
        .composition_id = "GridCleanBackground",
        .frame_index = 0,
        .width = 1920,
        .height = 1080,
        .format = VideoPixelFormat::YUV420P,
        .scene_hash = 123,
        .render_hash = 456,
    };

    CHECK(!cache.contains(key));
    cache.store(key, frame);
    CHECK(cache.contains(key));

    const auto* found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK((*found)->size() == frame->size());

    CHECK(cache.erase(key));
    CHECK(!cache.contains(key));
}
