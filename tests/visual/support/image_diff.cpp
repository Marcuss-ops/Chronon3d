#include "image_diff.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

namespace chronon3d::test {

ImageDiffResult compare_framebuffers(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const ImageDiffThreshold& threshold)
{
    ImageDiffResult res;

    // ── Dimension check ───────────────────────────────────────────────────
    if (actual.width() != expected.width() || actual.height() != expected.height()) {
        res.passed = false;
        std::ostringstream ss;
        ss << "Dimension mismatch: actual " << actual.width() << "x" << actual.height()
           << " vs expected " << expected.width() << "x" << expected.height();
        res.report = ss.str();
        return res;
    }

    const int W = actual.width();
    const int H = actual.height();
    const int total = W * H;

    double sum_sq_err = 0.0;
    double total_abs_err = 0.0;
    int mismatched = 0;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            // Actual is linear float → convert to sRGB for RGB comparison.
            // Alpha is compared in linear space (preserved by to_srgb() as-is).
            const Color actual_srgb = actual.get_pixel(x, y).to_srgb();
            const Color expected_srgb = expected.get_pixel(x, y);

            const double dr = static_cast<double>(std::abs(actual_srgb.r - expected_srgb.r));
            const double dg = static_cast<double>(std::abs(actual_srgb.g - expected_srgb.g));
            const double db = static_cast<double>(std::abs(actual_srgb.b - expected_srgb.b));
            const double da = static_cast<double>(std::abs(actual_srgb.a - expected_srgb.a));

            const double max_d = std::max({dr, dg, db, da});
            res.max_abs_error = std::max(res.max_abs_error, max_d);

            total_abs_err += dr + dg + db + da;
            sum_sq_err += dr * dr + dg * dg + db * db + da * da;

            if (max_d > threshold.max_abs_error) {
                ++mismatched;
            }
        }
    }

    res.mean_abs_error = total_abs_err / (static_cast<double>(total) * 4.0);
    res.changed_pixel_ratio = static_cast<double>(mismatched) / static_cast<double>(total);
    res.rmse = std::sqrt(sum_sq_err / (static_cast<double>(total) * 4.0));

    // PSNR: peak signal (1.0 in sRGB) over MSE.
    // Infinite when MSE is zero (identical images).
    const double mse = sum_sq_err / (static_cast<double>(total) * 4.0);
    res.psnr = (mse > 0.0) ? 20.0 * std::log10(1.0 / std::sqrt(mse)) : std::numeric_limits<double>::infinity();

    // ── SSIM ──────────────────────────────────────────────────────────────
    res.ssim = compute_ssim(actual, expected);

    res.passed =
        res.mean_abs_error <= threshold.max_mean_abs_error &&
        res.max_abs_error <= threshold.max_abs_error &&
        res.changed_pixel_ratio <= threshold.max_changed_pixel_ratio &&
        res.rmse <= threshold.max_rmse &&
        res.ssim >= threshold.min_ssim;

    if (!res.passed) {
        std::ostringstream ss;
        ss << "mean_abs_error=" << (res.mean_abs_error * 255.0) << "/255"
           << " max_abs_error=" << (res.max_abs_error * 255.0) << "/255"
           << " changed_ratio=" << (res.changed_pixel_ratio * 100.0) << "%"
           << " rmse=" << (res.rmse * 255.0) << "/255"
           << " psnr=" << res.psnr << " dB"
           << " ssim=" << res.ssim;
        res.report = ss.str();
    }

    return res;
}

