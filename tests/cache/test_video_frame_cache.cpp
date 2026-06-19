// =============================================================================
// test_video_frame_cache.cpp — Tests for LruCache-backed VideoFrameCache
// (Commit 2) + the VideoFrame encoding class.
//
// Covers the post-collapse behavior: bounded cardinality, LRU eviction, and
// shared_ptr return semantics from find().  The VideoFrame class itself is
// untouched (it owns raw bytes; no cache involvement).
// =============================================================================

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

TEST_CASE("VideoFrameCache default constructor uses Config-driven cap") {
    VideoFrameCache cache;
    CHECK(cache.size() == 0);
}

TEST_CASE("VideoFrameCache stores and finds frames by digest (shared_ptr return)") {
    // Cache operates in byte-weighted mode — use a byte-scale capacity.
    // 1920×1080 YUV420P ≈ 3.1 MiB per frame.
    constexpr size_t kCapBytes = 8ULL * 1024 * 1024;  // 8 MiB
    VideoFrameCache cache(/*max_capacity_bytes=*/kCapBytes, /*num_shards=*/1);
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

    auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found.get() == frame.get());    // same instance
    CHECK(found->size() == frame->size());

    CHECK(cache.erase(key));
    CHECK(!cache.contains(key));
    CHECK(cache.find(key) == nullptr);
}

TEST_CASE("VideoFrameCache count-mode LRU eviction at capacity") {
    // Cache operates in byte-weighted mode.  64×64 RGBA8 ≈ 16 KiB per frame.
    // Use a small byte budget that fits 3 frames but evicts on the 4th.
    constexpr size_t kCapFrames = 3;
    constexpr size_t kFrameBytes = 64 * 64 * 4;  // 16 KiB
    constexpr size_t kCapBytes = kCapFrames * kFrameBytes;
    VideoFrameCache cache(/*max_capacity_bytes=*/kCapBytes, /*num_shards=*/1);

    for (u64 i = 0; i < kCapFrames; ++i) {
        VideoFrameKey k{
            .composition_id = "Comp",
            .frame_index = i,
            .width = 64,
            .height = 64,
            .format = VideoPixelFormat::RGBA8,
            .scene_hash = 0,
            .render_hash = 0,
        };
        cache.store(k, std::make_shared<VideoFrame>(64, 64, VideoPixelFormat::RGBA8));
    }
    REQUIRE(cache.size() == kCapFrames);
    CHECK(cache.stats().evictions == 0);

    // 4th insert evicts frame_index 0 (LRU tail).
    VideoFrameKey evict_k{
        .composition_id = "Comp",
        .frame_index = static_cast<u64>(kCapFrames),
        .width = 64,
        .height = 64,
        .format = VideoPixelFormat::RGBA8,
        .scene_hash = 0,
        .render_hash = 0,
    };
    cache.store(evict_k, std::make_shared<VideoFrame>(64, 64, VideoPixelFormat::RGBA8));

    CHECK(cache.size() == kCapFrames);
    CHECK(cache.stats().evictions == 1);

    VideoFrameKey tail_k{
        .composition_id = "Comp",
        .frame_index = 0,
        .width = 64,
        .height = 64,
        .format = VideoPixelFormat::RGBA8,
        .scene_hash = 0,
        .render_hash = 0,
    };
    CHECK(!cache.contains(tail_k));
    CHECK(cache.contains(evict_k));
}
