// =============================================================================
// bench_prgb32_conversion.cpp — PRGB32 ↔ Color SIMD conversion benchmarks
//
/// Standalone benchmark using std::chrono (no Google Benchmark dependency).
/// Compares the full AVX2 SIMD path against a scalar reference for both
/// bl_image_prgb32_to_color_row and color_to_prgb32_row.
///
/// Build & run:
///   cd build/chronon/linux-fast-dev
///   g++ -std=c++20 -O2 -mavx2 -mfma \
///       -I../../../include -I../../../deps/include \
///       -I../../../vcpkg_installed/x64-linux/include \
///       ../../../tests/bench/bench_prgb32_conversion.cpp \
///       src/backends/software/CMakeFiles/chronon3d_backend_software.dir/simd/highway_color_kernels.cpp.o \
///       -L../../../vcpkg_installed/x64-linux/lib -lhwy -lfmt -lspdlog \
///       -o bench_prgb32 && ./bench_prgb32
// =============================================================================

#include <chronon3d/math/color.hpp>
#include <chronon3d/simd/kernels.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace chronon3d;

// ── Scalar reference implementations ────────────────────────────────────────

static void scalar_bl_image_prgb32_to_color_row(Color* dst, const uint32_t* src,
                                                 int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        const uint32_t p = src[i];
        const float a = static_cast<float>((p >> 24) & 0xFF) * (1.0f / 255.0f);
        if (a <= 0.0f) {
            dst[i] = Color{0.0f, 0.0f, 0.0f, 0.0f};
            continue;
        }
        const float inv_a = 1.0f / a;
        dst[i] = Color{
            std::clamp(static_cast<float>((p >> 16) & 0xFF) * (1.0f / 255.0f) * inv_a, 0.0f, 1.0f),
            std::clamp(static_cast<float>((p >>  8) & 0xFF) * (1.0f / 255.0f) * inv_a, 0.0f, 1.0f),
            std::clamp(static_cast<float>( p        & 0xFF) * (1.0f / 255.0f) * inv_a, 0.0f, 1.0f),
            a
        };
    }
}

