#include <doctest/doctest.h>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/math/color.hpp>
#include <vector>
#include <cstdlib>
#include <xxhash.h>
using namespace chronon3d;

using namespace chronon3d::video;
using namespace chronon3d::cache;

TEST_CASE("Long export: 1000 varying frames via frame converter + cache") {
    constexpr int w = 64, h = 64;
    constexpr int kFrames = 1000;
    // 64x64 YUV420P ≈ 6 KiB per frame; keep a few frames resident.
    constexpr int kCacheSizeBytes = 64 * 1024;

    ConvertedFrameCache cache(kCacheSizeBytes);
    const size_t y_sz = w * h;
    const size_t uv_sz = (w / 2) * (h / 2);

    uint64_t prev_digest = 0;
    int cache_hits = 0;
    int cache_misses = 0;

    for (int i = 0; i < kFrames; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kFrames);
        auto fb = std::make_shared<Framebuffer>(w, h);
        fb->clear(Color{t, 1.0f - t, t * 0.5f, 1.0f});

        uint64_t digest = 0;
        for (int y = 0; y < h; ++y)
            digest ^= XXH64(fb->pixels_row(y), fb->stride() * sizeof(Color), y);

        ConvertedFrameCacheKey key{
            .framebuffer_digest = digest,
            .width = w,
            .height = h,
        .format = EncoderPixelFormat::YUV420P,
        .matrix = YuvMatrix::BT709,
        .range  = ColorRange::Limited,
        .apply_gamma = true,
    };

        const auto entry = cache.lookup(key);
        if (entry) {
            ++cache_hits;
        } else {
            ++cache_misses;
            std::vector<uint8_t> y(y_sz), u(uv_sz), v(uv_sz);
            auto res = convert_frame_tight(*fb,
                FramePlanes{.y = y.data(), .u = u.data(), .v = v.data()},
                w, h, EncoderPixelFormat::YUV420P,
                YuvMatrix::BT709, ColorRange::Limited, true);
            REQUIRE(res.success);

            std::vector<uint8_t> packed(y_sz + uv_sz * 2);
            std::memcpy(packed.data(), y.data(), y_sz);
            std::memcpy(packed.data() + y_sz, u.data(), uv_sz);
            std::memcpy(packed.data() + y_sz + uv_sz, v.data(), uv_sz);
            cache.insert(key, packed);
        }

        prev_digest = digest;
    }

    CHECK(cache_hits + cache_misses == kFrames);
    MESSAGE("Cache hits=" << cache_hits << " misses=" << cache_misses
            << " hit_rate=" << (100.0 * cache_hits / kFrames) << "%");
}

TEST_CASE("Long export: FramebufferPool stable after 1000 acquire/release cycles") {
    auto pool = std::make_shared<FramebufferPool>(512ULL * 1024ULL * 1024ULL);
    pool->reset_counters();

    constexpr int kCycles = 1000;
    constexpr int kWidth = 1920;
    constexpr int kHeight = 1080;

    for (int i = 0; i < kCycles; ++i) {
        auto fb = pool->acquire(kWidth, kHeight);
        fb->clear(Color{
            0.1f + static_cast<float>(i) * 0.0008f,
            0.3f,
            0.5f,
            1.0f
        });
    }

    auto stats = pool->stats();
    MESSAGE("Total allocations=" << stats.total_allocations
            << " reuses=" << stats.total_reuses
            << " hit_rate=" << (stats.hit_rate * 100.0) << "%"
            << " current_bytes=" << stats.current_bytes);

    CHECK(stats.total_allocations > 0);
    CHECK(stats.total_reuses >= stats.total_allocations * 10);
    CHECK(stats.current_bytes > 0);
}

TEST_CASE("Long export: FramebufferPool memory does not grow after warmup") {
    auto pool = std::make_shared<FramebufferPool>(2ULL * 1024ULL * 1024ULL * 1024ULL);
    pool->reset_counters();

    // Warmup: acquire and release all distinct sizes
    constexpr int kSizes = 5;
    constexpr int kIter = 100;
    const int sizes[kSizes][2] = {{1920,1080}, {1280,720}, {640,480}, {320,240}, {1920,1080}};

    for (int i = 0; i < kIter; ++i) {
        for (int s = 0; s < kSizes; ++s) {
            auto fb = pool->acquire(sizes[s][0], sizes[s][1]);
            fb->clear(Color{0.1f, 0.2f, 0.3f, 1.0f});
        }
    }

    auto stats = pool->stats();
    MESSAGE("Pool after " << kIter * kSizes << " cycles: allocs=" << stats.total_allocations
            << " reuses=" << stats.total_reuses
            << " bytes=" << stats.current_bytes
            << " hit_rate=" << (stats.hit_rate * 100.0) << "%");

    size_t prev_current_bytes = stats.current_bytes;
    CHECK(prev_current_bytes > 0);

    // Another batch with same sizes: no new allocations expected
    for (int i = 0; i < kIter; ++i) {
        for (int s = 0; s < kSizes; ++s) {
            auto fb = pool->acquire(sizes[s][0], sizes[s][1]);
        }
    }

    auto stats2 = pool->stats();
    MESSAGE("Second batch: allocs=" << stats2.total_allocations
            << " reuses=" << stats2.total_reuses
            << " bytes=" << stats2.current_bytes
            << " hit_rate=" << (stats2.hit_rate * 100.0) << "%");

    CHECK(stats2.current_bytes <= prev_current_bytes + 4096);
    CHECK(stats2.total_allocations == stats.total_allocations);
}

TEST_CASE("Long export: near-duplicate frames hit converted frame cache") {
    constexpr int w = 32, h = 32;
    // 32x32 NV12 ≈ 1.5 KiB per frame; keep a few frames resident.
    constexpr int kCacheSizeBytes = 8 * 1024;
    constexpr int kFrames = 100;

    ConvertedFrameCache cache(kCacheSizeBytes);
    const size_t y_sz = w * h;
    const size_t uv_sz = (w / 2) * (h / 2);

    int total_hits = 0;
    int total_misses = 0;

    for (int i = 0; i < kFrames; ++i) {
        auto fb = std::make_shared<Framebuffer>(w, h);
        fb->clear(Color{1.0f, 0.0f, 0.0f, 1.0f});

        uint64_t digest = 42;
        ConvertedFrameCacheKey key{
            .framebuffer_digest = digest,
            .width = w,
            .height = h,
        .format = EncoderPixelFormat::NV12,
        .matrix = YuvMatrix::BT709,
        .range  = ColorRange::Limited,
        .apply_gamma = true,
    };

        const auto entry = cache.lookup(key);
        if (entry) {
            ++total_hits;
        } else {
            ++total_misses;
            std::vector<uint8_t> y(y_sz), uv(y_sz / 2);
            auto res = convert_frame_tight(*fb,
                FramePlanes{.y = y.data(), .uv = uv.data()},
                w, h, EncoderPixelFormat::NV12,
                YuvMatrix::BT709, ColorRange::Limited, true);
            REQUIRE(res.success);
            std::vector<uint8_t> packed(y_sz + y_sz / 2);
            std::memcpy(packed.data(), y.data(), y_sz);
            std::memcpy(packed.data() + y_sz, uv.data(), y_sz / 2);
            cache.insert(key, packed);
        }
    }

    CHECK(total_hits == kFrames - 1);
    CHECK(total_misses == 1);
}
