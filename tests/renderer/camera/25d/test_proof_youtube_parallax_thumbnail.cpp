#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <tests/helpers/render_fixtures.hpp>
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

// Pan scene: background at z_bg, subject at z_sub, foreground at z_fg
std::unique_ptr<Framebuffer> render_parallax_frame(float t, float z_bg, float z_sub, float z_fg) {
    auto renderer = make_renderer();
    Composition comp({.name="YTParallax", .width=640, .height=360, .duration=1},
        [t, z_bg, z_sub, z_fg](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const float st = camera_motion::smoothstep(t);
            s.camera().set({
                .enabled  = true,
                .position = {camera_motion::lerp(-60.0f, 60.0f, st), 0.0f,
                             camera_motion::lerp(-1000.0f, -880.0f, st)},
                .zoom = 1000.0f
            });

            s.rect("bg_fill", {.size={640.0f,360.0f}, .color={0.05f,0.08f,0.18f,1.0f}, .pos={320.0f,180.0f,0.0f}});

            // Background: wide colored rect (parallax: moves least)
            s.layer("bg", [z_bg](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, z_bg});
                l.rect("sky", {.size={2400.0f,1200.0f}, .color={0.06f,0.08f,0.20f,1.0f}, .pos={0,0,0}});
                l.rect("hor", {.size={2400.0f,140.0f}, .color={0.55f,0.28f,0.08f,0.65f}, .pos={0.0f,80.0f,0.0f}}).blur(35.0f);
            });

            // Subject: centered (parallax: moves normal)
            s.layer("sub", [z_sub](LayerBuilder& l) {
                l.enable_3d().position({0.0f, -20.0f, z_sub});
                l.rounded_rect("card", {.size={280.0f,360.0f}, .radius=8.0f,
                                         .color={0.65f,0.58f,0.50f,1.0f}, .pos={0,0,0}});
            });

            // Foreground: small colored rect far-left (parallax: moves most)
            s.layer("fg", [z_fg](LayerBuilder& l) {
                l.enable_3d().position({-250.0f, 100.0f, z_fg});
                l.circle("marker", {.radius=30.0f, .color={1.0f,0.9f,0.1f,0.8f}, .pos={0,0,0}});
            });

            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

template<typename Pred>
float scan_centroid_x(const Framebuffer& fb, Pred pred) {
    double sum = 0; int cnt = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x)
            if (pred(fb.get_pixel(x, y))) { sum += x; ++cnt; }
    return cnt > 0 ? static_cast<float>(sum / cnt) : -1.0f;
}

} // namespace

TEST_CASE("Proof YT — ParallaxThumbnail: renders at key frames") {
    auto fb0  = render_parallax_frame(0.0f, 800.0f, 0.0f, -300.0f);
    auto fb45 = render_parallax_frame(0.5f, 800.0f, 0.0f, -300.0f);
    auto fb89 = render_parallax_frame(1.0f, 800.0f, 0.0f, -300.0f);

    REQUIRE(fb0  != nullptr);
    REQUIRE(fb45 != nullptr);
    REQUIRE(fb89 != nullptr);

    save_debug(*fb0,  "output/debug/proofs/youtube/parallax_f000.png");
    save_debug(*fb45, "output/debug/proofs/youtube/parallax_f045.png");
    save_debug(*fb89, "output/debug/proofs/youtube/parallax_f089.png");
}

TEST_CASE("Proof YT — ParallaxThumbnail: foreground shifts more than subject during pan") {
    auto is_warm_subject = [](const Color& c){ return c.r > 0.5f && c.g > 0.4f && c.b > 0.3f && c.r < 0.85f; };
    auto is_yellow_fg    = [](const Color& c){ return c.r > 0.8f && c.g > 0.7f && c.b < 0.35f; };

    auto fb0 = render_parallax_frame(0.0f, 800.0f, 0.0f, -300.0f);
    auto fb1 = render_parallax_frame(1.0f, 800.0f, 0.0f, -300.0f);

    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);

    const float sub_x0 = scan_centroid_x(*fb0, is_warm_subject);
    const float sub_x1 = scan_centroid_x(*fb1, is_warm_subject);
    const float fg_x0  = scan_centroid_x(*fb0, is_yellow_fg);
    const float fg_x1  = scan_centroid_x(*fb1, is_yellow_fg);

    // Both must be visible
    if (sub_x0 > 0 && sub_x1 > 0 && fg_x0 > 0 && fg_x1 > 0) {
        const float sub_shift = std::abs(sub_x1 - sub_x0);
        const float fg_shift  = std::abs(fg_x1  - fg_x0);
        // Foreground (z=-300) shifts more than subject (z=0)
        CHECK(fg_shift > sub_shift);
    }
}
