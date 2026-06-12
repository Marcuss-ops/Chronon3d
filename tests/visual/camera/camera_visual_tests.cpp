#include <doctest/doctest.h>

#include "camera_visual_scenes.hpp"
#include "camera_visual_compare.hpp"
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

} // namespace

TEST_CASE("debug parallax pixels") {
    auto renderer = make_renderer();
    auto comp = make_parallax_stack_composition();
    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);
    
    Color c = fb->get_pixel(480, 270);
    std::cout << "Center pixel: " << c.r << " " << c.g << " " << c.b << " " << c.a << "\n";
    
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            Color p = fb->get_pixel(480 + dx, 270 + dy);
            std::cout << "Pixel(" << (480+dx) << "," << (270+dy) << "): " 
                      << p.r << " " << p.g << " " << p.b << " " << p.a << "\n";
        }
    }
    
    float luma = average_luma_rect(*fb, 0, 0, 960, 540);
    std::cout << "Avg luma: " << luma << "\n";
    
    int red_count = 0;
    for (int y = 0; y < fb->height(); ++y) {
        for (int x = 0; x < fb->width(); ++x) {
            Color p = fb->get_pixel(x, y);
            if (p.r > 0.5f && p.g < 0.3f && p.b < 0.3f) {
                ++red_count;
            }
        }
    }
    std::cout << "Red-ish pixels: " << red_count << "\n";
    CHECK(red_count > 0);
}

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

    verify_golden_or_create(*fb, "center_target", kGoldenDir, kArtifactDir, lenient_threshold());
}

TEST_CASE("Camera visual: parallax stack frame 000") {
    auto renderer = make_renderer();
    auto comp = make_parallax_stack_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    verify_golden_or_create(*fb, "parallax_stack_frame_000", kGoldenDir, kArtifactDir, lenient_threshold());
}

TEST_CASE("Camera visual: parallax stack frame 030") {
    auto renderer = make_renderer();
    auto comp = make_parallax_stack_composition();

    auto fb = renderer.render_frame(comp, 30);
    REQUIRE(fb != nullptr);

    verify_golden_or_create(*fb, "parallax_stack_frame_030", kGoldenDir, kArtifactDir, lenient_threshold());
}

TEST_CASE("Camera visual: parallax stack displacement is correct") {
    auto renderer = make_renderer();
    auto comp = make_parallax_stack_composition();

    auto fb0 = renderer.render_frame(comp, 0);
    auto fb30 = renderer.render_frame(comp, 30);
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb30 != nullptr);

    // Check that the near red card is visible in both frames
    CHECK(any_pixel(*fb0, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.15f));
    CHECK(any_pixel(*fb30, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.15f));

    // Check that the red card centroid moves significantly (parallax)
    const float cx0 = centroid_x(*fb0, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.15f);
    const float cx30 = centroid_x(*fb30, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.15f);
    CHECK(cx0 > 0.0f);
    CHECK(cx30 > 0.0f);
    CHECK(std::abs(cx30 - cx0) > 20.0f);

    // Check that frame 30 hash differs from frame 0 (something moved)
    CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb30));
}

TEST_CASE("Camera visual: orbit two node frame 000") {
    auto renderer = make_renderer();
    auto comp = make_orbit_two_node_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    verify_golden_or_create(*fb, "orbit_two_node_frame_000", kGoldenDir, kArtifactDir, lenient_threshold());
}

TEST_CASE("Camera visual: orbit two node frame 030") {
    auto renderer = make_renderer();
    auto comp = make_orbit_two_node_composition();

    auto fb = renderer.render_frame(comp, 30);
    REQUIRE(fb != nullptr);

    verify_golden_or_create(*fb, "orbit_two_node_frame_030", kGoldenDir, kArtifactDir, lenient_threshold());
}

TEST_CASE("Camera visual: orbit two node frame 060") {
    auto renderer = make_renderer();
    auto comp = make_orbit_two_node_composition();

    auto fb = renderer.render_frame(comp, 60);
    REQUIRE(fb != nullptr);

    verify_golden_or_create(*fb, "orbit_two_node_frame_060", kGoldenDir, kArtifactDir, lenient_threshold());
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

    verify_golden_or_create(*fb, "near_plane_crossing", kGoldenDir, kArtifactDir, lenient_threshold());
}

TEST_CASE("Camera visual: z sort stack is correct") {
    auto renderer = make_renderer();
    auto comp = make_z_sort_stack_composition();

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Sample a small region around center — near card (red) should be dominant
    Color avg = average_color_rect(*fb, 470, 260, 490, 280);
    // Check red channel is higher than blue and green
    CHECK(avg.r > avg.b);
    CHECK(avg.r > avg.g);

    verify_golden_or_create(*fb, "z_sort_stack", kGoldenDir, kArtifactDir, lenient_threshold());
}
