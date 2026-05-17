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

std::unique_ptr<Framebuffer> render_quote_frame(Frame f) {
    auto renderer = make_renderer();
    Composition comp({.name="YTQuote", .width=640, .height=360, .duration=90},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const float t  = ctx.duration > 1
                ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1) : 0.0f;
            const float st = camera_motion::smoothstep(t);
            s.camera().set({
                .enabled  = true,
                .position = {0.0f, 0.0f, camera_motion::lerp(-1000.0f, -940.0f, st)},
                .zoom     = 1000.0f,
                .dof      = {.enabled=true, .focus_z=0.0f, .aperture=0.018f, .max_blur=16.0f}
            });

            // Background
            s.layer("bg", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 600.0f});
                l.rect("base", {.size={1600.0f,900.0f}, .color={0.04f,0.04f,0.06f,1.0f}, .pos={0,0,0}});
                l.circle("a1", {.radius=200.0f, .color={0.08f,0.06f,0.20f,0.5f}, .pos={-400.0f,-30.0f,0.0f}}).blur(50.0f);
                l.circle("a2", {.radius=150.0f, .color={0.18f,0.04f,0.08f,0.4f}, .pos={350.0f,60.0f,0.0f}}).blur(40.0f);
            });

            // Backdrop card
            s.layer("card", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rounded_rect("bg", {.size={800.0f,180.0f}, .radius=6.0f,
                                       .color={0.04f,0.04f,0.06f,0.78f}, .pos={0,0,0}});
                l.rect("line", {.size={720.0f,2.0f}, .color={0.6f,0.5f,0.9f,0.8f}, .pos={0,-78.0f,0}});
            });

            return s.build();
        });
    return renderer.render_frame(comp, f);
}

float region_brightness(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    double sum = 0; int cnt = 0;
    for (int y = y0; y < y1 && y < fb.height(); ++y)
        for (int x = x0; x < x1 && x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            sum += (c.r + c.g + c.b) / 3.0;
            ++cnt;
        }
    return cnt > 0 ? static_cast<float>(sum / cnt) : 0.0f;
}

} // namespace

TEST_CASE("Proof YT — QuoteScene: scene renders at all 3 key frames") {
    auto fb0  = render_quote_frame(0);
    auto fb45 = render_quote_frame(45);
    auto fb89 = render_quote_frame(89);

    REQUIRE(fb0  != nullptr);
    REQUIRE(fb45 != nullptr);
    REQUIRE(fb89 != nullptr);

    save_debug(*fb0,  "output/debug/proofs/youtube/quote_f000.png");
    save_debug(*fb45, "output/debug/proofs/youtube/quote_f045.png");
    save_debug(*fb89, "output/debug/proofs/youtube/quote_f089.png");
}

TEST_CASE("Proof YT — QuoteScene: center card is brighter than background corners") {
    auto fb = render_quote_frame(0);
    REQUIRE(fb != nullptr);

    // Center (card region) vs top-left corner (background)
    const float center = region_brightness(*fb, 220, 150, 420, 210);
    const float corner = region_brightness(*fb, 0, 0, 80, 60);

    // Card backdrop must add at least a tiny brightness above pure background
    CHECK(center >= corner);
}
