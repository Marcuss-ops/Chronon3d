#pragma once

#include "image_diff.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>

#include <filesystem>
#include <string>
#include <fstream>

namespace chronon3d::test {

// ── GoldenTestMode ──────────────────────────────────────────────────────────

enum class GoldenMode {
    Verify,   // Compare against existing golden; fail if different or missing.
    Update    // Overwrite or create golden from current render output.
};

// ── GoldenTestConfig ────────────────────────────────────────────────────────

struct GoldenTestConfig {
    std::filesystem::path golden_directory;
    std::filesystem::path artifact_directory;
    ImageDiffThreshold    threshold{};
    GoldenMode            mode{GoldenMode::Verify};
};

// ── GoldenTestResult ────────────────────────────────────────────────────────

struct GoldenTestResult {
    bool passed{false};
    bool golden_missing{false};
    bool golden_updated{false};

    std::filesystem::path golden_path;
    std::filesystem::path actual_path;
    std::filesystem::path expected_path;
    std::filesystem::path diff_path;
    std::filesystem::path report_path;

    ImageDiffResult diff;
    std::string     message;
};

// ── golden_mode_from_environment ────────────────────────────────────────────
// Read CHRONON3D_UPDATE_GOLDENS from the environment.
//
// Accepted truthy values (case-insensitive):
//   1, true, on, yes
//
// Returns GoldenMode::Update when set, GoldenMode::Verify otherwise.

GoldenMode golden_mode_from_environment();

// ── verify_golden ──────────────────────────────────────────────────────────
// Compare a rendered framebuffer against a golden PNG, or update/create the
// golden when in Update mode.
//
// In Verify mode:
//   - Golden present + matches → pass
//   - Golden present + differs → fail + artifacts saved
//   - Golden missing           → fail
//
// In Update mode:
//   - Golden present           → overwritten
//   - Golden missing           → created

GoldenTestResult verify_golden(
    const Framebuffer& rendered,
    std::string_view case_name,
    const GoldenTestConfig& config
);

// ── Sanitise case name ────────────────────────────────────────────────────
// Replace characters that are unsafe in file names:
//   "/" → "_"   "../" → "parent_"
// Also strips leading/trailing whitespace.

std::string sanitise_case_name(std::string_view name);

} // namespace chronon3d::test
