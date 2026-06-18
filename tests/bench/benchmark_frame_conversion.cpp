// ============================================================================
// benchmark_frame_conversion.cpp — PR2: per-backend YUV conversion benchmarks
//
// Benchmarks each conversion path in isolation:
//   BM_YuvConvert_Highway   — direct float→YUV Highway SIMD
//   BM_YuvConvert_Swscale   — float→RGBA8→swscale
//   BM_YuvConvert_ScalarRef — float→YUV TBB scalar (correctness reference)
//
// Runs across 4 resolutions (640×360 … 3840×2160), 4 realistic datasets,
// YUV420P + NV12, gamma on/off, BT.601 + BT.709.  Reports MPix/s and GB/s
// as custom counters.  The ScalarRef path is correctness-only; the plan
// removes it from production in PR3.
//
// Usage:
//   ./chronon3d_benchmarks \
//     --benchmark_filter='BM_YuvConvert.*1080.*' \
//     --benchmark_repetitions=15 \
//     --benchmark_min_time=0.5 \
//     --benchmark_report_aggregates_only=true \
//     --benchmark_out=yuv_backend_baseline.json \
//     --benchmark_out_format=json
// ============================================================================

#include <benchmark/benchmark.h>

// Internal header: exposes Highway, scalar, and swscale backends directly
// so every benchmark can call exactly one path.
#include "src/media/frame_conversion/internal/yuv_conversion_internal.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/direct_yuv_converter.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::video;

// ============================================================================
//  Dataset helpers — 4 realistic input types
// ============================================================================

namespace {

// Enum at file scope so BenchParams can reference it without -Wsubobject-linkage.
} // anonymous namespace

enum class DatasetKind : int {
    Solid        = 0,   // Uniform mid-gray
    Gradient     = 1,   // Smooth radial + linear ramp
    Photographic = 2,   // Pseudo-photographic noise
    AlphaHeavy   = 3,   // High-alpha coverage with sub-pixel edges
};

namespace {

/// Deterministic seeded RNG so benchmarks are reproducible across machines.
static std::mt19937& bench_rng() {
    thread_local std::mt19937 rng(42);
    return rng;
}

/// Solid fill: every pixel is the same mid-gray.
static void fill_solid(Framebuffer& fb, int w, int h) {
    fb.clear(Color{0.45f, 0.45f, 0.45f, 1.0f});
    (void)w; (void)h;
}

/// Gradient: radial falloff from centre plus linear RGB ramps.
static void fill_gradient(Framebuffer& fb, int w, int h) {
    const float cx = static_cast<float>(w) * 0.5f;
    const float cy = static_cast<float>(h) * 0.5f;
    const float max_r = std::min(cx, cy);
    for (int y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float r = std::sqrt(dx * dx + dy * dy) / max_r;
            row[x] = Color{
                std::clamp(0.2f + 0.8f * static_cast<float>(x) / static_cast<float>(w), 0.0f, 1.0f),
                std::clamp(0.1f + 0.9f * static_cast<float>(y) / static_cast<float>(h), 0.0f, 1.0f),
                std::clamp(1.0f - r, 0.0f, 1.0f),
                1.0f - 0.3f * r,
            };
        }
    }
}

/// Photographic-like noise: Gaussian-ish colour clusters.
static void fill_photographic(Framebuffer& fb, int w, int h) {
    auto& rng = bench_rng();
    std::uniform_real_distribution<float> hue(0.0f, 1.0f);
    std::uniform_real_distribution<float> sat(0.2f, 0.9f);
    std::uniform_real_distribution<float> bri(0.1f, 0.95f);
    std::uniform_real_distribution<float> alpha(0.3f, 1.0f);

    // HSL→RGB helper (inline, no external dependency)
    auto hsl_to_rgb = [](float h, float s, float l) -> Color {
        auto hue2rgb = [](float p, float q, float t) -> float {
            if (t < 0.0f) t += 1.0f;
            if (t > 1.0f) t -= 1.0f;
            if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
            if (t < 1.0f/2.0f) return q;
            if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
            return p;
        };
        if (s == 0.0f) return Color{l, l, l, 1.0f};
        const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        const float p = 2.0f * l - q;
        return Color{
            hue2rgb(p, q, h + 1.0f/3.0f),
            hue2rgb(p, q, h),
            hue2rgb(p, q, h - 1.0f/3.0f),
            1.0f,
        };
    };

    for (int y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const Color c = hsl_to_rgb(hue(rng), sat(rng), bri(rng));
            row[x] = Color{c.r, c.g, c.b, alpha(rng)};
        }
    }
}

