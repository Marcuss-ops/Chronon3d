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

// ── Hot-path benchmarks (N14) ───────────────────────────────────────────

TEST_CASE("Benchmark: blur performance (radius 10, 50, 100)") {
    constexpr int w = 640, h = 360;
    Framebuffer src(w, h), dst(w, h);
    src.clear(Color{0.4f, 0.6f, 0.8f, 1.0f});

    auto run_blur = [&](int radius, int iter) -> double {
        uint64_t total_us = 0;
        for (int i = 0; i < iter; ++i) {
            auto t0 = now_us();
            // Use a simple 1D horizontal blur to measure throughput
            for (int y = 0; y < h; ++y) {
                Color* src_row = src.pixels_row(y);
                Color* dst_row = dst.pixels_row(y);
                for (int x = 0; x < w; ++x) {
                    f32 r = 0, g = 0, b = 0, a = 0, weight = 0;
                    int x0 = std::max(0, x - radius);
                    int x1 = std::min(w, x + radius + 1);
                    for (int kx = x0; kx < x1; ++kx) {
                        const f32 k = 1.0f - std::abs(static_cast<f32>(kx - x)) / static_cast<f32>(radius + 1);
                        r += src_row[kx].r * k;
                        g += src_row[kx].g * k;
                        b += src_row[kx].b * k;
                        a += src_row[kx].a * k;
                        weight += k;
                    }
                    if (weight > 0) {
                        dst_row[x] = {r / weight, g / weight, b / weight, a / weight};
                    }
                }
            }
            auto t1 = now_us();
            total_us += (t1 - t0);
        }
        return static_cast<double>(total_us) / (iter * 1000.0);
    };

    double ms_r10 = run_blur(10, 5);
    double ms_r50 = run_blur(50, 3);
    double ms_r100 = run_blur(100, 2);

    MESSAGE("blur_radius_10: avg=" << ms_r10 << "ms (5 iters, 640x360)");
    MESSAGE("blur_radius_50: avg=" << ms_r50 << "ms (3 iters, 640x360)");
    MESSAGE("blur_radius_100: avg=" << ms_r100 << "ms (2 iters, 640x360)");
    CHECK(ms_r10 > 0.0);
    CHECK(ms_r50 > 0.0);
    CHECK(ms_r100 > 0.0);
}

TEST_CASE("Benchmark: compositing blend modes") {
    constexpr int w = 640, h = 360;
    Framebuffer src(w, h), dst(w, h);
    src.clear(Color{0.8f, 0.2f, 0.3f, 0.8f});
    dst.clear(Color{0.2f, 0.6f, 0.4f, 1.0f});

    auto run_blend = [&](int iter) -> double {
        uint64_t total_us = 0;
        for (int i = 0; i < iter; ++i) {
            auto t0 = now_us();
            for (int y = 0; y < h; ++y) {
                Color* src_row = src.pixels_row(y);
                Color* dst_row = dst.pixels_row(y);
                for (int x = 0; x < w; ++x) {
                    const Color s = src_row[x];
                    const Color d = dst_row[x];
                    // Normal blend: s*a + d*(1-a)
                    const f32 a = s.a;
                    dst_row[x] = Color{
                        s.r * a + d.r * (1.0f - a),
                        s.g * a + d.g * (1.0f - a),
                        s.b * a + d.b * (1.0f - a),
                        a + d.a * (1.0f - a)
                    };
                }
            }
            auto t1 = now_us();
            total_us += (t1 - t0);
        }
        return static_cast<double>(total_us) / (iter * 1000.0);
    };

    double ms = run_blend(10);
    MESSAGE("compositing_normal_blend: avg=" << ms << "ms (10 iters, 640x360)");
    CHECK(ms > 0.0);
}

TEST_CASE("Benchmark: motion blur accumulation (simulated)") {
    constexpr int w = 320, h = 180;
    constexpr int kSamples[] = {4, 8, 16};

    for (int samples : kSamples) {
        uint64_t total_us = 0;
        constexpr int kIter = 5;

        for (int i = 0; i < kIter; ++i) {
            std::vector<Framebuffer> subframes;
            for (int s = 0; s < samples; ++s) {
                subframes.emplace_back(w, h);
                Color tint{
                    0.5f + 0.3f * std::sin(static_cast<f32>(s) * 0.5f),
                    0.3f + 0.4f * std::cos(static_cast<f32>(s) * 0.3f),
                    0.7f, 0.8f
                };
                subframes.back().clear(tint);
            }

            auto t0 = now_us();
            // Accumulate samples
            std::vector<f32> accum(w * h * 4, 0.0f);
            const f32 weight = 1.0f / static_cast<f32>(samples);
            for (int s = 0; s < samples; ++s) {
                for (int y = 0; y < h; ++y) {
                    Color* row = subframes[s].pixels_row(y);
                    for (int x = 0; x < w; ++x) {
                        const size_t idx = static_cast<size_t>(y) * w + x;
                        accum[idx * 4 + 0] += row[x].r * weight;
                        accum[idx * 4 + 1] += row[x].g * weight;
                        accum[idx * 4 + 2] += row[x].b * weight;
                        accum[idx * 4 + 3] += row[x].a * weight;
                    }
                }
            }
            auto t1 = now_us();
            total_us += (t1 - t0);
        }

        double avg_ms = static_cast<double>(total_us) / (kIter * 1000.0);
        MESSAGE("motion_blur_" << samples << "_samples: avg=" << avg_ms
                << "ms (" << kIter << " iters, " << w << "x" << h << ")");
        CHECK(avg_ms > 0.0);
    }
}
