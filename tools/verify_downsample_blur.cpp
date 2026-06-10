// ---------------------------------------------------------------------------
// verify_downsample_blur.cpp
//
// Compares full-resolution box blur vs downsample→blur→upscale pixel-by-pixel
// to verify that the downsample path produces visually equivalent output.
//
// Fully self-contained — uses only chronon3d header types + TBB.
//
// Build:
//   g++ -std=c++20 -O2 -march=native \
//       -I include -I deps/include \
//       tools/verify_downsample_blur.cpp \
//       -lpthread -ltbb -o /tmp/verify_blur
//
//   /tmp/verify_blur        # 640×360 default
//   /tmp/verify_blur 1920 1080 24.0   # custom size + radius
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <memory>
#include <chrono>
#include <vector>

using namespace chronon3d;

// ── Stub for g_current_counters (tool doesn't need profiling, Framebuffer
//    references this symbol but checks for null before using it). ────────────
namespace chronon3d::profiling {
thread_local RenderCounters* g_current_counters = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// 1. Full-resolution box blur (3-pass sliding window)
// ═══════════════════════════════════════════════════════════════════════════

static void box_blur_horizontal(Framebuffer& dst, const Framebuffer& src, int r) {
    const int w = src.width(), h = src.height();
    tbb::parallel_for(tbb::blocked_range<int>(0, h), [&](const tbb::blocked_range<int>& ry) {
        for (int y = ry.begin(); y < ry.end(); ++y) {
            const Color* src_row = src.pixels_row(y);
            Color* dst_row = dst.pixels_row(y);
            Color sum{0,0,0,0};
            for (int x = -r; x <= r; ++x) {
                const Color& p = src_row[std::clamp(x, 0, w-1)];
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const float inv = 1.0f / (2 * r + 1);
            for (int x = 0; x < w; ++x) {
                dst_row[x] = {sum.r*inv, sum.g*inv, sum.b*inv, sum.a*inv};
                const Color& add = src_row[std::min(x+r+1, w-1)];
                const Color& rem = src_row[std::max(x-r,   0)];
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
    });
}

static void box_blur_vertical(Framebuffer& dst, const Framebuffer& src, int r) {
    const int w = src.width(), h = src.height();
    tbb::parallel_for(tbb::blocked_range<int>(0, w), [&](const tbb::blocked_range<int>& rx) {
        for (int x = rx.begin(); x < rx.end(); ++x) {
            Color sum{0,0,0,0};
            for (int y = -r; y <= r; ++y) {
                const Color& p = src.pixels_row(std::clamp(y, 0, h-1))[x];
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const float inv = 1.0f / (2 * r + 1);
            for (int y = 0; y < h; ++y) {
                dst.pixels_row(y)[x] = {sum.r*inv, sum.g*inv, sum.b*inv, sum.a*inv};
                const Color& add = src.pixels_row(std::min(y+r+1, h-1))[x];
                const Color& rem = src.pixels_row(std::max(y-r,   0))[x];
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
    });
}

/// Full-res 3-pass box blur (H → V → H) — used as reference.
static void apply_3pass_blur(Framebuffer& fb, int radius) {
    if (radius <= 0) return;
    const int w = fb.width(), h = fb.height();
    auto tmp1 = std::make_shared<Framebuffer>(w, h);
    auto tmp2 = std::make_shared<Framebuffer>(w, h);
    box_blur_horizontal(*tmp1, fb, radius);
    box_blur_vertical(*tmp2, *tmp1, radius);
    box_blur_horizontal(fb, *tmp2, radius);
}

/// two_pass_equivalent_radius: match the Gaussian spread of a 3-pass box
/// filter with only 2 passes (H → V) by using a slightly larger radius.
/// Math: 3-pass variance = (r²+r)/3. 2-pass variance = (r₂²+r₂)/6.
/// Match: (r²+r)/3 = (r₂²+r₂)/6 → r₂²+r₂ = 2(r²+r) → r₂ = (√(1+6(r²+r))-1)/2
static int two_pass_equivalent_radius(int r) noexcept {
    const double r_plus = static_cast<double>(r) * static_cast<double>(r) + static_cast<double>(r);
    const double r2 = 0.5 * (std::sqrt(1.0 + 6.0 * r_plus) - 1.0);
    return std::max(1, static_cast<int>(std::round(r2)));
}

/// Full-res 2-pass box blur (H → V) with equivalent radius.
/// Uses 2 passes instead of 3 = ~33% fewer iterations, but slightly larger
/// radius per pass (e.g., r=10 → equiv r₂=14).
static void apply_2pass_blur(Framebuffer& fb, int radius) {
    if (radius <= 0) return;
    const int r2 = two_pass_equivalent_radius(radius);
    const int w = fb.width(), h = fb.height();
    auto tmp = std::make_shared<Framebuffer>(w, h);
    box_blur_horizontal(*tmp, fb, r2);
    box_blur_vertical(fb, *tmp, r2);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Downsample → blur → upscale path
// ═══════════════════════════════════════════════════════════════════════════

/// Box-filter 2× downsample (average of 2×2 blocks)
static void downsample_2x(const Framebuffer& src, Framebuffer& dst) {
    const int sw = src.width(), sh = src.height();
    const int dw = dst.width(), dh = dst.height();
    tbb::parallel_for(tbb::blocked_range<int>(0, dh), [&](const tbb::blocked_range<int>& range) {
        for (int dy = range.begin(); dy < range.end(); ++dy) {
            const int sy = std::clamp(dy * 2, 0, std::max(0, sh - 2));
            Color* dst_row = dst.pixels_row(dy);
            for (int dx = 0; dx < dw; ++dx) {
                const int sx = std::clamp(dx * 2, 0, std::max(0, sw - 2));
                const Color& c00 = src.pixels_row(sy           )[sx           ];
                const Color& c10 = src.pixels_row(sy           )[std::min(sx + 1, sw - 1)];
                const Color& c01 = src.pixels_row(std::min(sy + 1, sh - 1))[sx           ];
                const Color& c11 = src.pixels_row(std::min(sy + 1, sh - 1))[std::min(sx + 1, sw - 1)];
                const float inv = 0.25f;
                dst_row[dx] = Color{
                    (c00.r + c10.r + c01.r + c11.r) * inv,
                    (c00.g + c10.g + c01.g + c11.g) * inv,
                    (c00.b + c10.b + c01.b + c11.b) * inv,
                    (c00.a + c10.a + c01.a + c11.a) * inv
                };
            }
        }
    });
}

/// Box-filter 4× downsample (average of 4×4 blocks)
static void downsample_4x(const Framebuffer& src, Framebuffer& dst) {
    const int sw = src.width(), sh = src.height();
    const int dw = dst.width(), dh = dst.height();
    tbb::parallel_for(tbb::blocked_range<int>(0, dh), [&](const tbb::blocked_range<int>& range) {
        for (int dy = range.begin(); dy < range.end(); ++dy) {
            const int sy = std::clamp(dy * 4, 0, std::max(0, sh - 4));
            Color* dst_row = dst.pixels_row(dy);
            for (int dx = 0; dx < dw; ++dx) {
                const int sx = std::clamp(dx * 4, 0, std::max(0, sw - 4));
                float sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
                int count = 0;
                for (int oy = 0; oy < 4; ++oy) {
                    const int py = std::min(sy + oy, sh - 1);
                    const Color* src_row = src.pixels_row(py);
                    for (int ox = 0; ox < 4; ++ox) {
                        const int px = std::min(sx + ox, sw - 1);
                        const Color& p = src_row[px];
                        sum_r += p.r; sum_g += p.g; sum_b += p.b; sum_a += p.a;
                        ++count;
                    }
                }
                const float inv = 1.0f / static_cast<float>(count);
                dst_row[dx] = Color{sum_r * inv, sum_g * inv, sum_b * inv, sum_a * inv};
            }
        }
    });
}

/// Bilinear upscale from small → full-res
static Color bilinear_sample(const Framebuffer& src, float fx, float fy) {
    const int sw = src.width(), sh = src.height();
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, sw - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, sh - 1);
    const int x1 = std::min(x0 + 1, sw - 1);
    const int y1 = std::min(y0 + 1, sh - 1);
    const float dx = fx - static_cast<float>(x0);
    const float dy = fy - static_cast<float>(y0);
    const Color& c00 = src.pixels_row(y0)[x0];
    const Color& c10 = src.pixels_row(y0)[x1];
    const Color& c01 = src.pixels_row(y1)[x0];
    const Color& c11 = src.pixels_row(y1)[x1];
    const float w00 = (1.0f - dx) * (1.0f - dy);
    const float w10 = dx * (1.0f - dy);
    const float w01 = (1.0f - dx) * dy;
    const float w11 = dx * dy;
    return Color{
        c00.r * w00 + c10.r * w10 + c01.r * w01 + c11.r * w11,
        c00.g * w00 + c10.g * w10 + c01.g * w01 + c11.g * w11,
        c00.b * w00 + c10.b * w10 + c01.b * w01 + c11.b * w11,
        c00.a * w00 + c10.a * w10 + c01.a * w01 + c11.a * w11
    };
}

static void upscale_bilinear(const Framebuffer& small, Framebuffer& fb) {
    const int dw = fb.width(), dh = fb.height();
    const int sw = small.width(), sh = small.height();
    const float scale_x = static_cast<float>(sw) / static_cast<float>(dw);
    const float scale_y = static_cast<float>(sh) / static_cast<float>(dh);
    tbb::parallel_for(tbb::blocked_range<int>(0, dh), [&](const tbb::blocked_range<int>& range) {
        for (int dy = range.begin(); dy < range.end(); ++dy) {
            const float fy = (static_cast<float>(dy) + 0.5f) * scale_y - 0.5f;
            Color* dst_row = fb.pixels_row(dy);
            for (int dx = 0; dx < dw; ++dx) {
                const float fx = (static_cast<float>(dx) + 0.5f) * scale_x - 0.5f;
                dst_row[dx] = bilinear_sample(small, fx, fy);
            }
        }
    });
}

/// Downsample-2× → blur at half-resolution → bilinear upscale.
/// The blur radius at half-res is radius / downsample_factor.
static void apply_downsample_blur(Framebuffer& fb, float radius, int downsample_factor) {
    if (radius <= 0.5f || downsample_factor <= 1) return;
    const int w = fb.width(), h = fb.height();
    const int sw = std::max(1, (w + downsample_factor - 1) / downsample_factor);
    const int sh = std::max(1, (h + downsample_factor - 1) / downsample_factor);

    // Step 1: Downsample
    auto small = std::make_shared<Framebuffer>(sw, sh);
    if (downsample_factor == 2)
        downsample_2x(fb, *small);
    else if (downsample_factor == 4)
        downsample_4x(fb, *small);
    else
        downsample_2x(fb, *small);

    // Step 2: Blur in downsampled space (3-pass for reference accuracy)
    const float scaled_radius = radius / static_cast<float>(downsample_factor);
    apply_3pass_blur(*small, std::max(1, static_cast<int>(std::round(scaled_radius))));

    // Step 3: Upscale back
    upscale_bilinear(*small, fb);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Test image generators
// ═══════════════════════════════════════════════════════════════════════════

/// Diagonal gradient + sharp circle + hard corner — exercises smooth + edge cases.
static void make_test_image(Framebuffer& fb) {
    const int w = fb.width(), h = fb.height();
    tbb::parallel_for(tbb::blocked_range<int>(0, h), [&](const tbb::blocked_range<int>& ry) {
        for (int y = ry.begin(); y < ry.end(); ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                float nx = static_cast<float>(x) / static_cast<float>(w);
                float ny = static_cast<float>(y) / static_cast<float>(h);
                float val = nx * 0.7f + ny * 0.3f;
                // Sharp circle in center
                if ((x - w/2) * (x - w/2) + (y - h/2) * (y - h/2) < (w/8) * (w/8))
                    val = 0.9f;
                // Sharp dark corner
                if (x > w * 3 / 4 && y > h * 3 / 4)
                    val = 0.1f;
                row[x] = Color{val, val * 0.8f, val * 0.5f, 1.0f};
            }
        }
    });
}

/// High-contrast checkerboard + thin lines — exercises high-frequency content.
static void make_checker_image(Framebuffer& fb) {
    const int w = fb.width(), h = fb.height();
    tbb::parallel_for(tbb::blocked_range<int>(0, h), [&](const tbb::blocked_range<int>& ry) {
        for (int y = ry.begin(); y < ry.end(); ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = 0; x < w; ++x) {
                // 8×8 checkerboard
                int cx = x / 8, cy = y / 8;
                float val = ((cx + cy) & 1) ? 1.0f : 0.0f;
                // Thin 1-pixel lines every 32 pixels
                if (x % 32 == 0 || y % 32 == 0)
                    val = (val > 0.5f) ? 0.0f : 1.0f;
                row[x] = Color{val, val * 0.5f, 1.0f - val, 1.0f};
            }
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Output helpers
// ═══════════════════════════════════════════════════════════════════════════

static bool save_ppm(const Framebuffer& fb, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", fb.width(), fb.height());
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            unsigned char rgb[3] = {
                static_cast<unsigned char>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
                static_cast<unsigned char>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                static_cast<unsigned char>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f)
            };
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}

/// Compute per-channel statistics between two framebuffers.
struct ComparisonResult {
    double rmse;           // Root-mean-square error (per-channel Euclidean)
    double max_channel_err; // Max absolute difference in any single channel
    double max_per_pixel_rms; // Max per-pixel RMS
    uint64_t diff_pixels;  // Pixels with any channel diff > 0.5%
    uint64_t total_pixels;
    double pct_diff;
    double psnr;           // Peak signal-to-noise ratio (dB)
};

static ComparisonResult compare(const Framebuffer& ref, const Framebuffer& test) {
    const int w = std::min(ref.width(), test.width());
    const int h = std::min(ref.height(), test.height());
    double sum_sq = 0.0;
    double max_ch_err = 0.0;
    double max_pp_rms = 0.0;
    uint64_t diff = 0;
    const uint64_t total = static_cast<uint64_t>(w) * h;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Color ca = ref.get_pixel(x, y);
            Color cb = test.get_pixel(x, y);
            double dr = static_cast<double>(ca.r) - static_cast<double>(cb.r);
            double dg = static_cast<double>(ca.g) - static_cast<double>(cb.g);
            double db = static_cast<double>(ca.b) - static_cast<double>(cb.b);
            double err_sq = dr*dr + dg*dg + db*db;
            sum_sq += err_sq;
            double pp_rms = std::sqrt(err_sq);
            if (pp_rms > max_pp_rms) max_pp_rms = pp_rms;
            double max_c = std::max({std::abs(dr), std::abs(dg), std::abs(db)});
            if (max_c > max_ch_err) max_ch_err = max_c;
            if (max_c > 0.005) diff++;  // >0.5% per-channel
        }
    }

    double mse = sum_sq / static_cast<double>(total);
    double rmse = std::sqrt(mse);
    double psnr = (mse > 0.0) ? 20.0 * std::log10(1.0 / std::sqrt(mse)) : 99.9;

    return {
        rmse, max_ch_err, max_pp_rms,
        diff, total,
        100.0 * static_cast<double>(diff) / static_cast<double>(total),
        psnr
    };
}

/// Generate an error heatmap: red = large error, green = no error.
static void make_error_heatmap(const Framebuffer& ref, const Framebuffer& test, Framebuffer& out) {
    const int w = std::min({ref.width(), test.width(), out.width()});
    const int h = std::min({ref.height(), test.height(), out.height()});
    for (int y = 0; y < h; ++y) {
        const Color* ref_row = ref.pixels_row(y);
        const Color* test_row = test.pixels_row(y);
        Color* out_row = out.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            float max_c = std::max({
                std::abs(ref_row[x].r - test_row[x].r),
                std::abs(ref_row[x].g - test_row[x].g),
                std::abs(ref_row[x].b - test_row[x].b)
            });
            float e = std::clamp(max_c * 5.0f, 0.0f, 1.0f);
            out_row[x] = Color{e, 1.0f - e, 0.0f, 1.0f};
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Main
// ═══════════════════════════════════════════════════════════════════════════

static void run_test(const char* label,
                     const Framebuffer& original,
                     float radius,
                     int downsample_factor,
                     bool use_checker) {
    printf("\n──────────────────────────────────────────────────────────────\n");
    printf("  TEST: %s\n", label);
    printf("  Size: %dx%d  Radius: %.1f  Downsample: %d×\n",
           original.width(), original.height(), radius, downsample_factor);
    printf("──────────────────────────────────────────────────────────────\n");

    const char* suffix = use_checker ? "checker" : "gradient";

    // Full-res reference (3-pass)
    auto ref = std::make_shared<Framebuffer>(original);
    auto t0 = std::chrono::steady_clock::now();
    apply_3pass_blur(*ref, static_cast<int>(std::round(radius)));
    auto t1 = std::chrono::steady_clock::now();
    double ref_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Downsample path
    auto ds = std::make_shared<Framebuffer>(original);
    auto t2 = std::chrono::steady_clock::now();
    apply_downsample_blur(*ds, radius, downsample_factor);
    auto t3 = std::chrono::steady_clock::now();
    double ds_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // Save outputs
    char path_ref[128], path_ds[128], path_err[128], path_orig[128];
    std::snprintf(path_orig, sizeof(path_orig), "/tmp/blur_%s_orig.ppm", suffix);
    std::snprintf(path_ref,  sizeof(path_ref),  "/tmp/blur_%s_fullres_r%.0f.ppm", suffix, radius);
    std::snprintf(path_ds,   sizeof(path_ds),   "/tmp/blur_%s_ds%d_r%.0f.ppm", suffix, downsample_factor, radius);
    std::snprintf(path_err,  sizeof(path_err),  "/tmp/blur_%s_error_r%.0f.ppm", suffix, radius);

    save_ppm(original, path_orig);
    save_ppm(*ref, path_ref);
    save_ppm(*ds, path_ds);

    // Error heatmap
    auto error_fb = std::make_shared<Framebuffer>(original.width(), original.height());
    make_error_heatmap(*ref, *ds, *error_fb);
    save_ppm(*error_fb, path_err);

    // Comparison
    auto cr = compare(*ref, *ds);

    printf("\n  Timing:\n");
    printf("    Full-res:     %7.2fms\n", ref_ms);
    printf("    Downsample:   %7.2fms  (%s)\n", ds_ms, ds_ms < ref_ms ? "FASTER" : "slower");
    printf("    Speedup:      %7.2f×\n", ref_ms / ds_ms);

    printf("\n  Pixel diff (full-res vs downsample):\n");
    printf("    RMSE:         %9.6f  (0..√3 range)\n", cr.rmse);
    printf("    PSNR:         %9.2f dB\n", cr.psnr);
    printf("    Max chan err: %9.6f  (0..1 range)\n", cr.max_channel_err);
    printf("    Max pp RMS:   %9.6f\n", cr.max_per_pixel_rms);
    printf("    Pixels >0.5%%: %llu / %llu  (%6.2f%%)\n",
           (unsigned long long)cr.diff_pixels,
           (unsigned long long)cr.total_pixels, cr.pct_diff);

    printf("\n  Verdict: ");
    if (cr.psnr > 40.0 && cr.pct_diff < 5.0) {
        printf("✅ PASS — visually equivalent (PSNR=%.1fdB, %.2f%% differ)\n", cr.psnr, cr.pct_diff);
    } else if (cr.psnr > 30.0 && cr.pct_diff < 10.0) {
        printf("⚠️  MINOR — acceptable for motion graphics (PSNR=%.1fdB, %.2f%% differ)\n", cr.psnr, cr.pct_diff);
    } else {
        printf("❌ FAIL — significant difference (PSNR=%.1fdB, %.2f%% differ)\n", cr.psnr, cr.pct_diff);
    }

    printf("\n  Files:\n");
    printf("    Original:   %s\n", path_orig);
    printf("    Full-res:   %s\n", path_ref);
    printf("    Downsample: %s\n", path_ds);
    printf("    Error map:  %s\n", path_err);
}

int main(int argc, char** argv) {
    int W = 640, H = 360;
    float RADIUS = 12.0f;
    int DS_FACTOR = 2;

    if (argc >= 3) { W = std::atoi(argv[1]); H = std::atoi(argv[2]); }
    if (argc >= 4) { RADIUS = std::atof(argv[3]); }
    if (argc >= 5) { DS_FACTOR = std::atoi(argv[4]); }

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║        Blur Downsample Verification                         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\nBenchmark: full-res 3-pass box blur vs %d× downsample→blur→upscale\n\n", DS_FACTOR);

    // Test 1: gradient + sharp edges (lower frequencies)
    {
        auto fb = std::make_shared<Framebuffer>(W, H);
        make_test_image(*fb);
        run_test("Gradient + sharp edges", *fb, RADIUS, DS_FACTOR, false);
    }

    // Test 2: checkerboard + thin lines (high frequencies — worst case for downsample)
    {
        auto fb = std::make_shared<Framebuffer>(W, H);
        make_checker_image(*fb);
        run_test("Checkerboard + thin lines (worst case)", *fb, RADIUS, DS_FACTOR, true);
    }

    // Test 3: multiple radii on gradient image
    for (float r : {6.0f, 12.0f, 20.0f, 30.0f}) {
        auto fb = std::make_shared<Framebuffer>(W, H);
        make_test_image(*fb);
        int df = (r <= 10.0f) ? 2 : (r <= 24.0f ? 2 : 4);
        char label[64];
        std::snprintf(label, sizeof(label), "Gradient @ radius %.0f (%d× ds)", r, df);
        run_test(label, *fb, r, df, false);
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║    2-Pass vs 3-Pass Box Blur Comparison                      ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\nComparing 3-pass (H→V→H) vs 2-pass (H→V with equivalent radius)\n");
    printf("The equivalent radius is chosen so the total Gaussian variance matches.\n\n");

    for (int r : {4, 8, 12, 16, 24, 50, 100}) {
        int r2 = two_pass_equivalent_radius(r);
        printf("  Radius %3d → 2-pass equiv radius %3d (%.1f%% radius change, ", r, r2,
               100.0 * static_cast<double>(r2 - r) / static_cast<double>(r));
        // Passes: 3 passes × (2r+1) work per pass vs 2 passes × (2r₂+1)
        double work_3 = 3.0 * (2.0 * r + 1.0);
        double work_2 = 2.0 * (2.0 * r2 + 1.0);
        printf("work ratio %.1f%%)\n", 100.0 * work_2 / work_3);
    }

    // Actual visual comparison: 3-pass vs 2-pass at various radii
    printf("\n");
    for (int r : {4, 8, 12, 16, 24}) {
        auto fb = std::make_shared<Framebuffer>(W, H);
        make_test_image(*fb);

        // 3-pass reference
        auto ref = std::make_shared<Framebuffer>(*fb);
        auto t0 = std::chrono::steady_clock::now();
        apply_3pass_blur(*ref, r);
        auto t1 = std::chrono::steady_clock::now();
        double t3_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 2-pass with equivalent radius
        auto two = std::make_shared<Framebuffer>(*fb);
        auto t2 = std::chrono::steady_clock::now();
        apply_2pass_blur(*two, r);
        auto t3 = std::chrono::steady_clock::now();
        double t2_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

        auto cr = compare(*ref, *two);
        int r2 = two_pass_equivalent_radius(r);

        printf("  Radius %3d (equiv %3d): 3-pass=%6.2fms  2-pass=%6.2fms  (%.2f×)  PSNR=%.1fdB  RMSE=%.6f  >0.5%%=%5.2f%%\n",
               r, r2, t3_ms, t2_ms, t3_ms / t2_ms,
               cr.psnr, cr.rmse, cr.pct_diff);
    }

    // Checkerboard worst case at radius 12
    {
        auto fb = std::make_shared<Framebuffer>(W, H);
        make_checker_image(*fb);

        auto ref = std::make_shared<Framebuffer>(*fb);
        apply_3pass_blur(*ref, 12);

        auto two = std::make_shared<Framebuffer>(*fb);
        apply_2pass_blur(*two, 12);

        auto cr = compare(*ref, *two);
        printf("\n  Checkerboard r=12: PSNR=%.1fdB  RMSE=%.6f  >0.5%%=%5.2f%%\n",
               cr.psnr, cr.rmse, cr.pct_diff);

        // Save error heatmap
        auto err = std::make_shared<Framebuffer>(fb->width(), fb->height());
        make_error_heatmap(*ref, *two, *err);
        save_ppm(*ref, "/tmp/blur_3pass_checker_r12.ppm");
        save_ppm(*two, "/tmp/blur_2pass_checker_r12.ppm");
        save_ppm(*err, "/tmp/blur_2v3_error_checker_r12.ppm");
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf("  Done. View PPM files in /tmp/\n");
    printf("══════════════════════════════════════════════════════════════\n");
    return 0;
}
