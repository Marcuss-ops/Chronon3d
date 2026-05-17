#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/pixel_assertions.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::test;

// NOTE: for objects at world Y=0, pure camera X-rotation (tilt) produces the
// same screen-Y displacement for all Z depths (constant angle ratio). This
// means far objects are always occluded by near ones at the same XY. The proof
// verifies that tilt correctly displaces objects vertically and that the
// displacement direction is consistent with the tilt angle sign.

namespace {

SoftwareRenderer make_renderer() {
    SoftwareRenderer r;
    RenderSettings s;
    s.use_modular_graph = true;
    r.set_settings(s);
    return r;
}

template<typename Pred>
float scan_centroid_y(const Framebuffer& fb, Pred pred) {
    double sum = 0.0; int cnt = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x)
            if (pred(fb.get_pixel(x, y))) { sum += y; ++cnt; }
    return cnt > 0 ? static_cast<float>(sum / cnt) : -1.0f;
}

// Render a single red card at (0, 0, z_near) with the camera tilted to tilt_deg.
std::unique_ptr<Framebuffer> render_tilt_frame(float tilt_deg, float z_near) {
    auto renderer = make_renderer();
    Composition comp({.name = "TiltTest", .width = 640, .height = 480, .duration = 1},
        [tilt_deg, z_near](const FrameContext& ctx) {
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
                l.rect("r", {.size = {100.0f, 100.0f}, .color = Color{1.0f, 0.1f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

auto is_red = [](const Color& c){ return c.r > 0.7f && c.g < 0.35f && c.b < 0.35f; };

} // namespace

TEST_CASE("Proof — TiltParallax: tilt displaces objects vertically in screen space") {
    const float z = -300.0f;

    auto fb_neg = render_tilt_frame(-10.0f, z);
    auto fb_mid = render_tilt_frame(  0.0f, z);
    auto fb_pos = render_tilt_frame( 10.0f, z);

    REQUIRE(fb_neg != nullptr);
    REQUIRE(fb_mid != nullptr);
    REQUIRE(fb_pos != nullptr);

    save_debug(*fb_neg, "output/debug/proofs/tilt_parallax/tilt_neg10.png");
    save_debug(*fb_mid, "output/debug/proofs/tilt_parallax/tilt_0.png");
    save_debug(*fb_pos, "output/debug/proofs/tilt_parallax/tilt_pos10.png");

    const float y_neg = scan_centroid_y(*fb_neg, is_red);
    const float y_mid = scan_centroid_y(*fb_mid, is_red);
    const float y_pos = scan_centroid_y(*fb_pos, is_red);

    // Object must be visible at all three tilt angles
    CHECK(y_neg > 0.0f);
    CHECK(y_mid > 0.0f);
    CHECK(y_pos > 0.0f);

    // Tilt -10° shifts objects up (smaller screen Y), +10° shifts them down (larger screen Y)
    CHECK(y_neg < y_mid);
    CHECK(y_mid < y_pos);

    // Displacement must be significant (not just noise)
    CHECK(std::abs(y_neg - y_pos) > 20.0f);
}
