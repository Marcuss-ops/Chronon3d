#include <benchmark/benchmark.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>

// Include the real blur implementation for proper benchmarks
#include "src/backends/software/utils/render_effects_processor.hpp"

// Tile benchmark includes
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::video;
using namespace chronon3d::renderer;

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

// ---------------------------------------------------------------------------
// Real blur benchmarks that call the actual apply_blur() from effect_blur.cpp
// ---------------------------------------------------------------------------

/// Benchmark the real box-filter blur with the default 3-pass mode
void BM_Blur3Pass(benchmark::State& state) {
    const int radius = static_cast<int>(state.range(0));
    constexpr int w = 640;
    constexpr int h = 360;
    Framebuffer fb(w, h);
    fb.clear(Color{0.4f, 0.6f, 0.8f, 1.0f});

    for (auto _ : state) {
        apply_blur(fb, static_cast<f32>(radius), std::nullopt, 3);
        benchmark::DoNotOptimize(fb.get_pixel(0, 0));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

/// Benchmark the box-filter blur with the optimized 2-pass mode
/// (equivalent sigma, ~1.5× less work)
void BM_Blur2Pass(benchmark::State& state) {
    const int radius = static_cast<int>(state.range(0));
    constexpr int w = 640;
    constexpr int h = 360;
    Framebuffer fb(w, h);
    fb.clear(Color{0.4f, 0.6f, 0.8f, 1.0f});

    for (auto _ : state) {
        apply_blur(fb, static_cast<f32>(radius), std::nullopt, 2);
        benchmark::DoNotOptimize(fb.get_pixel(0, 0));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

// ---------------------------------------------------------------------------
// Tile execution benchmarks: sequential vs parallel vs no-tiles baseline
// ---------------------------------------------------------------------------

/// Scene with many moving objects to generate lots of dirty tiles per frame
static Composition make_tile_bench_scene(int width, int height, int duration) {
    return Composition(CompositionSpec{
        .name = "TileBench", .width = width, .height = height, .duration = duration
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Background fills the whole frame
        s.rect("bg", {
            .size = {static_cast<float>(width), static_cast<float>(height)},
            .color = Color{0.05f, 0.1f, 0.15f, 1.0f},
            .pos = {static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, 0}
        });
        // 16 moving circles — each on a different trajectory, creating
        // scattered dirty tiles across the frame every frame.
        for (int i = 0; i < 16; ++i) {
            float phase = static_cast<float>(i) * 0.4f;
            float x = 30.0f + static_cast<float>(ctx.frame) * 2.5f
                      + static_cast<float>(i % 4) * 80.0f;
            float y = 30.0f
                      + std::sin(phase + static_cast<float>(ctx.frame) * 0.1f) * 50.0f
                      + static_cast<float>(i / 4) * 60.0f;
            s.circle("ball" + std::to_string(i), {
                .radius = 6.0f,
                .color = Color{0.3f + 0.05f * static_cast<float>(i), 0.5f, 0.7f, 0.9f},
                .pos = {x, y, 0}
            });
        }
        return s.build();
    });
}

/// Scene with parameterised element count for dirty-ratio sweep.
/// Background + num_elements animated circles on scattered trajectories.
/// Varying num_elements produces a predictable, monotonic increase in dirty ratio.
static Composition make_dirty_ratio_sweep_scene(int width, int height, int duration, int num_elements) {
    const int clamped = std::max(1, std::min(num_elements, 256));
    return Composition(CompositionSpec{
        .name = "DirtySweep", .width = width, .height = height, .duration = duration
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Static background fills the whole frame
        s.rect("bg", {
            .size = {static_cast<float>(width), static_cast<float>(height)},
            .color = Color{0.05f, 0.1f, 0.15f, 1.0f},
            .pos = {static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, 0}
        });
        const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(clamped))));
        const int rows = (clamped + cols - 1) / cols;
        const float cell_w = static_cast<float>(width) / static_cast<float>(cols);
        const float cell_h = static_cast<float>(height) / static_cast<float>(rows);
        for (int i = 0; i < clamped; ++i) {
            const int col = i % cols;
            const int row = i / cols;
            const float cx = cell_w * (static_cast<float>(col) + 0.5f);
            const float cy = cell_h * (static_cast<float>(row) + 0.5f);
            const float phase = static_cast<float>(i) * 0.7f;
            const float t = static_cast<float>(ctx.frame) * 0.15f;
            const float x = cx + std::sin(phase + t * 1.3f) * cell_w * 0.35f;
            const float y = cy + std::cos(phase + t * 0.9f) * cell_h * 0.35f;
            s.circle("dot" + std::to_string(i), {
                .radius = 8.0f,
                .color = Color{0.4f, 0.6f, 0.8f, 1.0f},
                .pos = {x, y, 0}
            });
        }
        return s.build();
    });
}

/// Baseline: no tiles, full-frame render each frame
void BM_TileRenderNoTiles(benchmark::State& state) {
    constexpr int W = 640, H = 360;
    constexpr int kFrames = 12;
    Composition comp = make_tile_bench_scene(W, H, kFrames);

    for (auto _ : state) {
        SoftwareRenderer renderer;
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = false;
        renderer.set_settings(s);

        for (Frame f = 0; f < kFrames; ++f) {
            auto fb = renderer.render_frame(comp, f);
            benchmark::DoNotOptimize(fb);
        }
    }

    state.SetItemsProcessed(
        static_cast<int64_t>(state.iterations()) * kFrames * W * H);
}

/// Sequential tile processing: dirty tiles rendered one by one
void BM_TileRenderSequential(benchmark::State& state) {
    const int tile_size = static_cast<int>(state.range(0));
    constexpr int W = 640, H = 360;
    constexpr int kFrames = 12;
    Composition comp = make_tile_bench_scene(W, H, kFrames);

    for (auto _ : state) {
        SoftwareRenderer renderer;
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = tile_size;
        s.dirty.parallel_tiles = false;   // ← sequential
        renderer.set_settings(s);

        for (Frame f = 0; f < kFrames; ++f) {
            auto fb = renderer.render_frame(comp, f);
            benchmark::DoNotOptimize(fb);
        }
    }

    state.SetItemsProcessed(
        static_cast<int64_t>(state.iterations()) * kFrames * W * H);
}

/// Parallel tile processing (V1): dirty tiles rendered via tbb::parallel_for
void BM_TileRenderParallel(benchmark::State& state) {
    const int tile_size = static_cast<int>(state.range(0));
    constexpr int W = 640, H = 360;
    constexpr int kFrames = 12;
    Composition comp = make_tile_bench_scene(W, H, kFrames);

    for (auto _ : state) {
        SoftwareRenderer renderer;
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_size = tile_size;
        s.dirty.parallel_tiles = true;    // ← parallel (V1)
        renderer.set_settings(s);

        for (Frame f = 0; f < kFrames; ++f) {
            auto fb = renderer.render_frame(comp, f);
            benchmark::DoNotOptimize(fb);
        }
    }

    state.SetItemsProcessed(
        static_cast<int64_t>(state.iterations()) * kFrames * W * H);
}

// ---------------------------------------------------------------------------
// Dirty-ratio sweep: compare tile vs single-pass as dirty_ratio varies
// ---------------------------------------------------------------------------

/// Render a scene with `num_elements` animated objects and report both
/// tile-mode execution time and single-pass execution time.
/// Use BENCHMARK_CAPTURE to build separate tile/single curves:
///   BM_TileDirtyRatioSweep/Single/4    → single-pass, 4 elements
///   BM_TileDirtyRatioSweep/Tiled/4     → tiles (64), 4 elements
///
/// Custom counters:
///   dirty_ratio  — the actual dirty area fraction from the last frame
///   elements     — the Range() parameter (number of animated objects)
///   tile_ms      — per-frame time when tiles are enabled (Tiled only)
///   single_ms    — per-frame time when tiles are disabled (Single only)
static void BM_TileDirtyRatioSweep(benchmark::State& state, bool use_tiles) {
    const int num_elements = static_cast<int>(state.range(0));
    constexpr int W = 640, H = 360;
    constexpr int kFrames = 12;
    constexpr int tile_size = 64;

    Composition comp = make_dirty_ratio_sweep_scene(W, H, kFrames, num_elements);

    // Track the actual dirty ratio and per-frame time
    double last_dirty_ratio = 0.0;
    double per_frame_ms = 0.0;

    for (auto _ : state) {
        SoftwareRenderer renderer;
        RenderSettings s;
        s.use_modular_graph = true;
        s.dirty.enabled = true;
        s.dirty.use_bitmask = true;
        s.dirty.tile_dirty_ratio_threshold = 1.0;  // never auto-bypass — we want raw tile cost
        if (use_tiles) {
            s.dirty.tile_size = tile_size;
            s.dirty.parallel_tiles = false;  // sequential for apples-to-apples comparison
        } else {
            s.dirty.tile_size = 0;                  // single-pass
        }
        renderer.set_settings(s);

        auto t0 = profiling::now();
        for (Frame f = 0; f < kFrames; ++f) {
            auto fb = renderer.render_frame(comp, f);
            benchmark::DoNotOptimize(fb);
        }
        auto t1 = profiling::now();

        last_dirty_ratio = renderer.last_dirty_area_ratio();
        per_frame_ms = profiling::duration_ms(t0, t1)
                       / static_cast<double>(kFrames);
    }

    const std::string ratio_name = use_tiles ? "tile_ms" : "single_ms";
    state.counters[ratio_name] = per_frame_ms;
    state.counters["dirty_ratio"] = last_dirty_ratio;
    state.counters["elements"] = static_cast<double>(num_elements);
    state.SetItemsProcessed(
        static_cast<int64_t>(state.iterations()) * kFrames * W * H);
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

// ---------------------------------------------------------------------------
// RenderCounters throughput benchmark
//
// Compares the two counter-update strategies on the same workload:
//
//   Path A — atomic fetch_add on a shared RenderCounters (the "old" way)
//            Every increment is a cache-line bounce between cores.  On a
//            4+ core machine with 155 counters being touched, this is
//            the dominant cost in tight parallel regions.
//
//   Path B — plain ++ on thread_local_counters() + 1 merge_tls per
//            thread (the "new" way).  No atomic op, no cache-line
//            bouncing during the inner loop.  The single merge_tls at
//            the end moves 155 fields with one fetch_add each — cost
//            is O(num_counters), paid once per thread per frame.
//
// Both paths compute the same final value (sum of all increments).
// The custom counter "speedup_x" reports Path A / Path B.  Expected
// range on a 4-core machine: 2-5x.  On 8+ cores with contention: 5-10x.
// ---------------------------------------------------------------------------

namespace counters_bench {

constexpr int kCountersPerIter = 8;  // subset of the 155 fields — hot subset
constexpr int kItersPerThread  = 50'000;

void touch_atomic(RenderCounters& c) {
    for (int i = 0; i < kItersPerThread; ++i) {
        c.pixels_touched.fetch_add(1, std::memory_order_relaxed);
        c.cache_hits.fetch_add(1, std::memory_order_relaxed);
        c.cache_misses.fetch_add(1, std::memory_order_relaxed);
        c.nodes_executed.fetch_add(1, std::memory_order_relaxed);
        c.layers_rendered.fetch_add(1, std::memory_order_relaxed);
        c.composite_calls.fetch_add(1, std::memory_order_relaxed);
        c.composite_pixels.fetch_add(1, std::memory_order_relaxed);
        c.tile_pixels_rendered.fetch_add(1, std::memory_order_relaxed);
    }
}

void touch_tls(RenderCounters& global) {
    auto& tls = thread_local_counters();
    tls.reset();
    for (int i = 0; i < kItersPerThread; ++i) {
        tls.pixels_touched       += 1;
        tls.cache_hits           += 1;
        tls.cache_misses         += 1;
        tls.nodes_executed       += 1;
        tls.layers_rendered      += 1;
        tls.composite_calls      += 1;
        tls.composite_pixels     += 1;
        tls.tile_pixels_rendered += 1;
    }
    global.merge_tls(tls);
}

} // namespace counters_bench

void BM_CountersAtomicShared(benchmark::State& state) {
    RenderCounters shared;
    for (auto _ : state) {
        // Each benchmark iteration uses a fresh counters to avoid
        // overflow-related timing noise.
        shared.reset();
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(state.threads));
        for (int t = 0; t < state.threads; ++t) {
            workers.emplace_back([&] { counters_bench::touch_atomic(shared); });
        }
        for (auto& th : workers) th.join();
        benchmark::DoNotOptimize(shared.pixels_touched.load());
    }
    const auto total_ops = static_cast<int64_t>(state.iterations())
                           * state.threads
                           * counters_bench::kItersPerThread
                           * counters_bench::kCountersPerIter;
    state.SetItemsProcessed(total_ops);
    state.SetComplexityN(state.threads);
}

void BM_CountersThreadLocalMerge(benchmark::State& state) {
    RenderCounters merged;
    for (auto _ : state) {
        merged.reset();
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(state.threads));
        for (int t = 0; t < state.threads; ++t) {
            workers.emplace_back([&] { counters_bench::touch_tls(merged); });
        }
        for (auto& th : workers) th.join();
        benchmark::DoNotOptimize(merged.pixels_touched.load());
    }
    const auto total_ops = static_cast<int64_t>(state.iterations())
                           * state.threads
                           * counters_bench::kItersPerThread
                           * counters_bench::kCountersPerIter;
    state.SetItemsProcessed(total_ops);
    state.SetComplexityN(state.threads);
}

} // namespace

