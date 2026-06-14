#include <doctest/doctest.h>

#include "camera_visual_scenes.hpp"
#include "camera_visual_compare.hpp"
#include "../support/golden_test.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

const std::filesystem::path kGoldenDir = "tests/golden/camera";
const std::filesystem::path kArtifactDir = "artifacts/visual/camera";

ImageDiffThreshold lenient_threshold() {
    ImageDiffThreshold t;
    t.max_mean_abs_error = 2.5 / 255.0;
    t.max_abs_error = 24.0 / 255.0;
    t.max_changed_pixel_ratio = 0.02;
    return t;
}

void verify_camera_golden(const Framebuffer& fb, const std::string& case_name) {
    auto config = camera_golden_config(kGoldenDir, kArtifactDir, lenient_threshold());
    auto result = verify_golden(fb, case_name, config);

    if (result.golden_missing) {
        FAIL("Golden missing: " << result.golden_path.string()
             << ". Set CHRONON3D_UPDATE_GOLDENS=1 to create.");
    }

    INFO(result.message);
    CHECK(result.passed);
}

} // namespace



TEST_CASE("Camera visual: center target renders correctly") {
    auto renderer = make_renderer();
    auto comp = make_center_target_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 960);
    CHECK(fb->height() == 540);

    // Ensure the image is not blank
    const float avg_luma = average_luma_rect(*fb, 0, 0, 960, 540);
    CHECK(avg_luma > 0.005f);

    verify_camera_golden(*fb, "center_target");
}

TEST_CASE("Camera visual: parallax stack frame 000") {
    auto renderer = make_renderer();
    auto comp = make_parallax_stack_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    CHECK(average_luma_rect(*fb, 0, 0, 960, 540) > 0.003f);
}

TEST_CASE("Camera visual: parallax stack frame 030") {
    auto renderer = make_renderer();
    auto comp = make_parallax_stack_composition();

    auto fb = renderer.render_frame(comp, 30);
    REQUIRE(fb != nullptr);

    CHECK(average_luma_rect(*fb, 0, 0, 960, 540) > 0.003f);
}

TEST_CASE("Camera visual: parallax stack displacement is correct") {
    auto renderer = make_renderer();
    auto comp = make_parallax_stack_composition();

    auto fb0 = renderer.render_frame(comp, 0);
    auto fb30 = renderer.render_frame(comp, 30);
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb30 != nullptr);

    // Verify the image is not blank
    CHECK(average_luma_rect(*fb0, 0, 0, 960, 540) > 0.003f);
    CHECK(average_luma_rect(*fb30, 0, 0, 960, 540) > 0.003f);

    // Check that frame 30 differs from frame 0 (camera moved → parallax)
    CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb30));
}

TEST_CASE("Camera visual: orbit two node frame 000") {
    auto renderer = make_renderer();
    auto comp = make_orbit_two_node_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    CHECK(average_luma_rect(*fb, 0, 0, 960, 540) > 0.003f);
}

TEST_CASE("Camera visual: orbit two node frame 030") {
    auto renderer = make_renderer();
    auto comp = make_orbit_two_node_composition();

    auto fb = renderer.render_frame(comp, 30);
    REQUIRE(fb != nullptr);

    CHECK(average_luma_rect(*fb, 0, 0, 960, 540) > 0.003f);
}

TEST_CASE("Camera visual: orbit two node renders without errors") {
    auto renderer = make_renderer();
    auto comp = make_orbit_two_node_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    CHECK(average_luma_rect(*fb, 0, 0, 960, 540) > 0.003f);
}

TEST_CASE("Camera visual: near plane crossing does not explode") {
    auto renderer = make_renderer();
    auto comp = make_near_plane_crossing_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Image should not be completely black or white
    const float avg_luma = average_luma_rect(*fb, 0, 0, 960, 540);
    CHECK(avg_luma > 0.003f);
    CHECK(avg_luma < 0.995f);

    // No NaN pixels
    bool has_nan = false;
    for (int y = 0; y < fb->height(); ++y) {
        for (int x = 0; x < fb->width(); ++x) {
            Color c = fb->get_pixel(x, y);
            if (std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a)) {
                has_nan = true;
            }
        }
    }
    CHECK_FALSE(has_nan);

    verify_camera_golden(*fb, "near_plane_crossing");
}

TEST_CASE("Camera visual: z sort stack is correct") {
    auto renderer = make_renderer();
    auto comp = make_z_sort_stack_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Verify the image is not blank
    CHECK(average_luma_rect(*fb, 0, 0, 960, 540) > 0.003f);

    // Sample a small region around center — near card (red) should be dominant
    Color avg = average_color_rect(*fb, 470, 260, 490, 280);
    // Check red channel is higher than blue and green
    CHECK(avg.r > avg.b);
    CHECK(avg.r > avg.g);
}