/// Alpha-heavy: most pixels have alpha ≥ 0.5, with sparse sub-1/255 edge pixels.
static void fill_alpha_heavy(Framebuffer& fb, int w, int h) {
    auto& rng = bench_rng();
    std::uniform_real_distribution<float> rgb(0.1f, 1.0f);
    std::uniform_real_distribution<float> heavy(0.5f, 1.0f);
    std::uniform_real_distribution<float> edge(0.0f, 8.0f / 255.0f);

    for (int y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const bool is_edge = ((x + y) % 11) == 0;  // ~9% edge pixels
            const float a = is_edge ? edge(rng) : heavy(rng);
            row[x] = Color{rgb(rng), rgb(rng), rgb(rng), a};
        }
    }
}

/// Build a Framebuffer for the requested dataset.  Seed controls the RNG
/// state; identical seed + dimensions + kind → identical pixels.
static Framebuffer make_dataset(int w, int h, DatasetKind kind, uint64_t seed = 42) {
    bench_rng().seed(seed);
    Framebuffer fb(w, h);
    switch (kind) {
        case DatasetKind::Solid:        fill_solid(fb, w, h);        break;
        case DatasetKind::Gradient:     fill_gradient(fb, w, h);     break;
        case DatasetKind::Photographic: fill_photographic(fb, w, h); break;
        case DatasetKind::AlphaHeavy:   fill_alpha_heavy(fb, w, h);  break;
    }
    return fb;
}

} // anonymous namespace

// ============================================================================
//  BenchParams — decode integer Args into typed fields
// ============================================================================

struct BenchParams {
    int width;
    int height;
    EncoderPixelFormat format;
    YuvMatrix matrix;
    bool apply_gamma;
    DatasetKind dataset;

    static BenchParams from_state(const benchmark::State& st) {
        return BenchParams{
            .width       = static_cast<int>(st.range(0)),
            .height      = static_cast<int>(st.range(1)),
            .format      = (st.range(2) == 0) ? EncoderPixelFormat::YUV420P
                                              : EncoderPixelFormat::NV12,
            .matrix      = (st.range(3) == 0) ? YuvMatrix::BT709 : YuvMatrix::BT601,
            .apply_gamma = st.range(4) != 0,
            .dataset     = static_cast<DatasetKind>(st.range(5)),
        };
    }


};

// ============================================================================
//  Output buffer helpers
// ============================================================================

static size_t yuv_total_bytes(int w, int h, EncoderPixelFormat fmt) {
    const size_t y_sz = static_cast<size_t>(w) * h;
    if (fmt == EncoderPixelFormat::NV12) {
        return y_sz + y_sz / 2;   // Y + interleaved UV
    }
    return y_sz + y_sz / 2;        // Y + U + V (same total)
}

static FramePlanes make_planes(
    std::vector<uint8_t>& buf, int w, int h, EncoderPixelFormat fmt)
{
    const size_t y_sz = static_cast<size_t>(w) * h;
    buf.resize(yuv_total_bytes(w, h, fmt));
    std::memset(buf.data(), 0, buf.size());

    if (fmt == EncoderPixelFormat::YUV420P) {
        return FramePlanes{
            .y = buf.data(),
            .u = buf.data() + y_sz,
            .v = buf.data() + y_sz + y_sz / 4,
            .stride_y = w,
            .stride_u = w / 2,
            .stride_v = w / 2,
        };
    }
    // NV12
    return FramePlanes{
        .y = buf.data(),
        .uv = buf.data() + y_sz,
        .stride_y = w,
        .stride_uv = w,
    };
}