Framebuffer create_diff_heatmap(
    const Framebuffer& actual,
    const Framebuffer& expected,
    const ImageDiffThreshold& threshold)
{
    const int W = std::min(actual.width(), expected.width());
    const int H = std::min(actual.height(), expected.height());

    Framebuffer diff(W, H);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Color actual_srgb = actual.get_pixel(x, y).to_srgb();
            const Color expected_srgb = expected.get_pixel(x, y);

            const float dr = std::abs(actual_srgb.r - expected_srgb.r);
            const float dg = std::abs(actual_srgb.g - expected_srgb.g);
            const float db = std::abs(actual_srgb.b - expected_srgb.b);
            const float max_d = std::max({dr, dg, db});

            if (max_d > static_cast<float>(threshold.max_abs_error)) {
                // Error highlight: red intensity proportional to error.
                const float intensity = std::min(1.0f, max_d * 8.0f);
                diff.set_pixel(x, y, Color{intensity, 0.0f, 0.0f, 0.8f});
            } else {
                // Matching pixel: dim semi-transparent grey.
                diff.set_pixel(x, y, Color{0.0f, 0.0f, 0.0f, 0.15f});
            }
        }
    }

    return diff;
}

// ── compute_ssim (Structural Similarity Index) ──────────────────────────────
// 8×8 non-overlapping blocks on luminance (luma).  Standard constants
// C1 = (K1)², C2 = (K2)² with K1=0.01, K2=0.03, L=1.0.

double compute_ssim(
    const Framebuffer& a_fb,
    const Framebuffer& b_fb)
{
    const int W = a_fb.width();
    const int H = a_fb.height();

    if (W != b_fb.width() || H != b_fb.height()) return 0.0;

    // Block size.
    constexpr int BS = 8;
    const int blocksX = W / BS;
    const int blocksY = H / BS;
    if (blocksX == 0 || blocksY == 0) return 1.0;  // too small; declare match

    // Pre-compute luminance buffers (float in [0,1]).
    // Use BT.709 luminance coefficients (0.2126 R + 0.7152 G + 0.0722 B).
    auto lum = [](const Color& c) -> float {
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    };

    std::vector<float> la(static_cast<size_t>(W) * H);
    std::vector<float> lb(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Color ca = a_fb.get_pixel(x, y).to_srgb();
            const Color cb = b_fb.get_pixel(x, y);
            la[y * W + x] = lum(ca);
            lb[y * W + x] = lum(cb);
        }
    }

    constexpr double C1 = 0.0001;  // (0.01)²
    constexpr double C2 = 0.0009;  // (0.03)²
    constexpr int N = BS * BS;     // 64

    double ssim_sum = 0.0;
    int    block_count = 0;

    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const int x0 = bx * BS;
            const int y0 = by * BS;

            // First pass: compute means.
            double sum_a = 0.0, sum_b = 0.0;
            for (int dy = 0; dy < BS; ++dy) {
                for (int dx = 0; dx < BS; ++dx) {
                    sum_a += la[(y0 + dy) * W + (x0 + dx)];
                    sum_b += lb[(y0 + dy) * W + (x0 + dx)];
                }
            }
            const double mu_a = sum_a / N;
            const double mu_b = sum_b / N;

            // Second pass: compute variances and covariance.
            double var_a = 0.0, var_b = 0.0, cov_ab = 0.0;
            for (int dy = 0; dy < BS; ++dy) {
                for (int dx = 0; dx < BS; ++dx) {
                    const double da = la[(y0 + dy) * W + (x0 + dx)] - mu_a;
                    const double db = lb[(y0 + dy) * W + (x0 + dx)] - mu_b;
                    var_a += da * da;
                    var_b += db * db;
                    cov_ab += da * db;
                }
            }
            var_a /= (N - 1);
            var_b /= (N - 1);
            cov_ab /= (N - 1);

            // SSIM per block.
            const double num = (2.0 * mu_a * mu_b + C1) * (2.0 * cov_ab + C2);
            const double den = (mu_a * mu_a + mu_b * mu_b + C1) * (var_a + var_b + C2);
            const double block_ssim = (den > 0.0) ? num / den : 1.0;

            ssim_sum += block_ssim;
            ++block_count;
        }
    }

    return (block_count > 0) ? ssim_sum / block_count : 1.0;
}

} // namespace chronon3d::test
