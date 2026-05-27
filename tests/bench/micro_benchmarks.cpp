#include <benchmark/benchmark.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/video/converted_frame_cache.hpp>
#include <chronon3d/video/frame_converter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::video;

namespace {

void BM_FramebufferClear(benchmark::State& state) {
    constexpr int w = 1920;
    constexpr int h = 1080;
    Framebuffer fb(w, h);

    for (auto _ : state) {
        fb.clear(Color{0.1f, 0.5f, 0.9f, 1.0f});
        benchmark::DoNotOptimize(fb.get_pixel(0, 0));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

void BM_FrameConversionYUV420P(benchmark::State& state) {
    constexpr int w = 1920;
    constexpr int h = 1080;
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{0.3f, 0.6f, 0.9f, 1.0f});

    const size_t y_sz = static_cast<size_t>(w) * h;
    const size_t uv_sz = static_cast<size_t>(w / 2) * (h / 2);
    std::vector<uint8_t> y(y_sz, 0), u(uv_sz, 0), v(uv_sz, 0);

    for (auto _ : state) {
        auto res = convert_frame_tight(*fb, y.data(), u.data(), v.data(), nullptr,
                                       w, h, EncoderPixelFormat::YUV420P, true);
        if (!res.success) {
            state.SkipWithError("YUV420P conversion failed");
            return;
        }
        benchmark::DoNotOptimize(res.conversion_ns);
        benchmark::DoNotOptimize(y.data());
        benchmark::DoNotOptimize(u.data());
        benchmark::DoNotOptimize(v.data());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

void BM_FrameConversionNV12(benchmark::State& state) {
    constexpr int w = 1920;
    constexpr int h = 1080;
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{0.3f, 0.6f, 0.9f, 1.0f});

    const size_t y_sz = static_cast<size_t>(w) * h;
    const size_t uv_sz = y_sz / 2;
    std::vector<uint8_t> y(y_sz, 0), uv(uv_sz, 0);

    for (auto _ : state) {
        auto res = convert_frame_tight(*fb, y.data(), nullptr, nullptr, uv.data(),
                                       w, h, EncoderPixelFormat::NV12, true);
        if (!res.success) {
            state.SkipWithError("NV12 conversion failed");
            return;
        }
        benchmark::DoNotOptimize(res.conversion_ns);
        benchmark::DoNotOptimize(y.data());
        benchmark::DoNotOptimize(uv.data());
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

void BM_ConvertedFrameCacheHit(benchmark::State& state) {
    constexpr int w = 32;
    constexpr int h = 32;
    ConvertedFrameCache cache(64);
    const size_t y_sz = w * h;
    const size_t uv_sz = (w / 2) * (h / 2);

    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color{1.0f, 0.0f, 0.0f, 1.0f});

    ConvertedFrameCacheKey key{
        .framebuffer_digest = 1,
        .width = w,
        .height = h,
        .format = EncoderPixelFormat::YUV420P,
        .color_matrix = 0,
        .apply_gamma = true,
    };

    std::vector<uint8_t> y(y_sz), u(uv_sz), v(uv_sz);
    auto result = convert_frame_tight(*fb, y.data(), u.data(), v.data(), nullptr,
                                      w, h, EncoderPixelFormat::YUV420P, true);
    if (!result.success) {
        state.SkipWithError("cache warmup conversion failed");
        return;
    }
    cache.insert(key, y.data(), y_sz + uv_sz * 2);

    for (auto _ : state) {
        benchmark::DoNotOptimize(cache.lookup(key));
        benchmark::DoNotOptimize(cache.lookup(key));
        benchmark::DoNotOptimize(cache.lookup(key));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 3);
    state.counters["hit_rate"] = 1.0;
}

void BM_BlurPerformance(benchmark::State& state) {
    const int radius = static_cast<int>(state.range(0));
    constexpr int w = 640;
    constexpr int h = 360;
    Framebuffer src(w, h), dst(w, h);
    src.clear(Color{0.4f, 0.6f, 0.8f, 1.0f});

    for (auto _ : state) {
        for (int y = 0; y < h; ++y) {
            Color* src_row = src.pixels_row(y);
            Color* dst_row = dst.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                f32 r = 0.0f;
                f32 g = 0.0f;
                f32 b = 0.0f;
                f32 a = 0.0f;
                f32 weight = 0.0f;
                const int x0 = std::max(0, x - radius);
                const int x1 = std::min(w, x + radius + 1);
                for (int kx = x0; kx < x1; ++kx) {
                    const f32 k = 1.0f - std::abs(static_cast<f32>(kx - x)) / static_cast<f32>(radius + 1);
                    r += src_row[kx].r * k;
                    g += src_row[kx].g * k;
                    b += src_row[kx].b * k;
                    a += src_row[kx].a * k;
                    weight += k;
                }
                if (weight > 0.0f) {
                    dst_row[x] = {r / weight, g / weight, b / weight, a / weight};
                }
            }
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

void BM_CompositingBlendModes(benchmark::State& state) {
    constexpr int w = 640;
    constexpr int h = 360;
    Framebuffer src(w, h), dst(w, h);
    src.clear(Color{0.8f, 0.2f, 0.3f, 0.8f});
    dst.clear(Color{0.2f, 0.6f, 0.4f, 1.0f});

    for (auto _ : state) {
        for (int y = 0; y < h; ++y) {
            Color* src_row = src.pixels_row(y);
            Color* dst_row = dst.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                const Color s = src_row[x];
                const Color d = dst_row[x];
                const f32 a = s.a;
                dst_row[x] = Color{
                    s.r * a + d.r * (1.0f - a),
                    s.g * a + d.g * (1.0f - a),
                    s.b * a + d.b * (1.0f - a),
                    a + d.a * (1.0f - a)
                };
            }
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

void BM_MotionBlurAccumulation(benchmark::State& state) {
    const int samples = static_cast<int>(state.range(0));
    constexpr int w = 320;
    constexpr int h = 180;

    for (auto _ : state) {
        std::vector<Framebuffer> subframes;
        subframes.reserve(static_cast<size_t>(samples));
        for (int s = 0; s < samples; ++s) {
            subframes.emplace_back(w, h);
            Color tint{
                0.5f + 0.3f * std::sin(static_cast<f32>(s) * 0.5f),
                0.5f + 0.2f * std::cos(static_cast<f32>(s) * 0.25f),
                0.7f,
                1.0f
            };
            subframes.back().clear(tint);
        }

        Framebuffer accum(w, h);
        accum.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
        for (const auto& fb : subframes) {
            for (int y = 0; y < h; ++y) {
                const Color* src_row = fb.pixels_row(y);
                Color* dst_row = accum.pixels_row(y);
                for (int x = 0; x < w; ++x) {
                    dst_row[x] = dst_row[x] + src_row[x] * (1.0f / static_cast<f32>(samples));
                }
            }
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * samples * w * h);
}

} // namespace

BENCHMARK(BM_FramebufferClear)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FrameConversionYUV420P)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FrameConversionNV12)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ConvertedFrameCacheHit)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BlurPerformance)->Arg(10)->Arg(50)->Arg(100)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_CompositingBlendModes)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_MotionBlurAccumulation)->Arg(4)->Arg(8)->Arg(16)->Unit(benchmark::kMillisecond);
