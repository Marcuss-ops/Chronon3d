#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/trace.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/video/frame_converter.hpp>
#include <chronon3d/video/converted_frame_cache.hpp>
#include <chronon3d/math/color.hpp>
#include <chrono>
#include <vector>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::video;

static uint64_t now_us() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

TEST_CASE("Micro benchmark: framebuffer clear time (10 iterations)") {
    constexpr int w = 1920, h = 1080;
    Framebuffer fb(w, h);

    uint64_t total_us = 0;
    constexpr int kIter = 10;

    for (int i = 0; i < kIter; ++i) {
        auto t0 = now_us();
        fb.clear(Color{
            0.1f * static_cast<float>(i),
            0.5f,
            0.9f,
            1.0f
        });
        auto t1 = now_us();
        total_us += (t1 - t0);
    }

    double avg_ms = static_cast<double>(total_us) / (kIter * 1000.0);
    MESSAGE("clear_ms: avg=" << avg_ms << "ms over " << kIter << " iterations");
    CHECK(avg_ms > 0.0);
    CHECK(fb.get_pixel(0, 0).r == doctest::Approx(0.9f));
}

TEST_CASE("Micro benchmark: frame conversion time (YUV420P, 1920x1080)") {
    constexpr int w = 1920, h = 1080;
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{0.3f, 0.6f, 0.9f, 1.0f});

    const size_t y_sz = static_cast<size_t>(w) * h;
    const size_t uv_sz = static_cast<size_t>(w / 2) * (h / 2);
    std::vector<uint8_t> y(y_sz, 0), u(uv_sz, 0), v(uv_sz, 0);

    uint64_t total_ns = 0;
    constexpr int kIter = 5;

    for (int i = 0; i < kIter; ++i) {
        auto res = convert_frame_tight(*fb, y.data(), u.data(), v.data(), nullptr,
                                       w, h, EncoderPixelFormat::YUV420P, true);
        REQUIRE(res.success);
        total_ns += res.conversion_ns;
    }

    double avg_ms = static_cast<double>(total_ns) / (kIter * 1000000.0);
    MESSAGE("conversion_ms: avg=" << avg_ms << "ms per 1920x1080 YUV420P frame");
    CHECK(avg_ms > 0.0);
    CHECK(y[0] > 0);
    CHECK(u[0] > 0);
    CHECK(v[0] > 0);
}

TEST_CASE("Micro benchmark: frame conversion time (NV12, 1920x1080)") {
    constexpr int w = 1920, h = 1080;
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{0.3f, 0.6f, 0.9f, 1.0f});

    const size_t y_sz = static_cast<size_t>(w) * h;
    const size_t uv_sz = y_sz / 2;
    std::vector<uint8_t> y(y_sz, 0), uv(uv_sz, 0);

    uint64_t total_ns = 0;
    constexpr int kIter = 5;

    for (int i = 0; i < kIter; ++i) {
        auto res = convert_frame_tight(*fb, y.data(), nullptr, nullptr, uv.data(),
                                       w, h, EncoderPixelFormat::NV12, true);
        REQUIRE(res.success);
        total_ns += res.conversion_ns;
    }

    double avg_ms = static_cast<double>(total_ns) / (kIter * 1000000.0);
    MESSAGE("conversion_ms: avg=" << avg_ms << "ms per 1920x1080 NV12 frame");
    CHECK(avg_ms > 0.0);
    CHECK(y[0] > 0);
}

TEST_CASE("Micro benchmark: converted frame cache hit rate") {
    constexpr int w = 32, h = 32;
    ConvertedFrameCache cache(64);
    const size_t y_sz = w * h;
    const size_t uv_sz = (w / 2) * (h / 2);

    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{1.0f, 0.0f, 0.0f, 1.0f});

    ConvertedFrameCacheKey key{
        .framebuffer_digest = 1,
        .width = w, .height = h,
        .format = EncoderPixelFormat::YUV420P,
        .color_matrix = 0, .apply_gamma = true,
    };

    std::vector<uint8_t> y(y_sz), u(uv_sz), v(uv_sz);
    auto r = convert_frame_tight(*fb, y.data(), u.data(), v.data(), nullptr,
                                 w, h, EncoderPixelFormat::YUV420P, true);
    REQUIRE(r.success);
    cache.insert(key, y.data(), y_sz + uv_sz * 2);

    CHECK(cache.lookup(key) != nullptr);
    CHECK(cache.lookup(key) != nullptr);
    CHECK(cache.lookup(key) != nullptr);

    uint64_t hits = cache.hits();
    uint64_t misses = cache.misses();
    double hit_rate = (hits + misses) > 0
        ? static_cast<double>(hits) / static_cast<double>(hits + misses)
        : 0.0;

    MESSAGE("frame_cache: hits=" << hits << " misses=" << misses
            << " hit_rate=" << (hit_rate * 100.0) << "%");
    CHECK(hits > 0);
    CHECK(misses == 0);
    CHECK(hit_rate > 0.99);
}
