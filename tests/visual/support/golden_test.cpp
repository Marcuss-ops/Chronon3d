#include "golden_test.hpp"

#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <system_error>
using namespace chronon3d;

namespace chronon3d::test {

GoldenMode golden_mode_from_environment() {
    const char* env = std::getenv("CHRONON3D_UPDATE_GOLDENS");
    if (!env) return GoldenMode::Verify;

    std::string val(env);
    // Convert to lowercase for case-insensitive check.
    for (auto& c : val) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (val == "1" || val == "true" || val == "on" || val == "yes") {
        return GoldenMode::Update;
    }
    return GoldenMode::Verify;
}

std::string sanitise_case_name(std::string_view name) {
    std::string out;
    out.reserve(name.size());

    // Strip leading whitespace.
    size_t start = 0;
    while (start < name.size() && std::isspace(static_cast<unsigned char>(name[start]))) ++start;

    // Strip trailing whitespace.
    size_t end = name.size();
    while (end > start && std::isspace(static_cast<unsigned char>(name[end - 1]))) --end;

    for (size_t i = start; i < end; ++i) {
        char c = name[i];
        // Replace path separators and parent-directory sequences.
        if (c == '/' || c == '\\') {
            out += '_';
        } else if (c == '.' && i + 2 < end && name[i+1] == '.' && name[i+2] == '/') {
            out += "parent_";
            i += 2; // skip the ".."
        } else {
            out += c;
        }
    }
    return out;
}

GoldenTestResult verify_golden(
    const Framebuffer& rendered,
    std::string_view case_name,
    const GoldenTestConfig& config)
{
    GoldenTestResult res;
    const std::string safe_name = sanitise_case_name(case_name);

    res.golden_path = config.golden_directory / (safe_name + ".png");

    // Ensure directories exist.
    std::error_code ec;
    std::filesystem::create_directories(config.golden_directory, ec);
    std::filesystem::create_directories(config.artifact_directory / safe_name, ec);

    // ── Update mode ───────────────────────────────────────────────────────
    if (config.mode == GoldenMode::Update) {
        // Atomic write: write to a .tmp file first, then rename.
        const auto tmp_path = res.golden_path.string() + ".tmp";
        if (!save_png(rendered, tmp_path)) {
            res.passed = false;
            res.message = "Failed to save golden PNG (tmp): " + tmp_path;
            return res;
        }
        std::filesystem::rename(tmp_path, res.golden_path, ec);
        if (ec) {
            res.passed = false;
            res.message = "Failed to rename golden tmp: " + ec.message();
            return res;
        }
        res.golden_updated = true;
        res.passed = true;
        res.message = "Golden updated: " + res.golden_path.string();
        return res;
    }

    // ── Verify mode ───────────────────────────────────────────────────────
    if (!std::filesystem::exists(res.golden_path)) {
        res.passed = false;
        res.golden_missing = true;
        res.message = "Golden missing: " + res.golden_path.string();
        return res;
    }

    // Load golden PNG as sRGB framebuffer.
    auto golden_fb = load_png_as_framebuffer(res.golden_path.string());
    if (!golden_fb) {
        res.passed = false;
        res.message = "Failed to load golden: " + res.golden_path.string();
        return res;
    }

    // Compare.
    res.diff = compare_framebuffers(rendered, *golden_fb, config.threshold);
    res.passed = res.diff.passed;

    if (!res.passed) {
        // Save actual PNG.
        res.actual_path = config.artifact_directory / safe_name / "actual.png";
        save_png(rendered, res.actual_path.string());

        // Save expected (golden copy) PNG.
        res.expected_path = config.artifact_directory / safe_name / "expected.png";
        save_png(*golden_fb, res.expected_path.string());

        // Save diff heatmap.
        res.diff_path = config.artifact_directory / safe_name / "diff.png";
        auto heatmap = create_diff_heatmap(rendered, *golden_fb, config.threshold);
        save_png(heatmap, res.diff_path.string());

        // Save report text.
        res.report_path = config.artifact_directory / safe_name / "report.txt";
        std::ofstream report(res.report_path);
        if (report) {
            report << "case: " << case_name << "\n";
            report << "resolution: " << rendered.width() << "x" << rendered.height() << "\n";
            report << "mean_abs_error: " << (res.diff.mean_abs_error * 255.0) << "\n";
            report << "max_abs_error: " << (res.diff.max_abs_error * 255.0) << "\n";
            report << "changed_pixel_ratio: " << res.diff.changed_pixel_ratio << "\n";
            report << "rmse: " << (res.diff.rmse * 255.0) << "\n";
            report << "psnr: " << res.diff.psnr << "\n";
            report << "threshold_mean_abs_error: " << (config.threshold.max_mean_abs_error * 255.0) << "\n";
            report << "threshold_max_abs_error: " << (config.threshold.max_abs_error * 255.0) << "\n";
            report << "threshold_changed_pixel_ratio: " << config.threshold.max_changed_pixel_ratio << "\n";
            report << "threshold_rmse: " << (config.threshold.max_rmse * 255.0) << "\n";
            report << "result: failed\n";
        }

        res.message = res.diff.report;
    }

    return res;
}

} // namespace chronon3d::test
