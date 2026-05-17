#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

SoftwareRenderer make_renderer() {
    SoftwareRenderer r;
    RenderSettings s;
    s.use_modular_graph = true;
    r.set_settings(s);
    return r;
}

// Minimal scene: red card at z_red, blue card at z_blue, both at world XY center.
std::unique_ptr<Framebuffer> render_two_cards(float z_red, float z_blue) {
    auto renderer = make_renderer();
    Composition comp({.name = "DepthSort", .width = 400, .height = 400, .duration = 1},
        [z_red, z_blue](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set({
                .enabled  = true,
                .position = {0.0f, 0.0f, -1000.0f},
                .zoom     = 1000.0f
            });
            s.layer("blue", [z_blue](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z_blue});
                l.rect("r", {.size = {120.0f, 120.0f}, .color = Color{0.1f, 0.2f, 1.0f, 1.0f}, .pos = {0, 0, 0}});
            });
            s.layer("red", [z_red](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z_red});
                l.rect("r", {.size = {120.0f, 120.0f}, .color = Color{1.0f, 0.1f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

// Separate cards at different Y to measure apparent size without overlap.
std::unique_ptr<Framebuffer> render_scale_comparison() {
    auto renderer = make_renderer();
    Composition comp({.name = "DepthScale", .width = 640, .height = 480, .duration = 1},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set({
                .enabled  = true,
                .position = {0.0f, 0.0f, -1000.0f},
                .zoom     = 1000.0f
            });
            // Blue top — far z=+300
            s.layer("blue_far", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, -100.0f, 300.0f});
                l.rect("r", {.size = {100.0f, 100.0f}, .color = Color{0.1f, 0.2f, 1.0f, 1.0f}, .pos = {0, 0, 0}});
            });
            // Red bottom — near z=-300
            s.layer("red_near", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 100.0f, -300.0f});
                l.rect("r", {.size = {100.0f, 100.0f}, .color = Color{1.0f, 0.1f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

int count_channel(const Framebuffer& fb,
                  float r_min, float r_max,
                  float g_min, float g_max,
                  float b_min, float b_max) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (c.r >= r_min && c.r <= r_max &&
                c.g >= g_min && c.g <= g_max &&
                c.b >= b_min && c.b <= b_max) ++n;
        }
    return n;
}

} // namespace

TEST_CASE("Proof — DepthLadder: near card appears larger than far card") {
    auto fb = render_scale_comparison();
    REQUIRE(fb != nullptr);

    save_debug(*fb, "output/debug/proofs/depth_ladder/scale_comparison.png");

    const int red_pixels  = count_channel(*fb, 0.7f, 1.1f, 0.0f, 0.3f, 0.0f, 0.3f);
    const int blue_pixels = count_channel(*fb, 0.0f, 0.3f, 0.0f, 0.4f, 0.7f, 1.1f);

    CHECK(red_pixels  > 0);
    CHECK(blue_pixels > 0);
    // z=-300 projects larger than z=+300 — near card has more screen pixels
    CHECK(red_pixels > blue_pixels);
}

TEST_CASE("Proof — DepthLadder: near card occludes far card at same XY") {
    // Red (z=-300) in front of blue (z=+300) — center pixel = red
    auto fb = render_two_cards(-300.0f, 300.0f);
    REQUIRE(fb != nullptr);

    save_debug(*fb, "output/debug/proofs/depth_ladder/sort_near_front.png");

    const Color center = fb->get_pixel(200, 200);
    CHECK(center.r > 0.7f);
    CHECK(center.g < 0.3f);
    CHECK(center.b < 0.3f);
}

TEST_CASE("Proof — DepthLadder: z-sorting flips correctly when depths are swapped") {
    // Blue (z=-300, now near) in front of red (z=+300, now far) — center pixel = blue
    auto fb = render_two_cards(300.0f, -300.0f);
    REQUIRE(fb != nullptr);

    save_debug(*fb, "output/debug/proofs/depth_ladder/sort_blue_front.png");

    const Color center = fb->get_pixel(200, 200);
    CHECK(center.b > 0.7f);
    CHECK(center.r < 0.3f);
}