BENCHMARK(BM_FramebufferClear)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FrameConversionYUV420P)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_FrameConversionNV12)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ConvertedFrameCacheHit)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Blur3Pass)->Arg(10)->Arg(50)->Arg(100)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Blur2Pass)->Arg(10)->Arg(50)->Arg(100)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TileRenderNoTiles)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TileRenderSequential)->Arg(32)->Arg(64)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_TileRenderParallel)->Arg(32)->Arg(64)->Unit(benchmark::kMillisecond);

// Dirty-ratio sweep: single-pass vs tile at 2,4,8,16,32,64 elements
BENCHMARK_CAPTURE(BM_TileDirtyRatioSweep, Single, false)
    ->RangeMultiplier(2)->Range(2, 64)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_TileDirtyRatioSweep, Tiled, true)
    ->RangeMultiplier(2)->Range(2, 64)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_CompositingBlendModes)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_MotionBlurAccumulation)->Arg(4)->Arg(8)->Arg(16)->Unit(benchmark::kMillisecond);

// RenderCounters throughput: sweep 1, 2, 4, 8 threads for both paths.
// Compare the two on the same machine to read the actual speedup.
BENCHMARK(BM_CountersAtomicShared)
    ->ThreadRange(1, benchmark::CPUInfo::GetNumPhysicalCores())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
BENCHMARK(BM_CountersThreadLocalMerge)
    ->ThreadRange(1, benchmark::CPUInfo::GetNumPhysicalCores())
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();
