#include "image_diff.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

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

    res.passed =
        res.mean_abs_error <= threshold.max_mean_abs_error &&
        res.max_abs_error <= threshold.max_abs_error &&
        res.changed_pixel_ratio <= threshold.max_changed_pixel_ratio &&
        res.rmse <= threshold.max_rmse;

    if (!res.passed) {
        std::ostringstream ss;
        ss << "mean_abs_error=" << (res.mean_abs_error * 255.0) << "/255"
           << " max_abs_error=" << (res.max_abs_error * 255.0) << "/255"
           << " changed_ratio=" << (res.changed_pixel_ratio * 100.0) << "%"
           << " rmse=" << (res.rmse * 255.0) << "/255"
           << " psnr=" << res.psnr << " dB";
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

} // namespace chronon3d::test
