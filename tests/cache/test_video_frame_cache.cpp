// =============================================================================
// test_video_frame_cache.cpp — Tests for LruCache-backed VideoFrameCache
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/video_frame_cache.hpp>
#include <chronon3d/runtime/frame_format.hpp>

using namespace chronon3d;
using namespace chronon3d::cache;

namespace {

constexpr auto kRgba8 = runtime::make_frame_format(runtime::PixelFormat::Rgba8Unorm);
constexpr auto kYuv420P = runtime::make_frame_format(runtime::PixelFormat::Yuv420P);

} // namespace

TEST_CASE("canonical render format is RGBA16F linear premultiplied") {
    const auto format = runtime::canonical_render_format();
    CHECK(format.pixel == runtime::PixelFormat::Rgba16Float);
    CHECK(format.transfer == runtime::TransferFunction::Linear);
    CHECK(format.matrix == runtime::ColorMatrix::Identity);
    CHECK(format.range == runtime::ColorRange::Full);
    CHECK(format.alpha == runtime::AlphaMode::Premultiplied);
}

TEST_CASE("VideoFrame allocates expected buffer sizes from canonical format") {
    VideoFrame rgba(10, 10, kRgba8);
    CHECK(rgba.size() == 400);
    CHECK(rgba.expected_size() == 400);

    VideoFrame yuv(10, 10, kYuv420P);
    CHECK(yuv.size() == 150);
    CHECK(yuv.expected_size() == 150);
}

TEST_CASE("VideoFrameCache default constructor uses Config-driven cap") {
    VideoFrameCache cache;
    CHECK(cache.size() == 0);
}

TEST_CASE("VideoFrameCache stores and finds frames by canonical format") {
    constexpr size_t kCapBytes = 8ULL * 1024 * 1024;
    VideoFrameCache cache(/*max_capacity_bytes=*/kCapBytes, /*num_shards=*/1);
    auto frame = std::make_shared<VideoFrame>(1920, 1080, kYuv420P);

    VideoFrameKey key{
        .composition_id = "GridCleanBackground",
        .frame_index = 0,
        .width = 1920,
        .height = 1080,
        .format = kYuv420P,
        .scene_hash = 123,
        .render_hash = 456,
    };

    CHECK(!cache.contains(key));
    cache.store(key, frame);
    CHECK(cache.contains(key));

    auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found.get() == frame.get());
    CHECK(found->size() == frame->size());
    CHECK(found->format() == kYuv420P);

    CHECK(cache.erase(key));
    CHECK(!cache.contains(key));
    CHECK(cache.find(key) == nullptr);
}

TEST_CASE("VideoFrameCache format metadata participates in key identity") {
    auto limited = kYuv420P;
    auto full = limited;
    full.range = runtime::ColorRange::Full;

    VideoFrameKey limited_key{
        .composition_id = "Comp",
        .frame_index = 7,
        .width = 64,
        .height = 64,
        .format = limited,
    };
    auto full_key = limited_key;
    full_key.format = full;

    CHECK(limited_key != full_key);
    CHECK(limited_key.digest() != full_key.digest());
}

TEST_CASE("VideoFrameCache byte-weighted LRU eviction at capacity") {
    constexpr size_t kCapFrames = 3;
    constexpr size_t kFrameBytes = 64 * 64 * 4;
    constexpr size_t kCapBytes = kCapFrames * kFrameBytes;
    VideoFrameCache cache(/*max_capacity_bytes=*/kCapBytes, /*num_shards=*/1);

    for (u64 i = 0; i < kCapFrames; ++i) {
        VideoFrameKey k{
            .composition_id = "Comp",
            .frame_index = i,
            .width = 64,
            .height = 64,
            .format = kRgba8,
            .scene_hash = 0,
            .render_hash = 0,
        };
        cache.store(k, std::make_shared<VideoFrame>(64, 64, kRgba8));
    }
    REQUIRE(cache.size() == kCapFrames);
    CHECK(cache.stats().evictions == 0);

    VideoFrameKey evict_k{
        .composition_id = "Comp",
        .frame_index = static_cast<u64>(kCapFrames),
        .width = 64,
        .height = 64,
        .format = kRgba8,
        .scene_hash = 0,
        .render_hash = 0,
    };
    cache.store(evict_k, std::make_shared<VideoFrame>(64, 64, kRgba8));

    CHECK(cache.size() == kCapFrames);
    CHECK(cache.stats().evictions == 1);

    VideoFrameKey tail_k{
        .composition_id = "Comp",
        .frame_index = 0,
        .width = 64,
        .height = 64,
        .format = kRgba8,
        .scene_hash = 0,
        .render_hash = 0,
    };
    CHECK(!cache.contains(tail_k));
    CHECK(cache.contains(evict_k));
}
