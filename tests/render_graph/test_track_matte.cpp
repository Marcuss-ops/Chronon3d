#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <xxhash.h>

using namespace chronon3d;

namespace {

SoftwareRenderer make_renderer() {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer;
}

u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

Composition make_alpha_matte_comp(Color matte_color) {
    return composition({
        .name = "TrackMatteAlpha",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [matte_color](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("matte", [matte_color](LayerBuilder& l) {
            l.circle("matte-circle", {
                .radius = 45.0f,
                .color = matte_color,
                .pos = {0, 0, 0}
            });
        });

        s.layer("target", [](LayerBuilder& l) {
            l.track_matte_alpha("matte");
            l.rect("target-rect", {
                .size = {160, 160},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("Track matte alpha clips target to matte silhouette") {
    auto comp = make_alpha_matte_comp(Color::white());
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(100, 100);
    CHECK(center.r > 0.5f);
    CHECK(center.a > 0.5f);

    const Color corner = fb->get_pixel(25, 100);
    CHECK(corner.a == 0.0f);
    CHECK(corner.r == 0.0f);
}

TEST_CASE("Track matte output changes when matte source changes") {
    auto white_matte = make_alpha_matte_comp(Color::white());
    auto black_matte = make_alpha_matte_comp(Color::transparent());
    auto renderer = make_renderer();

    auto fb_white = renderer.render_frame(white_matte, 0);
    auto fb_black = renderer.render_frame(black_matte, 0);
    REQUIRE(fb_white != nullptr);
    REQUIRE(fb_black != nullptr);

    CHECK(framebuffer_hash(*fb_white) != framebuffer_hash(*fb_black));
}
