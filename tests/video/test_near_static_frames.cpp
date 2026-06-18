#include <doctest/doctest.h>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <memory>
#include <vector>
#include <cmath>
#include <xxhash.h>
using namespace chronon3d;

using namespace chronon3d::video;

TEST_CASE("Near-static frames: small color variations produce mostly cache hits") {
    constexpr int w = 64, h = 64;
    constexpr int kFrames = 50;
    constexpr int kChangeInterval = 10;

    ConvertedFrameCache cache(16);
    const size_t y_sz = static_cast<size_t>(w) * h;
    const size_t uv_sz = static_cast<size_t>(w / 2) * (h / 2);

    int cache_hits = 0;
    int cache_misses = 0;

    for (int i = 0; i < kFrames; ++i) {
        auto fb = std::make_shared<Framebuffer>(w, h);

        if (i % kChangeInterval == 0) {
            float shade = 0.5f + 0.5f * (static_cast<float>(i) / kFrames);
            fb->clear(Color{shade, shade * 0.5f, shade * 0.3f, 1.0f});
        } else {
            fb->clear(Color{1.0f, 0.0f, 0.0f, 1.0f});
        }

        uint64_t digest = XXH64(fb->pixels_row(0), fb->size_bytes(), 0);

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
            cache.insert(key, packed.data(), packed.size());
        }
    }

    MESSAGE("Near-static: hits=" << cache_hits << " misses=" << cache_misses
            << " (expect ~5-6 misses for 5 unique frames + LRU eviction)");
    CHECK(cache_hits > cache_misses);
    CHECK(cache_misses >= kFrames / kChangeInterval);
    CHECK(cache_misses <= kFrames / kChangeInterval + 2);
}

TEST_CASE("Near-static frames: single repeated frame hits cache 100%") {
    constexpr int w = 32, h = 32;
    constexpr int kFrames = 100;

    ConvertedFrameCache cache(4);
    const size_t y_sz = static_cast<size_t>(w) * h;
    const size_t uv_sz = static_cast<size_t>(w / 2) * (h / 2);

    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{0.5f, 0.3f, 0.7f, 1.0f});

    uint64_t digest = XXH64(fb->pixels_row(0), fb->size_bytes(), 0);
    ConvertedFrameCacheKey key{
        .framebuffer_digest = digest,
        .width = w,
        .height = h,
        .format = EncoderPixelFormat::YUV420P,
        .color_matrix = 0,
        .apply_gamma = true,
    };

    int hits = 0, misses = 0;
    for (int i = 0; i < kFrames; ++i) {
        const auto entry = cache.lookup(key);
        if (entry) {
            ++hits;
        } else {
            ++misses;
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
            cache.insert(key, packed.data(), packed.size());
        }
    }

    CHECK(misses == 1);
    CHECK(hits == kFrames - 1);
    CHECK(cache.hits() == kFrames - 1);
}

TEST_CASE("Near-static frames: format change between YUV420P and NV12 causes miss") {
    constexpr int w = 16, h = 16;
    ConvertedFrameCache cache(4);
    const size_t y_sz = static_cast<size_t>(w) * h;
    const size_t uv_sz_yuv = static_cast<size_t>(w / 2) * (h / 2);
    const size_t uv_sz_nv = y_sz / 2;

    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{0.5f, 0.5f, 0.5f, 1.0f});

    uint64_t digest = XXH64(fb->pixels_row(0), fb->size_bytes(), 0);

    ConvertedFrameCacheKey key_yuv{
        .framebuffer_digest = digest,
        .width = w, .height = h,
        .format = EncoderPixelFormat::YUV420P,
        .matrix = YuvMatrix::BT709, .range = ColorRange::Limited, .apply_gamma = true,
    };
    ConvertedFrameCacheKey key_nv{
        .framebuffer_digest = digest,
        .width = w, .height = h,            .format = EncoderPixelFormat::NV12,
            .matrix = YuvMatrix::BT709,
            .range  = ColorRange::Limited,
            .apply_gamma = true,
        };

    std::vector<uint8_t> y_yuv(y_sz), u(uv_sz_yuv), v(uv_sz_yuv);
    auto r1 = convert_frame_tight(*fb,
        FramePlanes{.y = y_yuv.data(), .u = u.data(), .v = v.data()},
        w, h, EncoderPixelFormat::YUV420P,
        YuvMatrix::BT709, ColorRange::Limited, true);
    REQUIRE(r1.success);

    std::vector<uint8_t> packed_yuv(y_sz + uv_sz_yuv * 2);
    std::memcpy(packed_yuv.data(), y_yuv.data(), y_sz);
    std::memcpy(packed_yuv.data() + y_sz, u.data(), uv_sz_yuv);
    std::memcpy(packed_yuv.data() + y_sz + uv_sz_yuv, v.data(), uv_sz_yuv);
    cache.insert(key_yuv, packed_yuv.data(), packed_yuv.size());

    std::vector<uint8_t> y_nv(y_sz), uv(uv_sz_nv);
    auto r2 = convert_frame_tight(*fb,
        FramePlanes{.y = y_nv.data(), .uv = uv.data()},
        w, h, EncoderPixelFormat::NV12,
        YuvMatrix::BT709, ColorRange::Limited, true);
    REQUIRE(r2.success);

    std::vector<uint8_t> packed_nv(y_sz + uv_sz_nv);
    std::memcpy(packed_nv.data(), y_nv.data(), y_sz);
    std::memcpy(packed_nv.data() + y_sz, uv.data(), uv_sz_nv);
    cache.insert(key_nv, packed_nv.data(), packed_nv.size());

    CHECK(cache.lookup(key_yuv) != nullptr);
    CHECK(cache.lookup(key_nv) != nullptr);
}
