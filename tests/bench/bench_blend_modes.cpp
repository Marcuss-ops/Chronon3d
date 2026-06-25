// =============================================================================
// bench_blend_modes.cpp — B7: Blend mode performance benchmarks
//
/// Measures the throughput of each compositing blend mode across multiple
/// buffer sizes and compares the Highway SIMD path against the scalar
/// reference.  Key metrics reported per benchmark:
///
///   - ItemsProcessed (pixels × iterations)
///   - BytesProcessed
///   - custom counter "gb_s" — effective GB/s throughput
///
/// Sizes tested (256×256, 640×360, 1920×1080) cover the common use cases.
/// Each benchmark processes `pixel_count` pixels via the SIMD kernel.
/// The scalar reference is NOT benchmarked (it's only for correctness);
/// benchmark targets are the real SIMD implementations.
// =============================================================================

#include <benchmark/benchmark.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/simd/kernels.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
using namespace chronon3d;


namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Fill a framebuffer with deterministic semi-random premultiplied data.
/// Uses a simple LCG so results are reproducible across runs.
/// Each pixel has alpha in [0.2, 1.0] and RGB in [0, 1.5] (HDR-safe).
static void fill_premul_fb(Framebuffer& fb, uint32_t seed = 0xDEADBEEF) {
    uint32_t state = seed;
    auto rng = [&]() -> float {
        state = state * 1103515245U + 12345U;
        return static_cast<float>(state & 0x7FFFFFFF) / 2147483648.0f;
    };

    const int total = fb.width() * fb.height();
    for (int i = 0; i < total; ++i) {
        const float a = 0.2f + rng() * 0.8f;       // keep alpha non-trivial
        const float r = rng() * 1.5f * a;           // premultiplied
        const float g = rng() * 1.5f * a;
        const float b = rng() * 1.5f * a;
        fb.pixels_row(0)[i] = Color{r, g, b, a};
    }
}

/// Alias for a SIMD blend kernel.
using BlendFunc = void (*)(std::span<Color> dst, std::span<const Color> src);

/// Platform-portable aligned allocation (C11 aligned_alloc or MSVC _aligned_malloc).
static Color* aligned_color_alloc(int count) {
    return static_cast<Color*>(std::aligned_alloc(
        64, static_cast<size_t>(count) * sizeof(Color)));
}

static void aligned_color_free(Color* ptr) {
    std::free(ptr);
}

/// Normal blend wrapper — adapts composite_normal_premul (3 params with default)
/// to the 2-param BlendFunc signature for benchmarking.
namespace simd {
static void composite_normal_premul_wrapper(std::span<Color> d, std::span<const Color> s) {
    composite_normal_premul(d, s);
}
}  // namespace simd
static void bench_blend(benchmark::State& state, BlendFunc blend_func,
                        int pixels, const char* name) {
    // Allocate and fill source & destination buffers.
    // Use 64-byte aligned allocations for SIMD-friendliness.
    auto* src = aligned_color_alloc(pixels);
    auto* dst = aligned_color_alloc(pixels);
    if (!src || !dst) {
        state.SkipWithError("aligned_alloc failed");
        aligned_color_free(src);
        aligned_color_free(dst);
        return;
    }

    // Fill with deterministic data.
    {
        uint32_t rng_state = 0xC0FFEE;
        auto rng = [&]() -> float {
            rng_state = rng_state * 1103515245U + 12345U;
            return static_cast<float>(rng_state & 0x7FFFFFFF) / 2147483648.0f;
        };
        for (int i = 0; i < pixels; ++i) {
            const float a = 0.2f + rng() * 0.8f;
            src[i] = Color{rng() * 1.5f * a, rng() * 1.5f * a,
                           rng() * 1.5f * a, a};
            dst[i] = Color{rng() * 1.5f * a, rng() * 1.5f * a,
                           rng() * 1.5f * a, a};
        }
    }

    // Warmup: one call to ensure any lazy resolution/caching is done.
    blend_func(std::span<Color>(dst, pixels), std::span<const Color>(src, pixels));

    for (auto _ : state) {
        blend_func(std::span<Color>(dst, pixels), std::span<const Color>(src, pixels));
        benchmark::DoNotOptimize(dst[0]);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * pixels);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * pixels *
                            static_cast<int64_t>(sizeof(Color)));

    // GB/s throughput (benchmark::Counter kIsRate auto-divides by elapsed time)
    const double bytes_per_iter = static_cast<double>(pixels) * sizeof(Color);
    state.counters["gb_s"] = benchmark::Counter(
        bytes_per_iter * 1e-9,
        benchmark::Counter::kIsRate);

    // Label with resolution name
    state.SetLabel(name);

    aligned_color_free(src);
    aligned_color_free(dst);
}

