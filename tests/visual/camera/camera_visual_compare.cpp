#include "camera_visual_compare.hpp"

#include <doctest/doctest.h>


namespace chronon3d::test {

// The implementation is now in image_diff.cpp and golden_test.cpp in
// tests/visual/support/.  This file remains as a forwarder so that
// camera_visual_tests.cpp does not need to change its includes yet.

// Legacy function kept for backward compatibility during migration.
void verify_golden_or_create(
    const Framebuffer& rendered,
    const std::string& case_name,
    const std::filesystem::path& golden_dir,
    const std::filesystem::path& artifact_dir,
    const ImageDiffThreshold& threshold)
{
    auto config = camera_golden_config(golden_dir, artifact_dir, threshold);
    auto result = verify_golden(rendered, case_name, config);

    REQUIRE_FALSE(result.golden_missing);
    INFO(result.message);
    REQUIRE_FALSE(result.golden_missing);
    CHECK(result.passed);
}

} // namespace chronon3d::test
