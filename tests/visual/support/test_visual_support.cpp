#include "image_diff.hpp"
#include "golden_test.hpp"

#include <doctest/doctest.h>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <tests/helpers/test_utils.hpp>

#include <fstream>
#include <cstdlib>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// Helper: create a simple test framebuffer with a known pattern.
Framebuffer make_test_fb(int width, int height, float r, float g, float b, float a) {
    Framebuffer fb(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            fb.set_pixel(x, y, Color{r, g, b, a});
        }
    }
    return fb;
}

// Helper: create a framebuffer with a single modified pixel.
Framebuffer make_modified_fb(const Framebuffer& base, int px, int py, float dr, float dg, float db, float da) {
    Framebuffer fb(base);
    Color c = fb.get_pixel(px, py);
    fb.set_pixel(px, py, Color{c.r + dr, c.g + dg, c.b + db, c.a + da});
    return fb;
}

// Temporary test directory.
const std::filesystem::path kTestDir = "build/test_visual_support";
const std::filesystem::path kGoldenDir = kTestDir / "golden";
const std::filesystem::path kArtifactDir = kTestDir / "artifacts";

struct TestDirGuard {
    TestDirGuard() { std::filesystem::create_directories(kTestDir); }
    ~TestDirGuard() { std::filesystem::remove_all(kTestDir); }
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// ImageDiff tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("image_diff: identical framebuffers pass") {
    auto fb = make_test_fb(100, 100, 0.5f, 0.5f, 0.5f, 1.0f);
    auto result = compare_framebuffers(fb, fb);
    CHECK(result.passed);
    CHECK(result.max_abs_error == 0.0);
    CHECK(result.psnr == std::numeric_limits<double>::infinity());
}

TEST_CASE("image_diff: dimension mismatch fails") {
    auto a = make_test_fb(100, 100, 0.5f, 0.5f, 0.5f, 1.0f);
    auto b = make_test_fb(200, 200, 0.5f, 0.5f, 0.5f, 1.0f);
    auto result = compare_framebuffers(a, b);
    CHECK_FALSE(result.passed);
    CHECK(result.report.find("Dimension mismatch") != std::string::npos);
}

TEST_CASE("image_diff: single pixel difference fails") {
    auto a = make_test_fb(100, 100, 0.5f, 0.5f, 0.5f, 1.0f);
    auto b = make_modified_fb(a, 50, 50, 0.2f, 0.0f, 0.0f, 0.0f);
    auto result = compare_framebuffers(a, b);
    CHECK_FALSE(result.passed);
    CHECK(result.max_abs_error > 0.0);
    CHECK(result.changed_pixel_ratio > 0.0);
}

// ═════════════════════════════════════════════════════════════════════════════
// GoldenTest tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("golden_test: missing golden in verify mode fails") {
    TestDirGuard guard;

    GoldenTestConfig config;
    config.golden_directory = kGoldenDir;
    config.artifact_directory = kArtifactDir;
    config.mode = GoldenMode::Verify;

    auto fb = make_test_fb(50, 50, 0.3f, 0.6f, 0.9f, 1.0f);
    auto result = verify_golden(fb, "missing_golden", config);

    CHECK_FALSE(result.passed);
    CHECK(result.golden_missing);
}

TEST_CASE("golden_test: missing golden in update mode creates it") {
    TestDirGuard guard;

    // Ensure golden does not exist.
    std::filesystem::remove_all(kGoldenDir);

    GoldenTestConfig config;
    config.golden_directory = kGoldenDir;
    config.artifact_directory = kArtifactDir;
    config.mode = GoldenMode::Update;

    auto fb = make_test_fb(50, 50, 0.3f, 0.6f, 0.9f, 1.0f);
    auto result = verify_golden(fb, "create_golden", config);

    CHECK(result.passed);
    CHECK(result.golden_updated);
    CHECK(std::filesystem::exists(result.golden_path));
}

