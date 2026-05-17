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

Composition make_track_matte_comp(
    TrackMatteType type,
    Color matte_color,
    Vec3 matte_position = {0, 0, 0},
    std::string matte_name = "matte"
) {
    return composition({
        .name = "TrackMatte",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer(matte_name, [=](LayerBuilder& l) {
            l.position(matte_position);
            l.circle("matte-circle", {
                .radius = 45.0f,
                .color = matte_color,
                .pos = {0, 0, 0}
            });
        });

        s.layer("target", [=](LayerBuilder& l) {
            switch (type) {
                case TrackMatteType::Alpha:
                    l.track_matte_alpha(matte_name);
                    break;
                case TrackMatteType::AlphaInverted:
                    l.track_matte_alpha_inverted(matte_name);
                    break;
                case TrackMatteType::Luma:
                    l.track_matte_luma(matte_name);
                    break;
                case TrackMatteType::LumaInverted:
                    l.track_matte_luma_inverted(matte_name);
                    break;
                default:
                    break;
            }

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
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(make_track_matte_comp(
        TrackMatteType::Alpha,
        Color::white()
    ), 0);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(100, 100);
    CHECK(center.r > 0.5f);
    CHECK(center.a > 0.5f);

    const Color corner = fb->get_pixel(25, 100);
    CHECK(corner.a == 0.0f);
    CHECK(corner.r == 0.0f);
}

TEST_CASE("Track matte alpha inverted reveals outside the matte") {
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(make_track_matte_comp(
        TrackMatteType::AlphaInverted,
        Color::white()
    ), 0);
    REQUIRE(fb != nullptr);

    CHECK(fb->get_pixel(100, 100).a == 0.0f);
    CHECK(fb->get_pixel(25, 100).r > 0.5f);
}

TEST_CASE("Track matte luma uses grayscale intensity") {
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(make_track_matte_comp(
        TrackMatteType::Luma,
        Color{0.5f, 0.5f, 0.5f, 1.0f}
    ), 0);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(100, 100);
    CHECK(center.a == doctest::Approx(0.214041f).epsilon(0.02f));
    CHECK(center.r > 0.5f);
}

TEST_CASE("Track matte luma inverted respects premultiplied alpha") {
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(make_track_matte_comp(
        TrackMatteType::LumaInverted,
        Color{1.0f, 1.0f, 1.0f, 0.0f}
    ), 0);
    REQUIRE(fb != nullptr);

    CHECK(fb->get_pixel(100, 100).a > 0.9f);
    CHECK(fb->get_pixel(100, 100).r > 0.5f);
}

TEST_CASE("Track matte source does not render directly") {
    auto renderer = make_renderer();
    auto comp = composition({
        .name = "TrackMatteSourceHidden",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("matte", [](LayerBuilder& l) {
            l.circle("matte-circle", {
                .radius = 70.0f,
                .color = Color{0.0f, 1.0f, 0.0f, 1.0f},
                .pos = {0, 0, 0}
            });
        });

        s.layer("target", [](LayerBuilder& l) {
            l.track_matte_alpha("matte");
            l.rect("target-rect", {
                .size = {40, 40},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Pixel is inside the matte source circle but outside the target.
    // If the matte source were composited directly, this would be green.
    const Color sample = fb->get_pixel(140, 100);
    CHECK(sample.a == 0.0f);
    CHECK(sample.g == 0.0f);
}

TEST_CASE("Track matte transformed source shifts the clipped region") {
    auto renderer = make_renderer();
    auto fb = renderer.render_frame(make_track_matte_comp(
        TrackMatteType::Alpha,
        Color::white(),
        Vec3{60.0f, 0.0f, 0.0f}
    ), 0);
    REQUIRE(fb != nullptr);

    CHECK(fb->get_pixel(100, 100).a == 0.0f);
    CHECK(fb->get_pixel(160, 100).r > 0.5f);
}

TEST_CASE("Track matte output changes when matte source changes") {
    auto renderer = make_renderer();

    auto fb_visible = renderer.render_frame(make_track_matte_comp(
        TrackMatteType::Alpha,
        Color::white()
    ), 0);
    auto fb_hidden = renderer.render_frame(make_track_matte_comp(
        TrackMatteType::Alpha,
        Color::transparent()
    ), 0);

    REQUIRE(fb_visible != nullptr);
    REQUIRE(fb_hidden != nullptr);

    CHECK(framebuffer_hash(*fb_visible) != framebuffer_hash(*fb_hidden));
}

TEST_CASE("Track matte missing source leaves target unchanged") {
    auto renderer = make_renderer();
    auto comp = composition({
        .name = "TrackMatteMissingSource",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("target", [](LayerBuilder& l) {
            l.track_matte_alpha("missing");
            l.rect("target-rect", {
                .size = {160, 160},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });
        });
        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    CHECK(fb->get_pixel(100, 100).r > 0.5f);
    CHECK(fb->get_pixel(25, 100).r > 0.5f);
}
