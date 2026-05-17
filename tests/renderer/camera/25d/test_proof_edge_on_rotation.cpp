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

std::unique_ptr<Framebuffer> render_y_rotation(float angle_deg) {
    auto renderer = make_renderer();
    Composition comp({.name = "EdgeOnTest", .width = 640, .height = 480, .duration = 1},
        [angle_deg](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            Camera2_5D cam;
            cam.enabled  = true;
            cam.zoom     = 1000.0f;
            cam.position = {0.0f, 0.0f, -1000.0f};
            s.camera().set(cam);

            s.rect("bg", {
                .size  = {640.0f, 480.0f},
                .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
                .pos   = {320.0f, 240.0f, 0.0f}
            });
            s.layer("card", [angle_deg](LayerBuilder& l) {
                l.enable_3d()
                 .position({0.0f, 0.0f, 0.0f})
                 .rotate({0.0f, angle_deg, 0.0f});
                l.rect("face", {
                    .size  = {240.0f, 140.0f},
                    .color = Color{1.0f, 0.9f, 0.1f, 1.0f},
                    .pos   = {0.0f, 0.0f, 0.0f}
                });
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

int count_yellow(const Framebuffer& fb) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (c.r > 0.8f && c.g > 0.7f && c.b < 0.3f) ++n;
        }
    return n;
}

} // namespace

TEST_CASE("Proof — EdgeOnRotation: card pixel count decreases monotonically as Y rotation increases") {
    const int px0  = count_yellow(*render_y_rotation( 0.0f));
    const int px30 = count_yellow(*render_y_rotation(30.0f));
    const int px60 = count_yellow(*render_y_rotation(60.0f));

    save_debug(*render_y_rotation( 0.0f), "output/debug/proofs/edge_on_rotation/rot_0.png");
    save_debug(*render_y_rotation(30.0f), "output/debug/proofs/edge_on_rotation/rot_30.png");
    save_debug(*render_y_rotation(60.0f), "output/debug/proofs/edge_on_rotation/rot_60.png");
    save_debug(*render_y_rotation(89.0f), "output/debug/proofs/edge_on_rotation/rot_89.png");

    CHECK(px0  > 0);  // card visible face-on
    CHECK(px30 > 0);  // still visible at 30°
    // Each step should have fewer pixels (card is narrowing)
    CHECK(px0 > px30);
    CHECK(px30 > px60);
}

TEST_CASE("Proof — EdgeOnRotation: card does not grow wider during rotation") {
    // Width should only decrease, never increase
    const int w0  = width_at_row(*render_y_rotation( 0.0f), 240, Color{1.0f, 0.9f, 0.1f, 1.0f});
    const int w45 = width_at_row(*render_y_rotation(45.0f), 240, Color{1.0f, 0.9f, 0.1f, 1.0f});

    CHECK(w0  > 0);
    // At 45° the card must be narrower than at 0°
    CHECK(w45 < w0);
}
