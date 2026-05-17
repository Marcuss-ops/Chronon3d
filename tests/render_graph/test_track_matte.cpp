#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/render_fixtures.hpp>

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
    test::save_debug(*fb, "output/debug/track_matte/alpha.png");

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
    test::save_debug(*fb, "output/debug/track_matte/alpha_inverted.png");

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
    test::save_debug(*fb, "output/debug/track_matte/luma.png");

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
    test::save_debug(*fb, "output/debug/track_matte/luma_inverted.png");

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

TEST_CASE("Track matte 3D source: projection applies correctly") {
    // A 3D matte circle rotated 60deg around Y produces a narrower silhouette
    // than the same circle unrotated. This verifies the matte source goes through
    // project_layer_2_5d() instead of the 2D flat path.
    auto make_3d_matte_comp = [](float rotate_y) {
        return composition({
            .name = "TrackMatte3D", .width = 320, .height = 200, .duration = 1
        }, [rotate_y](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().enable(true).position({0, 0, -800}).zoom(800).look_at({0, 0, 0});

            s.layer("matte_src", [rotate_y](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0}).rotate({0, rotate_y, 0});
                l.circle("c", {.radius = 70.0f, .color = Color::white(), .pos = {}});
            });

            s.layer("target", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0}).track_matte_alpha("matte_src");
                l.rect("r", {.size = {300, 180}, .color = {1, 0, 0, 1}, .pos = {}});
            });

            return s.build();
        });
    };

    auto flat    = test::render_modular(make_3d_matte_comp(0.0f));
    auto rotated = test::render_modular(make_3d_matte_comp(60.0f));
    REQUIRE(flat    != nullptr);
    REQUIRE(rotated != nullptr);

    test::save_debug(*flat,    "output/debug/track_matte/3d_source_flat.png");
    test::save_debug(*rotated, "output/debug/track_matte/3d_source_rotated.png");

    // Y=60deg rotation collapses the matte circle horizontally
    CHECK(framebuffer_hash(*flat) != framebuffer_hash(*rotated));

    // Count visible (alpha > 0) pixels in the center row.
    // The rotated matte must be narrower.
    auto count_visible_row = [](const Framebuffer& fb, int y) {
        int count = 0;
        for (int x = 0; x < fb.width(); ++x) {
            if (fb.get_pixel(x, y).a > 0.0f) ++count;
        }
        return count;
    };

    const int flat_visible    = count_visible_row(*flat,    100);
    const int rotated_visible = count_visible_row(*rotated, 100);
    CHECK(rotated_visible < flat_visible);
}