static void scalar_color_to_prgb32_row(uint32_t* dst, const Color* src,
                                         int pixel_count) {
    const auto pack = [](float v) -> uint32_t {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    for (int i = 0; i < pixel_count; ++i) {
        const float a = std::clamp(src[i].a, 0.0f, 1.0f);
        const uint32_t pa = pack(a);
        const uint32_t pr = pack(std::clamp(src[i].r * a, 0.0f, 1.0f));
        const uint32_t pg = pack(std::clamp(src[i].g * a, 0.0f, 1.0f));
        const uint32_t pb = pack(std::clamp(src[i].b * a, 0.0f, 1.0f));
        dst[i] = (pa << 24) | (pr << 16) | (pg << 8) | pb;
    }
}

// ── Data generation ─────────────────────────────────────────────────────────

static void generate_glow_prgb32(std::vector<uint32_t>& out, int count) {
    uint32_t rng = 0xBEEFCAFE;
    auto randf = [&]() -> float {
        rng = rng * 1103515245U + 12345U;
        return static_cast<float>(rng & 0x7FFFFFFF) / 2147483648.0f;
    };
    out.resize(count);
    for (int i = 0; i < count; ++i) {
        if (randf() < 0.6f) {
            out[i] = 0x00000000u;
        } else {
            const float a = 0.2f + randf() * 0.6f;
            const uint8_t ai = static_cast<uint8_t>(a * 255.0f);
            const uint8_t ri = static_cast<uint8_t>(randf() * a * 255.0f);
            const uint8_t gi = static_cast<uint8_t>(randf() * a * 255.0f);
            const uint8_t bi = static_cast<uint8_t>(randf() * a * 255.0f);
            out[i] = (static_cast<uint32_t>(ai) << 24) | (ri << 16) | (gi << 8) | bi;
        }
    }
}

static void generate_glow_color(std::vector<Color>& out, int count) {
    uint32_t rng = 0xDEADBEEF;
    auto randf = [&]() -> float {
        rng = rng * 1103515245U + 12345U;
        return static_cast<float>(rng & 0x7FFFFFFF) / 2147483648.0f;
    };
    out.resize(count);
    for (int i = 0; i < count; ++i) {
        if (randf() < 0.6f) {
            out[i] = Color{0.0f, 0.0f, 0.0f, 0.0f};
        } else {
            const float a = 0.2f + randf() * 0.6f;
            out[i] = Color{randf(), randf(), randf(), a};
        }
    }
}

// ── Benchmark harness ───────────────────────────────────────────────────────

struct BenchResult {
    double median_us;
    double throughput_gbs;  // GB/s of output data
};

/// Run `func` for `iterations` warmup + 10 timed runs, return median time.
template <typename Func>
static BenchResult run_bench(Func&& func, int pixels, int output_bytes,
                              int iterations = 200) {
    // Warmup
    for (int i = 0; i < iterations; ++i) func();

    // Timed runs
    constexpr int kRuns = 10;
    double times[kRuns];
    for (int r = 0; r < kRuns; ++r) {
        auto t0 = std::chrono::high_resolution_clock::now();
        func();
        auto t1 = std::chrono::high_resolution_clock::now();
        times[r] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
    std::sort(times, times + kRuns);
    const double median = times[kRuns / 2];
    const double throughput = (static_cast<double>(output_bytes) * 1e-3) / median;  // GB/s
    return {median, throughput};
}

/// Prevent compiler from optimizing away the result.
static void do_not_optimize(const void* ptr) {
    asm volatile("" : : "r,m"(ptr) : "memory");
}

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
    const int widths[] = {256, 640, 1920, 3840};

    std::printf("\n");
    std::printf("═══════════════════════════════════════════════════════════════\n");
    std::printf("  PRGB32 ↔ Color Conversion Benchmark — SIMD vs Scalar\n");
    std::printf("  Data: ~60%% transparent padding (text glow pattern)\n");
    std::printf("═══════════════════════════════════════════════════════════════\n");

    // ── PRGB32 → Color (un-premultiply) ─────────────────────────────────
    std::printf("\n  PRGB32 → Color (un-premultiply)\n");
    std::printf("  %-8s  %10s  %8s  %10s  %8s  %s\n",
                "Width", "Scalar(µs)", "GB/s", "SIMD(µs)", "GB/s", "Speedup");
    std::printf("  %-8s  %10s  %8s  %10s  %8s  %s\n",
                "────", "──────────", "──────", "────────", "──────", "───────");

    for (int w : widths) {
        std::vector<uint32_t> src;
        std::vector<Color> dst(w);
        generate_glow_prgb32(src, w);

        auto scalar_res = run_bench([&]() {
            scalar_bl_image_prgb32_to_color_row(dst.data(), src.data(), w);
            do_not_optimize(&dst[0]);
        }, w, w * static_cast<int>(sizeof(Color)));

        auto simd_res = run_bench([&]() {
            simd::bl_image_prgb32_to_color_row(dst.data(), src.data(), w);
            do_not_optimize(&dst[0]);
        }, w, w * static_cast<int>(sizeof(Color)));

        const double speedup = scalar_res.median_us / simd_res.median_us;
        std::printf("  %-8d  %10.2f  %7.2f  %10.2f  %7.2f  %.2fx\n",
                    w, scalar_res.median_us, scalar_res.throughput_gbs,
                    simd_res.median_us, simd_res.throughput_gbs, speedup);
    }

    // ── Color → PRGB32 (premultiply) ────────────────────────────────────
    std::printf("\n  Color → PRGB32 (premultiply)\n");
    std::printf("  %-8s  %10s  %8s  %10s  %8s  %s\n",
                "Width", "Scalar(µs)", "GB/s", "SIMD(µs)", "GB/s", "Speedup");
    std::printf("  %-8s  %10s  %8s  %10s  %8s  %s\n",
                "────", "──────────", "──────", "────────", "──────", "───────");

    for (int w : widths) {
        std::vector<Color> src;
        std::vector<uint32_t> dst(w);
        generate_glow_color(src, w);

        auto scalar_res = run_bench([&]() {
            scalar_color_to_prgb32_row(dst.data(), src.data(), w);
            do_not_optimize(&dst[0]);
        }, w, w * static_cast<int>(sizeof(uint32_t)));

        auto simd_res = run_bench([&]() {
            simd::color_to_prgb32_row(dst.data(), src.data(), w);
            do_not_optimize(&dst[0]);
        }, w, w * static_cast<int>(sizeof(uint32_t)));

        const double speedup = scalar_res.median_us / simd_res.median_us;
        std::printf("  %-8d  %10.2f  %7.2f  %10.2f  %7.2f  %.2fx\n",
                    w, scalar_res.median_us, scalar_res.throughput_gbs,
                    simd_res.median_us, simd_res.throughput_gbs, speedup);
    }

    std::printf("\n═══════════════════════════════════════════════════════════════\n\n");
    return 0;
}
