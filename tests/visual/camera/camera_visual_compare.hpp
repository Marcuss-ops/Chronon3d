#pragma once

#include "../support/image_diff.hpp"
#include "../support/golden_test.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <string>
#include <filesystem>

namespace chronon3d::test {

// Legacy convenience wrapper — delegates to the new golden_test framework.
// Camera visual tests can continue using this for a smooth migration.
// New visual tests (blend, matte, etc.) should use golden_test directly.

inline GoldenTestConfig camera_golden_config(
    const std::filesystem::path& golden_dir,
    const std::filesystem::path& artifact_dir,
    const ImageDiffThreshold& threshold = {})
{
    GoldenTestConfig config;
    config.golden_directory = golden_dir;
    config.artifact_directory = artifact_dir;
    config.threshold = threshold;
    config.mode = golden_mode_from_environment();
    return config;
}

// verify_golden_or_create is replaced by verify_golden().
// Legacy callers should use:
//   verify_golden(rendered, case_name,
//       camera_golden_config(golden_dir, artifact_dir, threshold));

} // namespace chronon3d::test