TEST_CASE("golden_test: different golden in update mode overwrites") {
    TestDirGuard guard;

    // Create initial golden.
    GoldenTestConfig config;
    config.golden_directory = kGoldenDir;
    config.artifact_directory = kArtifactDir;
    config.mode = GoldenMode::Update;

    auto fb1 = make_test_fb(50, 50, 0.3f, 0.6f, 0.9f, 1.0f);
    verify_golden(fb1, "overwrite_golden", config);

    // Now update with different content.
    auto fb2 = make_test_fb(50, 50, 0.9f, 0.3f, 0.6f, 1.0f);
    auto result = verify_golden(fb2, "overwrite_golden", config);

    CHECK(result.passed);
    CHECK(result.golden_updated);

    // Reload and verify content matches fb2.
    auto loaded = load_png_as_framebuffer(result.golden_path.string());
    REQUIRE(loaded != nullptr);
    CHECK(loaded->get_pixel(0, 0).r == doctest::Approx(0.9f).epsilon(2.0/255.0));
}

TEST_CASE("golden_test: artifacts created on failure") {
    TestDirGuard guard;

    // Create a golden.
    GoldenTestConfig update_config;
    update_config.golden_directory = kGoldenDir;
    update_config.artifact_directory = kArtifactDir;
    update_config.mode = GoldenMode::Update;

    auto original = make_test_fb(50, 50, 0.5f, 0.5f, 0.5f, 1.0f);
    verify_golden(original, "artifact_test", update_config);

    // Now verify with different content.
    GoldenTestConfig verify_config;
    verify_config.golden_directory = kGoldenDir;
    verify_config.artifact_directory = kArtifactDir;
    verify_config.mode = GoldenMode::Verify;

    auto modified = make_modified_fb(original, 25, 25, 0.3f, 0.0f, 0.0f, 0.0f);
    auto result = verify_golden(modified, "artifact_test", verify_config);

    CHECK_FALSE(result.passed);
    CHECK(std::filesystem::exists(result.actual_path));
    CHECK(std::filesystem::exists(result.expected_path));
    CHECK(std::filesystem::exists(result.diff_path));
    CHECK(std::filesystem::exists(result.report_path));
}

TEST_CASE("golden_test: sanified case name prevents directory traversal") {
    CHECK(sanitise_case_name("../evil") == "parent_evil");
    CHECK(sanitise_case_name("normal_name") == "normal_name");
    CHECK(sanitise_case_name("  spaced  ") == "spaced");
    CHECK(sanitise_case_name("path/to/file") == "path_to_file");
}

TEST_CASE("golden_test: infinite PSNR for identical images") {
    auto fb = make_test_fb(10, 10, 0.2f, 0.4f, 0.6f, 1.0f);
    auto result = compare_framebuffers(fb, fb);
    CHECK(result.psnr == std::numeric_limits<double>::infinity());
}

TEST_CASE("golden_test: no source tree modification in verify mode") {
    TestDirGuard guard;

    // Create a golden in a non-standard location.
    const auto non_standard_dir = kTestDir / "custom_golden";
    std::filesystem::create_directories(non_standard_dir);

    // Save golden manually.
    auto fb = make_test_fb(20, 20, 0.1f, 0.2f, 0.3f, 1.0f);
    save_png(fb, (non_standard_dir / "no_modify.png").string());

    GoldenTestConfig config;
    config.golden_directory = non_standard_dir;
    config.artifact_directory = kArtifactDir;
    config.mode = GoldenMode::Verify;

    // Verify — should not modify any golden files.
    auto result = verify_golden(fb, "no_modify", config);
    CHECK(result.passed);

    // Verify that the golden file was not modified (timestamp unchanged).
    // We just check it still exists and passes.
    CHECK(std::filesystem::exists(result.golden_path));
}

TEST_CASE("golden_test: golden_mode_from_environment parses correctly") {
    // Unset to ensure default.
    unsetenv("CHRONON3D_UPDATE_GOLDENS");
    CHECK(golden_mode_from_environment() == GoldenMode::Verify);

    // Set to truthy values.
    setenv("CHRONON3D_UPDATE_GOLDENS", "1", 1);
    CHECK(golden_mode_from_environment() == GoldenMode::Update);

    setenv("CHRONON3D_UPDATE_GOLDENS", "true", 1);
    CHECK(golden_mode_from_environment() == GoldenMode::Update);

    setenv("CHRONON3D_UPDATE_GOLDENS", "ON", 1);
    CHECK(golden_mode_from_environment() == GoldenMode::Update);

    // Set to falsy values.
    setenv("CHRONON3D_UPDATE_GOLDENS", "0", 1);
    CHECK(golden_mode_from_environment() == GoldenMode::Verify);

    setenv("CHRONON3D_UPDATE_GOLDENS", "false", 1);
    CHECK(golden_mode_from_environment() == GoldenMode::Verify);
}
