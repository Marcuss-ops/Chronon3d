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

// Pan scene: red at z_near, blue at z_far. Returns frame at t in [0,1].
std::unique_ptr<Framebuffer> render_pan_frame(float t, float z_near, float z_far) {
    auto renderer = make_renderer();
    Composition comp({.name = "PanTest", .width = 640, .height = 480, .duration = 1},
        [t, z_near, z_far](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set(camera_motion::pan(t, {
                .from_x = -120.0f,
                .to_x   =  120.0f,
                .z      = -1000.0f,
                .zoom   = 1000.0f
            }));
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

TEST_CASE("Proof — PanParallax: near object shifts more than far object during camera pan") {
    const Color red_sel  = Color{1.0f, 0.1f, 0.1f, 1.0f};
    const Color blue_sel = Color{0.1f, 0.2f, 1.0f, 1.0f};

    auto fb0 = render_pan_frame(0.0f, -300.0f, 600.0f);
    auto fb1 = render_pan_frame(1.0f, -300.0f, 600.0f);

    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);

    save_debug(*fb0, "output/debug/proofs/pan_parallax/frame_start.png");
    save_debug(*fb1, "output/debug/proofs/pan_parallax/frame_end.png");

    const float near_x0 = centroid_x(*fb0, red_sel);
    const float near_x1 = centroid_x(*fb1, red_sel);
    const float far_x0  = centroid_x(*fb0, blue_sel);
    const float far_x1  = centroid_x(*fb1, blue_sel);

    // Both objects must be visible
    CHECK(near_x0 > 0.0f);
    CHECK(near_x1 > 0.0f);
    CHECK(far_x0  > 0.0f);
    CHECK(far_x1  > 0.0f);

    const float near_shift = std::abs(near_x1 - near_x0);
    const float far_shift  = std::abs(far_x1  - far_x0);

    // Near object (z=-300) must shift more than far object (z=+600)
    CHECK(near_shift > far_shift);
    // Sanity: there IS some shift (camera actually moved)
    CHECK(near_shift > 2.0f);
}
