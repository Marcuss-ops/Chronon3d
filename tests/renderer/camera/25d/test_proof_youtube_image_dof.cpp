#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <cmath>
#include <string>

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

// Builds a test DOF scene: background shapes at z_bg, subject rect at z_sub.
std::unique_ptr<Framebuffer> render_dof_scene(float z_bg, float z_sub, bool dof_on) {
    auto renderer = make_renderer();
    Composition comp({.name="YTDof", .width=640, .height=360, .duration=1},
        [z_bg, z_sub, dof_on](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            Camera2_5D cam;
            cam.enabled  = true;
            cam.position = {0.0f, 0.0f, -1000.0f};
            cam.zoom     = 1000.0f;
            cam.dof = {.enabled=dof_on, .focus_z=0.0f, .aperture=0.028f, .max_blur=24.0f};
            s.camera().set(cam);

            // Background with distinct edges at z_bg
            s.layer("bg", [z_bg](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z_bg});
                l.rect("base", {.size={2000.0f,1200.0f}, .color={0.05f,0.04f,0.08f,1.0f}, .pos={0,0,0}});
                // Colored rects — their edges get DOF-blurred
                l.rect("b1", {.size={120.0f,350.0f}, .color={0.55f,0.20f,0.08f,1.0f}, .pos={-350.0f,0.0f,0.0f}});
                l.rect("b2", {.size={90.0f,250.0f},  .color={0.08f,0.30f,0.55f,1.0f}, .pos={280.0f,-30.0f,0.0f}});
                l.rect("b3", {.size={110.0f,300.0f}, .color={0.20f,0.55f,0.12f,1.0f}, .pos={0.0f,60.0f,0.0f}});
            });

            // Subject — sharp at z_sub (focus)
            s.layer("sub", [z_sub](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z_sub});
                l.rounded_rect("card", {.size={220.0f,280.0f}, .radius=8.0f,
                                         .color={0.90f,0.85f,0.78f,1.0f}, .pos={0,0,0}});
                // Distinctive stripe — edge visible when not blurred
                l.rect("stripe", {.size={220.0f,4.0f}, .color={0.0f,0.0f,0.0f,1.0f}, .pos={0.0f,-60.0f,0.0f}});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

// Average gradient magnitude (sharpness) in a region
float sharpness(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    double sum = 0; int cnt = 0;
    for (int y = y0; y < y1 - 1 && y < fb.height() - 1; ++y)
        for (int x = x0; x < x1 - 1 && x < fb.width() - 1; ++x) {
            const Color a = fb.get_pixel(x, y);
            const Color r = fb.get_pixel(x+1, y);
            const Color d = fb.get_pixel(x, y+1);
            sum += std::abs(a.r - r.r) + std::abs(a.g - r.g) + std::abs(a.b - r.b);
            sum += std::abs(a.r - d.r) + std::abs(a.g - d.g) + std::abs(a.b - d.b);
            ++cnt;
        }
    return cnt > 0 ? static_cast<float>(sum / cnt) : 0.0f;
}

} // namespace

TEST_CASE("Proof YT — ImageDof: DOF with enabled=true blurs background more than subject") {
    // DOF enabled: background at z=800, subject at z=0 (focus)
    auto fb_dof = render_dof_scene(800.0f, 0.0f, true);
    REQUIRE(fb_dof != nullptr);

    save_debug(*fb_dof, "output/debug/proofs/youtube/image_dof_enabled.png");

    // Background region: left side (where bg rects live)
    const float bg_sharp  = sharpness(*fb_dof,  40, 40, 240, 200);
    // Subject region: center area (where card is)
    const float sub_sharp = sharpness(*fb_dof, 270, 80, 370, 220);

    CHECK(sub_sharp > 0.0f);  // subject has visible content
    CHECK(bg_sharp  > 0.0f);  // background has visible content

    // DOF must blur background more than subject — subject sharpness >= background
    CHECK(sub_sharp >= bg_sharp);
}

TEST_CASE("Proof YT — ImageDof: DOF disabled gives same or higher sharpness everywhere") {
    auto fb_on  = render_dof_scene(800.0f, 0.0f, true);
    auto fb_off = render_dof_scene(800.0f, 0.0f, false);

    REQUIRE(fb_on  != nullptr);
    REQUIRE(fb_off != nullptr);

    save_debug(*fb_off, "output/debug/proofs/youtube/image_dof_disabled.png");

    // Without DOF, background should be as sharp or sharper than with DOF
    const float bg_sharp_on  = sharpness(*fb_on,  40, 40, 240, 200);
    const float bg_sharp_off = sharpness(*fb_off, 40, 40, 240, 200);

    CHECK(bg_sharp_off >= bg_sharp_on);
}