// ── Pixel counts for the 3 tested resolutions ───────────────────────────────
constexpr int kPixels_Small  =   256 * 256;    // 65K
constexpr int kPixels_Medium =   640 * 360;    // 230K
constexpr int kPixels_Large  = 1920 * 1080;    // 2M

// ── Blend-mode benchmark macros ─────────────────────────────────────────────

#define DEFINE_BLEND_BENCH(name, func)                                         \
    static void BM_Blend_##name##_Small(benchmark::State& state) {             \
        bench_blend(state, simd::func, kPixels_Small, #name);                  \
    }                                                                           \
    static void BM_Blend_##name##_Medium(benchmark::State& state) {            \
        bench_blend(state, simd::func, kPixels_Medium, #name);                 \
    }                                                                           \
    static void BM_Blend_##name##_Large(benchmark::State& state) {             \
        bench_blend(state, simd::func, kPixels_Large, #name);                  \
    }                                                                           \
    BENCHMARK(BM_Blend_##name##_Small)->Unit(benchmark::kMicrosecond);         \
    BENCHMARK(BM_Blend_##name##_Medium)->Unit(benchmark::kMicrosecond);        \
    BENCHMARK(BM_Blend_##name##_Large)->Unit(benchmark::kMicrosecond)

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// 8 blend modes benchmarked at 3 resolutions = 24 benchmarks
// ══════════════════════════════════════════════════════════════════════════

DEFINE_BLEND_BENCH(Normal,     composite_normal_premul_wrapper);
DEFINE_BLEND_BENCH(Multiply,   composite_multiply_premul);
DEFINE_BLEND_BENCH(Screen,     composite_screen_premul);
DEFINE_BLEND_BENCH(Overlay,    composite_overlay_premul);
DEFINE_BLEND_BENCH(Darken,     composite_darken_premul);
DEFINE_BLEND_BENCH(Lighten,    composite_lighten_premul);
DEFINE_BLEND_BENCH(Difference, composite_difference_premul);
DEFINE_BLEND_BENCH(Exclusion,  composite_exclusion_premul);

// ══════════════════════════════════════════════════════════════════════════
// Special modes: Add, SoftLight, HardLight, ColorDodge, ColorBurn
// ══════════════════════════════════════════════════════════════════════════

DEFINE_BLEND_BENCH(Add,        composite_add_premul);
DEFINE_BLEND_BENCH(SoftLight,  composite_soft_light_premul);
DEFINE_BLEND_BENCH(HardLight,  composite_hard_light_premul);
DEFINE_BLEND_BENCH(ColorDodge, composite_color_dodge_premul);
DEFINE_BLEND_BENCH(ColorBurn,  composite_color_burn_premul);

// ══════════════════════════════════════════════════════════════════════════
// Matte benchmarks: alpha matte and luma matte
// ══════════════════════════════════════════════════════════════════════════

static void BM_Matte_Alpha_Small(benchmark::State& state) {
    bench_blend(state, [](std::span<Color> d, std::span<const Color> s) {
        simd::apply_alpha_matte_premul(d, s, false);
    }, kPixels_Small, "AlphaMatte");
}
BENCHMARK(BM_Matte_Alpha_Small)->Unit(benchmark::kMicrosecond);

static void BM_Matte_Alpha_Inverted_Small(benchmark::State& state) {
    bench_blend(state, [](std::span<Color> d, std::span<const Color> s) {
        simd::apply_alpha_matte_premul(d, s, true);
    }, kPixels_Small, "AlphaMatteInverted");
}
BENCHMARK(BM_Matte_Alpha_Inverted_Small)->Unit(benchmark::kMicrosecond);

static void BM_Matte_Luma_Small(benchmark::State& state) {
    bench_blend(state, [](std::span<Color> d, std::span<const Color> s) {
        simd::apply_luma_matte_premul(d, s, false);
    }, kPixels_Small, "LumaMatte");
}
BENCHMARK(BM_Matte_Luma_Small)->Unit(benchmark::kMicrosecond);

