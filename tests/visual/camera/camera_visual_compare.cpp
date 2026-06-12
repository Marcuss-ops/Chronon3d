#include "camera_visual_compare.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <tests/helpers/test_utils.hpp>
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace chronon3d::test {

ImageDiffResult compare_framebuffers(const Framebuffer& rendered,
                                      const Framebuffer& golden,
                                      const ImageDiffThreshold& threshold) {
    ImageDiffResult res;
    if (rendered.width() != golden.width() || rendered.height() != golden.height()) {
        res.passed = false;
        res.report = "Dimension mismatch: " + std::to_string(rendered.width()) + "x" +
                     std::to_string(rendered.height()) + " vs " +
                     std::to_string(golden.width()) + "x" + std::to_string(golden.height());
        return res;
    }

    double total_err = 0.0;
    int mismatched = 0;
    const int total = rendered.width() * rendered.height();

    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            const Color a = rendered.get_pixel(x, y).to_srgb();
            const Color b = golden.get_pixel(x, y);
            const float dr = std::abs(a.r - b.r);
            const float dg = std::abs(a.g - b.g);
            const float db = std::abs(a.b - b.b);
            const float da = std::abs(a.a - b.a);
            const float max_d = std::max({dr, dg, db, da});
            res.max_abs_error = std::max(res.max_abs_error, static_cast<double>(max_d));
            total_err += dr + dg + db + da;
            if (max_d > threshold.max_abs_error) {
                ++mismatched;
            }
        }
    }

    res.mean_abs_error = total_err / (total * 4);
    res.changed_pixel_ratio = static_cast<double>(mismatched) / static_cast<double>(total);

    res.passed = res.mean_abs_error <= threshold.max_mean_abs_error &&
                 res.max_abs_error <= threshold.max_abs_error &&
                 res.changed_pixel_ratio <= threshold.max_changed_pixel_ratio;

    if (!res.passed) {
        res.report = "mean_abs_error=" + std::to_string(res.mean_abs_error * 255.0) +
                     "/255 max_abs_error=" + std::to_string(res.max_abs_error * 255.0) +
                     "/255 changed_ratio=" + std::to_string(res.changed_pixel_ratio * 100.0) + "%";
    }

    return res;
}

void verify_golden_or_create(
    const Framebuffer& rendered,
    const std::string& case_name,
    const std::filesystem::path& golden_dir,
    const std::filesystem::path& artifact_dir,
    const ImageDiffThreshold& threshold) {
    std::filesystem::create_directories(golden_dir);
    std::filesystem::create_directories(artifact_dir / case_name);

    const std::filesystem::path golden_path = golden_dir / (case_name + ".png");

    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(rendered, golden_path.string()));
        MESSAGE("Golden image created: " << golden_path.string());
        return;
    }

    auto golden = load_png_as_framebuffer(golden_path.string());
    REQUIRE(golden != nullptr);

    const auto diff = compare_framebuffers(rendered, *golden, threshold);
    if (!diff.passed) {
        // Save actual
        save_png(rendered, (artifact_dir / case_name / "actual.png").string());
        // Save expected copy
        save_png(*golden, (artifact_dir / case_name / "expected.png").string());
        // Save diff image
        Framebuffer diff_fb(rendered.width(), rendered.height());
        for (int y = 0; y < rendered.height(); ++y) {
            for (int x = 0; x < rendered.width(); ++x) {
                const Color a = rendered.get_pixel(x, y).to_srgb();
                const Color b = golden->get_pixel(x, y);
                const float dr = std::abs(a.r - b.r);
                const float dg = std::abs(a.g - b.g);
                const float db = std::abs(a.b - b.b);
                const float max_d = std::max({dr, dg, db});
                if (max_d > threshold.max_abs_error) {
                    diff_fb.set_pixel(x, y, Color::red());
                } else {
                    diff_fb.set_pixel(x, y, Color{0,0,0,0.2f});
                }
            }
        }
        save_png(diff_fb, (artifact_dir / case_name / "diff.png").string());
        // Save report
        std::ofstream report((artifact_dir / case_name / "report.txt").string());
        report << "case: " << case_name << "\n";
        report << "resolution: " << rendered.width() << "x" << rendered.height() << "\n";
        report << "mean_abs_error: " << diff.mean_abs_error * 255.0 << "\n";
        report << "max_abs_error: " << diff.max_abs_error * 255.0 << "\n";
        report << "changed_pixel_ratio: " << diff.changed_pixel_ratio << "\n";
        report << "threshold_mean_abs_error: " << threshold.max_mean_abs_error * 255.0 << "\n";
        report << "threshold_max_abs_error: " << threshold.max_abs_error * 255.0 << "\n";
        report << "threshold_changed_pixel_ratio: " << threshold.max_changed_pixel_ratio << "\n";
        report << "result: failed\n";
    }
    INFO(diff.report);
    CHECK(diff.passed);
}

} // namespace chronon3d::test
