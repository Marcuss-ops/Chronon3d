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

std::unique_ptr<Framebuffer> render_orbit_frame(float t) {
    auto renderer = make_renderer();
    Composition comp({.name = "OrbitTest", .width = 640, .height = 480, .duration = 1},
        [t](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set(camera_motion::orbit(t, {
                .radius          = 80.0f,
                .z               = -1000.0f,
                .y               = 0.0f,
                .target          = {0.0f, 0.0f, 0.0f},
                .start_angle_deg = -20.0f,
                .end_angle_deg   =  20.0f,
                .zoom            = 1000.0f
            }));
            s.rect("bg", {
                .size  = {640.0f, 480.0f},
                .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
                .pos   = {320.0f, 240.0f, 0.0f}
            });
            // White central subject at world origin
            s.layer("subject", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("card", {.size = {120.0f, 120.0f}, .color = Color{1.0f, 1.0f, 1.0f, 1.0f}, .pos = {0, 0, 0}});
            });
            // Orange left
            s.layer("left", [](LayerBuilder& l) {
                l.enable_3d().position({-250.0f, 0.0f, 0.0f});
                l.rect("card", {.size = {80.0f, 80.0f}, .color = Color{1.0f, 0.5f, 0.1f, 1.0f}, .pos = {0, 0, 0}});
            });
            // Cyan right
            s.layer("right", [](LayerBuilder& l) {
                l.enable_3d().position({250.0f, 0.0f, 0.0f});
                l.rect("card", {.size = {80.0f, 80.0f}, .color = Color{0.1f, 0.9f, 0.9f, 1.0f}, .pos = {0, 0, 0}});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

} // namespace

TEST_CASE("Proof — OrbitSubject: central subject remains near screen center during orbit") {
    const Color white_sel = Color{1.0f, 1.0f, 1.0f, 1.0f};
    const float cx = 320.0f;
    const float cy = 240.0f;
    const float tolerance = 40.0f; // orbit moves camera, subject may shift slightly

    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        auto fb = render_orbit_frame(t);
        REQUIRE(fb != nullptr);

        const float sx = centroid_x(*fb, white_sel);
        const float sy = centroid_y(*fb, white_sel);

        CHECK(sx > 0.0f); // subject must be visible
        CHECK(std::abs(sx - cx) < tolerance);
        CHECK(std::abs(sy - cy) < tolerance);
    }

    save_debug(*render_orbit_frame(0.0f),  "output/debug/proofs/orbit_subject/orbit_t0.png");
    save_debug(*render_orbit_frame(0.5f),  "output/debug/proofs/orbit_subject/orbit_t50.png");
    save_debug(*render_orbit_frame(1.0f),  "output/debug/proofs/orbit_subject/orbit_t100.png");
}

TEST_CASE("Proof — OrbitSubject: side objects show horizontal parallax during orbit") {
    const Color orange_sel = Color{1.0f, 0.5f, 0.1f, 1.0f};
    const Color cyan_sel   = Color{0.1f, 0.9f, 0.9f, 1.0f};

    auto fb0 = render_orbit_frame(0.0f);
    auto fb1 = render_orbit_frame(1.0f);

    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);

    const float orange_x0 = centroid_x(*fb0, orange_sel);
    const float orange_x1 = centroid_x(*fb1, orange_sel);
    const float cyan_x0   = centroid_x(*fb0, cyan_sel);
    const float cyan_x1   = centroid_x(*fb1, cyan_sel);

    // Side objects must shift during orbit (orbit != static camera)
    if (orange_x0 > 0.0f && orange_x1 > 0.0f)
        CHECK(std::abs(orange_x1 - orange_x0) > 2.0f);
    if (cyan_x0 > 0.0f && cyan_x1 > 0.0f)
        CHECK(std::abs(cyan_x1 - cyan_x0) > 2.0f);
}
