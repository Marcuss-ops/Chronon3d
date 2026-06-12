#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <string>
#include <filesystem>

namespace chronon3d::test {

struct ImageDiffResult {
    double mean_abs_error{0.0};
    double max_abs_error{0.0};
    double changed_pixel_ratio{0.0};
    bool passed{true};
    std::string report;
};

struct ImageDiffThreshold {
    double max_mean_abs_error{1.5 / 255.0};   // ~0.00588 in [0,1]
    double max_abs_error{18.0 / 255.0};       // ~0.0706 in [0,1]
    double max_changed_pixel_ratio{0.015};    // 1.5%
};

ImageDiffResult compare_framebuffers(const Framebuffer& rendered,
                                      const Framebuffer& golden,
                                      const ImageDiffThreshold& threshold = {});

// Verify against golden, or create golden if missing.
// On mismatch, saves actual/diff/expected to artifact_dir.
void verify_golden_or_create(
    const Framebuffer& rendered,
    const std::string& case_name,
    const std::filesystem::path& golden_dir,
    const std::filesystem::path& artifact_dir,
    const ImageDiffThreshold& threshold = {});

} // namespace chronon3d::test