// ============================================================================
//  BM_YuvConvert_Highway — direct float→YUV Highway SIMD path
// ============================================================================

static void BM_YuvConvert_Highway(benchmark::State& state) {
    const auto p = BenchParams::from_state(state);

    // Build input framebuffer once (not timed)
    Framebuffer src = make_dataset(p.width, p.height, p.dataset);

    // Pre-allocate output buffer
    std::vector<uint8_t> out_buf;
    const FramePlanes planes = make_planes(out_buf, p.width, p.height, p.format);

    // Warmup: one untimed run to populate TBB worker-pool and caches
    DirectYuvRequest dreq{
        .src = src,
        .planes = planes,
        .width = p.width,
        .height = p.height,
        .format = p.format,
        .matrix = p.matrix,
        .range = ColorRange::Limited,
        .apply_gamma = p.apply_gamma,
    };
    if (p.format == EncoderPixelFormat::YUV420P)
        convert_to_yuv420p_hwy(dreq);
    else
        convert_to_nv12_hwy(dreq);

    for (auto _ : state) {
        std::memset(out_buf.data(), 0, out_buf.size());
        const auto res = (p.format == EncoderPixelFormat::YUV420P)
            ? convert_to_yuv420p_hwy(dreq)
            : convert_to_nv12_hwy(dreq);

        if (!res.success) {
            state.SkipWithError("Highway conversion failed");
            return;
        }
        state.SetIterationTime(
            static_cast<double>(res.conversion_ns) / 1e9);
    }

    const int64_t px = static_cast<int64_t>(state.iterations()) * p.width * p.height;
    state.SetItemsProcessed(px);
    state.counters["MPix/s"] = benchmark::Counter(
        static_cast<double>(px) / 1e6,
        benchmark::Counter::kIsRate);
    const double total_bytes = static_cast<double>(px) * 1.5;
    state.counters["GB/s"] = benchmark::Counter(
        total_bytes / 1e9,
        benchmark::Counter::kIsRate);
}

// ============================================================================
//  BM_YuvConvert_Swscale — float→RGBA8 staging→swscale path
// ============================================================================

static void BM_YuvConvert_Swscale(benchmark::State& state) {
    const auto p = BenchParams::from_state(state);

    Framebuffer src = make_dataset(p.width, p.height, p.dataset);
    std::vector<uint8_t> out_buf;
    const FramePlanes planes = make_planes(out_buf, p.width, p.height, p.format);

    ConvertFrameRequest creq{
        .src = src,
        .planes = planes,
        .width = p.width,
        .height = p.height,
        .format = p.format,
        .matrix = p.matrix,
        .range = ColorRange::Limited,
        .apply_gamma = p.apply_gamma,
    };

    // Warmup
    if (p.format == EncoderPixelFormat::YUV420P)
        convert_rgba_to_yuv420p_swscale(creq);
    else
        convert_rgba_to_nv12_swscale(creq);

    for (auto _ : state) {
        std::memset(out_buf.data(), 0, out_buf.size());
        const auto res = (p.format == EncoderPixelFormat::YUV420P)
            ? convert_rgba_to_yuv420p_swscale(creq)
            : convert_rgba_to_nv12_swscale(creq);

        if (!res.success) {
            state.SkipWithError("Swscale conversion failed");
            return;
        }
        state.SetIterationTime(
            static_cast<double>(res.conversion_ns) / 1e9);
    }

    const int64_t px = static_cast<int64_t>(state.iterations()) * p.width * p.height;
    state.SetItemsProcessed(px);
    state.counters["MPix/s"] = benchmark::Counter(
        static_cast<double>(px) / 1e6,
        benchmark::Counter::kIsRate);
    const double total_bytes = static_cast<double>(px) * 1.5;  // YUV420P/NV12 ~1.5 bytes/px
    state.counters["GB/s"] = benchmark::Counter(
        total_bytes / 1e9,
        benchmark::Counter::kIsRate);
}

