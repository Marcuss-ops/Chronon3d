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

std::unique_ptr<Framebuffer> render_roll_frame(float roll_deg) {
    auto renderer = make_renderer();
    Composition comp({.name = "RollTest", .width = 640, .height = 480, .duration = 1},
        [roll_deg](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            Camera2_5D cam;
            cam.enabled  = true;
            cam.zoom     = 1000.0f;
            cam.position = {0.0f, 0.0f, -1000.0f};
            cam.set_roll(roll_deg);
            s.camera().set(cam);

            s.rect("bg", {
                .size  = {640.0f, 480.0f},
                .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
                .pos   = {320.0f, 240.0f, 0.0f}
            });
            // White center marker at world origin
            s.layer("center", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.circle("dot", {
                    .radius = 30.0f,
                    .color  = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .pos    = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

} // namespace

TEST_CASE("Proof — RollStability: center marker stays near screen center during roll") {
    const Color white_sel = Color{1.0f, 1.0f, 1.0f, 1.0f};
    const float cx = 320.0f;
    const float cy = 240.0f;
    const float tolerance = 12.0f; // pixels

    for (float roll : {-15.0f, -10.0f, 0.0f, 10.0f, 15.0f}) {
        auto fb = render_roll_frame(roll);
        REQUIRE(fb != nullptr);

        const float mx = centroid_x(*fb, white_sel);
        const float my = centroid_y(*fb, white_sel);

        CHECK(mx > 0.0f); // marker must be visible
        CHECK(std::abs(mx - cx) < tolerance);
        CHECK(std::abs(my - cy) < tolerance);
    }

    save_debug(*render_roll_frame(-15.0f), "output/debug/proofs/roll_stability/roll_neg15.png");
    save_debug(*render_roll_frame(  0.0f), "output/debug/proofs/roll_stability/roll_0.png");
    save_debug(*render_roll_frame( 15.0f), "output/debug/proofs/roll_stability/roll_pos15.png");
}
