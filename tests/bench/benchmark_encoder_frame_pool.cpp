#include <benchmark/benchmark.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/media/frame_conversion/encoder_frame_pool.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::video;

namespace {

static Framebuffer make_source(int width, int height) {
    Framebuffer fb(width, height);
    fb.clear(Color{0.35f, 0.55f, 0.8f, 1.0f});
    return fb;
}

static void BM_EncoderFramePool_ConvertDirect(benchmark::State& state) {
    const int width = static_cast<int>(state.range(0));
    const int height = static_cast<int>(state.range(1));
    Framebuffer source = make_source(width, height);
    EncoderFramePool pool({
        .width = width,
        .height = height,
        .format = EncoderPixelFormat::YUV420P,
        .slot_count = 4,
    });

    for (auto _ : state) {
        auto frame = pool.acquire();
        if (!frame) {
            state.SkipWithError("encoder frame pool exhausted");
            return;
        }
        const auto result = convert_frame_tight(
            source, frame.planes, width, height,
            EncoderPixelFormat::YUV420P, YuvMatrix::BT709,
            ColorRange::Limited, true);
        benchmark::DoNotOptimize(result);
        if (!result.success) {
            state.SkipWithError("direct conversion failed");
            return;
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["pool_slots"] = static_cast<double>(pool.stats().slots_allocated);
    state.counters["slot_reuses"] = static_cast<double>(pool.stats().slot_reuses);
    state.counters["conversion_bytes"] = static_cast<double>(
        pool.frame_bytes()) * static_cast<double>(state.iterations());
}

/// Baseline for the previous adapter contract: conversion writes into one
/// reusable contiguous staging vector and the caller submits that packed view.
/// This intentionally measures only the in-Chronon destination management
/// difference; the FFmpeg process-boundary pipe copy is common to both paths.
static void BM_EncoderFrameStaging_Convert(benchmark::State& state) {
    const int width = static_cast<int>(state.range(0));
    const int height = static_cast<int>(state.range(1));
    Framebuffer source = make_source(width, height);
    const auto frame_bytes = EncoderFramePool::encoded_size(
        width, height, EncoderPixelFormat::YUV420P);
    std::vector<uint8_t> staging(frame_bytes);

    for (auto _ : state) {
        const auto y_size = static_cast<std::size_t>(width) * height;
        const auto result = convert_frame_tight(
            source,
            FramePlanes{
                .y = staging.data(),
                .u = staging.data() + y_size,
                .v = staging.data() + y_size + y_size / 4,
                .stride_y = width,
                .stride_u = width / 2,
                .stride_v = width / 2,
            },
            width, height, EncoderPixelFormat::YUV420P,
            YuvMatrix::BT709, ColorRange::Limited, true);
        benchmark::DoNotOptimize(result);
        if (!result.success) {
            state.SkipWithError("staging conversion failed");
            return;
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["staging_slots"] = 1.0;
    state.counters["staging_copy_bytes"] = static_cast<double>(
        frame_bytes) * static_cast<double>(state.iterations());
}

} // namespace

BENCHMARK(BM_EncoderFramePool_ConvertDirect)
    ->Args({640, 360})
    ->Args({1280, 720})
    ->Args({1920, 1080})
    ->MinTime(0.1);

BENCHMARK(BM_EncoderFrameStaging_Convert)
    ->Args({640, 360})
    ->Args({1280, 720})
    ->Args({1920, 1080})
    ->MinTime(0.1);
