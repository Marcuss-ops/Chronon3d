// ── DOF Benchmark: Disc Kernel (O(r²)) vs Separable Two-Pass (O(2r)) ─────
// Compares the original scatter-as-gather disc blur with the optimised
// separable horizontal+vertical blur across a range of blur radii and
// two resolutions (640×360 and 1920×1080).
//
// Build (from project root):
//   cmake --preset linux-debug -DCHRONON3D_BUILD_BENCHMARKS=ON
//   cmake --build build --target chronon3d_dof_benchmarks -- -j$(nproc)
//   ./build/tests/chronon3d_dof_benchmarks --benchmark_min_time=0.5s

#include <benchmark/benchmark.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/backends/software/utils/effects/per_pixel_dof.hpp>

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <hwy/highway.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::renderer;

// ══════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════

namespace {

/// Create a framebuffer with a simple depth-test pattern:
/// - Left 1/3:  foreground (z = -300)   → small blur
/// - Centre 1/3: subject (z = 0)        → in focus
/// - Right 1/3: background (z = 1200)   → large blur
/// Pixels are semi-random non-uniform colours so blur is visible.
static void populate_fb_and_depth(Framebuffer& fb, std::vector<float>& depth,
                                  int w, int h) {
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
    depth.assign(static_cast<size_t>(w) * h, 0.0f);

    for (i32 y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            const float fx = static_cast<float>(x) / static_cast<float>(w);
            // Colour gradient with some variation to avoid trivial fast-paths
            row[x] = Color{
                0.3f + 0.4f * fx,
                0.5f + 0.2f * std::sin(static_cast<float>(y) * 0.05f),
                0.7f - 0.3f * fx,
                0.8f + 0.2f * std::sin(static_cast<float>(x + y) * 0.02f)
            };

            if (fx < 0.333f)
                depth[idx] = -300.0f;   // foreground
            else if (fx < 0.666f)
                depth[idx] = 0.0f;      // in focus
            else
                depth[idx] = 1200.0f;   // background
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Reference: O(r²) disc kernel (original algorithm)
// ══════════════════════════════════════════════════════════════════════════

static void apply_disc_dof(Framebuffer& fb, const std::vector<float>& depth,
                           const DepthOfFieldSettings& dof) {
    if (!dof.enabled) return;

    constexpr float kUnsetDepth = 1e18f;
    const i32 w = fb.width();
    const i32 h = fb.height();
    if (static_cast<i32>(depth.size()) != w * h) return;

    // Pre-compute blur radii
    std::vector<float> blur_radii(static_cast<size_t>(w) * h, 0.0f);
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            const float z = depth[idx];
            if (z < kUnsetDepth * 0.5f) {
                blur_radii[idx] = compute_dof_blur_radius(dof, z);
            }
        }
    }

    // Single scratch buffer (copy source; disc kernel reads from this copy)
    std::vector<Color> output(static_cast<size_t>(w) * h);
    for (i32 y = 0; y < h; ++y) {
        const Color* src_row = fb.pixels_row(y);
        std::copy_n(src_row, w, &output[static_cast<size_t>(y) * w]);
    }

    // ── Disc blur: O(r²) per pixel, TBB parallel over rows ─────────────
    tbb::parallel_for(tbb::blocked_range<i32>(0, h),
        [&](const tbb::blocked_range<i32>& range) {
            for (i32 y = range.begin(); y < range.end(); ++y) {
                for (i32 x = 0; x < w; ++x) {
                    const size_t idx = static_cast<size_t>(y) * w + x;
                    const float my_radius = blur_radii[idx];
                    if (my_radius < 0.5f) continue;

                    const i32 r = static_cast<i32>(std::ceil(my_radius));
                    const float inv_r = 1.0f / my_radius;

                    float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
                    float total_weight = 0.0f;

                    const i32 sy0 = std::max(0, y - r);
                    const i32 sy1 = std::min(h - 1, y + r);
                    const i32 sx0 = std::max(0, x - r);
                    const i32 sx1 = std::min(w - 1, x + r);

                    for (i32 ky = sy0; ky <= sy1; ++ky) {
                        for (i32 kx = sx0; kx <= sx1; ++kx) {
                            const Color& s = output[static_cast<size_t>(ky) * w + kx];
                            if (s.a <= 0.0f) continue;

                            const float dx = static_cast<float>(kx - x);
                            const float dy = static_cast<float>(ky - y);
                            const float dist = std::sqrt(dx * dx + dy * dy);
                            const float t = std::clamp(dist * inv_r, 0.0f, 1.0f);
                            const float falloff = 1.0f - t * t * (3.0f - 2.0f * t);
                            const float weight = s.a * falloff;

                            sum_r += s.r * weight;
                            sum_g += s.g * weight;
                            sum_b += s.b * weight;
                            sum_a += s.a * weight;
                            total_weight += weight;
                        }
                    }

                    if (total_weight > 1e-6f) {
                        const float inv_w = 1.0f / total_weight;
                        output[idx] = {sum_r * inv_w, sum_g * inv_w,
                                       sum_b * inv_w, sum_a * inv_w};
                    }
                }
            }
        });

    // Write back
    tbb::parallel_for(tbb::blocked_range<i32>(0, h),
        [&](const tbb::blocked_range<i32>& range) {
            using namespace hwy::HWY_NAMESPACE;
            const ScalableTag<float> df;
            const size_t lanes = Lanes(df);
            const int floats_per_row = w * 4;

            for (i32 y = range.begin(); y < range.end(); ++y) {
                const float* src = reinterpret_cast<const float*>(
                    &output[static_cast<size_t>(y) * w]);
                float* dst = reinterpret_cast<float*>(fb.pixels_row(y));

                int fx = 0;
                for (; fx + static_cast<int>(lanes) <= floats_per_row;
                     fx += static_cast<int>(lanes)) {
                    Store(Load(df, src + fx), df, dst + fx);
                }
                for (; fx < floats_per_row; ++fx) {
                    dst[fx] = src[fx];
                }
            }
        });
}

// ══════════════════════════════════════════════════════════════════════════
// Benchmark: Disc kernel (O(r²))
// ══════════════════════════════════════════════════════════════════════════

static void BM_DofDiscKernel(benchmark::State& state) {
    const int w = static_cast<int>(state.range(0));
    const int h = static_cast<int>(state.range(1));
    const float aperture = static_cast<float>(state.range(2)) * 0.001f;

    Framebuffer fb(w, h);
    std::vector<float> depth;
    populate_fb_and_depth(fb, depth, w, h);

    const DepthOfFieldSettings dof{
        .enabled = true,
        .focus_z = 0.0f,
        .aperture = aperture,
        .max_blur = 48.0f
    };

    for (auto _ : state) {
        apply_disc_dof(fb, depth, dof);
        benchmark::DoNotOptimize(fb.get_pixel(0, 0));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

// ══════════════════════════════════════════════════════════════════════════
// Benchmark: Separable two-pass (O(2r))
// ══════════════════════════════════════════════════════════════════════════

static void BM_DofSeparable(benchmark::State& state) {
    const int w = static_cast<int>(state.range(0));
    const int h = static_cast<int>(state.range(1));
    const float aperture = static_cast<float>(state.range(2)) * 0.001f;

    Framebuffer fb(w, h);
    std::vector<float> depth;
    populate_fb_and_depth(fb, depth, w, h);

    const DepthOfFieldSettings dof{
        .enabled = true,
        .focus_z = 0.0f,
        .aperture = aperture,
        .max_blur = 48.0f
    };

    for (auto _ : state) {
        apply_per_pixel_dof(fb, depth, dof);
        benchmark::DoNotOptimize(fb.get_pixel(0, 0));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * w * h);
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// Registration: sweep aperture (which controls blur radius via dist × aperture)
//
// With focus_z=0 and max_blur=48:
//   aperture=5   → foreground r=1.5, background r=6   (small blur)
//   aperture=10  → foreground r=3,   background r=12  (medium)
//   aperture=20  → foreground r=6,   background r=24  (large)
//   aperture=40  → foreground r=12,  background r=48  (max, clamped)
// ══════════════════════════════════════════════════════════════════════════

// 640×360 — interactive quality
BENCHMARK(BM_DofDiscKernel)
    ->Args({640, 360, 5})->Args({640, 360, 10})
    ->Args({640, 360, 20})->Args({640, 360, 40})
    ->Unit(benchmark::kMillisecond)->Name("DofDiscKernel_640x360");

BENCHMARK(BM_DofSeparable)
    ->Args({640, 360, 5})->Args({640, 360, 10})
    ->Args({640, 360, 20})->Args({640, 360, 40})
    ->Unit(benchmark::kMillisecond)->Name("DofSeparable_640x360");

// 1920×1080 — full HD
BENCHMARK(BM_DofDiscKernel)
    ->Args({1920, 1080, 5})->Args({1920, 1080, 10})
    ->Args({1920, 1080, 20})->Args({1920, 1080, 40})
    ->Unit(benchmark::kMillisecond)->Name("DofDiscKernel_1920x1080");

BENCHMARK(BM_DofSeparable)
    ->Args({1920, 1080, 5})->Args({1920, 1080, 10})
    ->Args({1920, 1080, 20})->Args({1920, 1080, 40})
    ->Unit(benchmark::kMillisecond)->Name("DofSeparable_1920x1080");

BENCHMARK_MAIN();