// ============================================================================
//  BM_YuvConvert_ScalarRef — TBB scalar float→YUV (correctness reference)
// ============================================================================

static void BM_YuvConvert_ScalarRef(benchmark::State& state) {
    const auto p = BenchParams::from_state(state);

    Framebuffer src = make_dataset(p.width, p.height, p.dataset);
    std::vector<uint8_t> out_buf;
    const FramePlanes planes = make_planes(out_buf, p.width, p.height, p.format);

    DirectYuvRequest dreq{
        .src = src,
        .planes = planes,
        .width = p.width,
        .height = p.height,
        .format = p.format,
        .matrix = p.matrix,
        .range = ColorRange::Limited,
        .apply_gamma = p.apply_gamma,
    };

    // Warmup
    if (p.format == EncoderPixelFormat::YUV420P)
        convert_to_yuv420p_parallel(dreq);
    else
        convert_to_nv12_parallel(dreq);

    for (auto _ : state) {
        std::memset(out_buf.data(), 0, out_buf.size());
        const auto res = (p.format == EncoderPixelFormat::YUV420P)
            ? convert_to_yuv420p_parallel(dreq)
            : convert_to_nv12_parallel(dreq);

        if (!res.success) {
            state.SkipWithError("Scalar conversion failed");
            return;
        }
        state.SetIterationTime(
            static_cast<double>(res.conversion_ns) / 1e9);
    }

    const int64_t px = static_cast<int64_t>(state.iterations()) * p.width * p.height;
    state.SetItemsProcessed(px);
    state.counters["MPix/s"] = benchmark::Counter(
        static_cast<double>(px) / 1e6,
        benchmark::Counter::kIsRate);
    const double total_bytes = static_cast<double>(px) * 1.5;
    state.counters["GB/s"] = benchmark::Counter(
        total_bytes / 1e9,
        benchmark::Counter::kIsRate);
}

// ============================================================================
//  Registration — full matrix of (dim, fmt, matrix, gamma, dataset)
//
//  Args layout: {width, height, format, matrix, gamma, dataset}
//    format: 0=YUV420P, 1=NV12
//    matrix: 0=BT.709, 1=BT.601
//    gamma:  0=off, 1=on
//    dataset: 0=Solid, 1=Gradient, 2=Photographic, 3=AlphaHeavy
// ============================================================================

// All resolutions we care about
static constexpr int kResolutions[][2] = {
    {640, 360},
    {1280, 720},
    {1920, 1080},
    {3840, 2160},
};

/// Register one conversion benchmark over all dimension/format/matrix/gamma/dataset combos.
/// Each registration chain is at file scope (google-benchmark requirement).
static void register_yuv_bench(benchmark::internal::Benchmark* b) {
    for (const auto& res : kResolutions) {
        for (int fmt = 0; fmt <= 1; ++fmt) {
            for (int mat = 0; mat <= 1; ++mat) {
                for (int gm = 0; gm <= 1; ++gm) {
                    for (int ds = 0; ds <= 3; ++ds) {
                        b->Args({res[0], res[1], fmt, mat, gm, ds});
                    }
                }
            }
        }
    }
    b->Unit(benchmark::kMicrosecond)
     ->Repetitions(15)
     ->MinTime(0.5)
     ->UseManualTime();
}

// Register the three backends
BENCHMARK(BM_YuvConvert_Highway)->Apply(register_yuv_bench);
BENCHMARK(BM_YuvConvert_Swscale)->Apply(register_yuv_bench);
BENCHMARK(BM_YuvConvert_ScalarRef)->Apply(register_yuv_bench);
