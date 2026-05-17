#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <cmath>

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

std::unique_ptr<Framebuffer> render_tilt_frame(float tilt_deg, float z_near, float z_far) {
    auto renderer = make_renderer();
    Composition comp({.name = "TiltTest", .width = 640, .height = 480, .duration = 1},
        [tilt_deg, z_near, z_far](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            Camera2_5D cam;
            cam.enabled  = true;
            cam.zoom     = 1000.0f;
            cam.position = {0.0f, 0.0f, -1000.0f};
            cam.set_tilt(tilt_deg);
            s.camera().set(cam);

            s.rect("bg", {
                .size  = {640.0f, 480.0f},
                .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
                .pos   = {320.0f, 240.0f, 0.0f}
            });
            s.layer("near", [z_near](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z_near});
                l.rect("r", {.size = {80.0f, 80.0f}, .color = Color{1.0f, 0.1f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            });
            s.layer("far", [z_far](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z_far});
                l.rect("r", {.size = {80.0f, 80.0f}, .color = Color{0.1f, 0.2f, 1.0f, 1.0f}, .pos = {0, 0, 0}});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

} // namespace

TEST_CASE("Proof — TiltParallax: near object shifts more vertically than far object") {
    const Color red_sel  = Color{1.0f, 0.1f, 0.1f, 1.0f};
    const Color blue_sel = Color{0.1f, 0.2f, 1.0f, 1.0f};

    auto fb_neg = render_tilt_frame(-10.0f, -300.0f, 400.0f);
    auto fb_pos = render_tilt_frame( 10.0f, -300.0f, 400.0f);

    REQUIRE(fb_neg != nullptr);
    REQUIRE(fb_pos != nullptr);

    save_debug(*fb_neg, "output/debug/proofs/tilt_parallax/tilt_neg10.png");
    save_debug(*fb_pos, "output/debug/proofs/tilt_parallax/tilt_pos10.png");

    const float near_y0 = centroid_y(*fb_neg, red_sel);
    const float near_y1 = centroid_y(*fb_pos, red_sel);
    const float far_y0  = centroid_y(*fb_neg, blue_sel);
    const float far_y1  = centroid_y(*fb_pos, blue_sel);

    CHECK(near_y0 > 0.0f);
    CHECK(near_y1 > 0.0f);
    CHECK(far_y0  > 0.0f);
    CHECK(far_y1  > 0.0f);

    const float near_shift = std::abs(near_y1 - near_y0);
    const float far_shift  = std::abs(far_y1  - far_y0);

    // Near object (z=-300) shifts more vertically than far object (z=+400)
    CHECK(near_shift > far_shift);
    CHECK(near_shift > 2.0f);
}
