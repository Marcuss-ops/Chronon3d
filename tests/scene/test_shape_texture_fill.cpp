#include <doctest/doctest.h>
#include <chronon3d/scene/builders/shape_texture_fill.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/layer/track_matte.hpp>

#include <tests/helpers/render_scene_graph.hpp>
#include <tests/helpers/test_utils.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

#include <filesystem>
#include <stdexcept>

using namespace chronon3d;

namespace {

ShapeTextureFillSpec valid_spec() {
    ShapeTextureFillSpec spec;
    spec.node_name = "flag";
    spec.shape_layer = "shape";
    spec.image.size = Vec2{320.0f, 180.0f};
    spec.image.fit = FitMode::Cover;
    spec.image.opacity = 0.75f;
    return spec;
}

}  // namespace

TEST_CASE("ShapeTextureFill: sets alpha track matte to the shape layer") {
    LayerBuilder builder("flag_layer", SampleTime{});
    shape_texture_fill(builder, valid_spec());
    Layer layer = builder.build();

    CHECK(layer.track_matte.type == TrackMatteType::Alpha);
    CHECK(layer.track_matte.source_layer == "shape");
    CHECK(layer.track_matte.active());
    CHECK_FALSE(layer.nodes.empty());
}

TEST_CASE("ShapeTextureFill: preserves every authored image fit mode") {
    const FitMode modes[] = {
        FitMode::Cover,
        FitMode::Contain,
        FitMode::Stretch,
        FitMode::None,
    };

    for (const auto mode : modes) {
        LayerBuilder builder("flag_layer", SampleTime{});
        auto spec = valid_spec();
        spec.image.fit = mode;
        shape_texture_fill(builder, spec);
        const Layer layer = builder.build();

        REQUIRE(layer.nodes.size() == 1);
        REQUIRE(layer.nodes[0].shape.type() == ShapeType::Image);
        CHECK(layer.nodes[0].shape.image().fit == mode);
    }
}

TEST_CASE("ShapeTextureFill: rejects empty names") {
    LayerBuilder builder("flag_layer", SampleTime{});
    auto spec = valid_spec();
    spec.node_name.clear();
    CHECK_THROWS_AS(shape_texture_fill(builder, spec), std::invalid_argument);

    spec = valid_spec();
    spec.shape_layer.clear();
    CHECK_THROWS_AS(shape_texture_fill(builder, spec), std::invalid_argument);
}

TEST_CASE("ShapeTextureFill: rejects non-positive size") {
    LayerBuilder builder("flag_layer", SampleTime{});
    auto spec = valid_spec();
    spec.image.size = Vec2{0.0f, 180.0f};
    CHECK_THROWS_AS(shape_texture_fill(builder, spec), std::invalid_argument);
}

TEST_CASE("ShapeTextureFill: rejects opacity outside [0, 1]") {
    LayerBuilder builder("flag_layer", SampleTime{});
    auto spec = valid_spec();
    spec.image.opacity = 1.5f;
    CHECK_THROWS_AS(shape_texture_fill(builder, spec), std::invalid_argument);
    spec.image.opacity = -0.1f;
    CHECK_THROWS_AS(shape_texture_fill(builder, spec), std::invalid_argument);
}

TEST_CASE("ShapeTextureFill: rendered image is clipped to the shape alpha") {
    namespace fs = std::filesystem;
    const fs::path dir = "output/debug/shape_texture_fill";
    fs::create_directories(dir);
    const auto png = dir / "solid_blue_8x8.png";
    Framebuffer src(8, 8);
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            src.pixels_row(y)[x] = Color::blue();
        }
    }
    REQUIRE(save_png(src, png.string()));

    Scene scene;
    {
        // Shape: 100x100 white rect centred on the canvas.
        LayerBuilder shape("shape", SampleTime{});
        shape.duration(60);
        shape.rect("shape_rect", RectParams{.size = Vec2{100.0f, 100.0f}, .color = Color::white()});
        scene.add_layer(shape.build());
    }
    {
        // Image: 200x200 blue, larger than the shape, matted by it.
        LayerBuilder fill("flag", SampleTime{});
        fill.duration(60);
        ShapeTextureFillSpec spec;
        spec.node_name = "flag_img";
        spec.shape_layer = "shape";
        spec.image.asset_path = png.string();
        spec.image.size = Vec2{200.0f, 200.0f};
        spec.image.fit = FitMode::Stretch;
        spec.image.opacity = 1.0f;
        shape_texture_fill(fill, spec);
        scene.add_layer(fill.build());
    }

    auto renderer = test::make_renderer();
    Camera camera;
    auto fb = test::render_scene_graph(renderer, scene, camera, 200, 200, 30.0f);
    REQUIRE(fb != nullptr);

    // Inside the shape: the blue image shows through.
    const Color inside = fb->get_pixel(100, 100);
    CHECK(inside.b > 0.9f);
    CHECK(inside.r < 0.1f);
    CHECK(inside.a > 0.9f);

    // Outside the shape but inside the 200x200 image: matte must hide it.
    const Color outside = fb->get_pixel(10, 10);
    CHECK(outside.a < 0.05f);
}

TEST_CASE("ShapeTextureFill: CPU pixels follow Cover Contain Stretch and None") {
    namespace fs = std::filesystem;
    const fs::path dir = "output/debug/shape_texture_fill";
    fs::create_directories(dir);
    const auto png = dir / "fit_modes_8x4.png";

    // Four vertical bands make source cropping and destination letterboxing
    // observable without depending on a photographic golden.
    Framebuffer source(8, 4);
    const Color bands[] = {
        Color{1.0f, 0.0f, 0.0f, 1.0f},
        Color{0.0f, 1.0f, 0.0f, 1.0f},
        Color{0.0f, 0.0f, 1.0f, 1.0f},
        Color{1.0f, 1.0f, 0.0f, 1.0f},
    };
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.pixels_row(y)[x] = bands[x / 2];
        }
    }
    REQUIRE(save_png(source, png.string()));

    Camera camera;
    const auto render_mode = [&](FitMode mode) {
        // Isolate static fit metadata from graph-cache reuse between scenes.
        auto renderer = test::make_renderer();
        Scene scene;
        LayerBuilder shape("shape", SampleTime{});
        shape.duration(60);
        shape.rect("shape_rect", RectParams{
            .size = Vec2{160.0f, 160.0f}, .color = Color::white()});
        scene.add_layer(shape.build());

        LayerBuilder fill("fill", SampleTime{});
        fill.duration(60);
        ShapeTextureFillSpec spec;
        spec.node_name = "texture";
        spec.shape_layer = "shape";
        spec.image.asset_path = png.string();
        spec.image.size = Vec2{200.0f, 200.0f};
        spec.image.fit = mode;
        spec.image.opacity = 1.0f;
        shape_texture_fill(fill, spec);
        scene.add_layer(fill.build());

        return test::render_scene_graph(renderer, scene, camera, 200, 200, 30.0f);
    };
    const auto is_red = [](const Color& c) { return c.r > 0.8f && c.g < 0.2f && c.b < 0.2f; };
    const auto is_green = [](const Color& c) { return c.g > 0.8f && c.r < 0.2f && c.b < 0.2f; };
    const auto is_blue = [](const Color& c) { return c.b > 0.8f && c.r < 0.2f && c.g < 0.2f; };
    const auto is_yellow = [](const Color& c) { return c.r > 0.8f && c.g > 0.8f && c.b < 0.2f; };

    const auto stretched = render_mode(FitMode::Stretch);
    REQUIRE(stretched != nullptr);
    CHECK(is_red(stretched->get_pixel(30, 100)));
    CHECK(is_green(stretched->get_pixel(80, 100)));
    CHECK(is_blue(stretched->get_pixel(120, 100)));
    CHECK(is_yellow(stretched->get_pixel(170, 100)));

    const auto contained = render_mode(FitMode::Contain);
    REQUIRE(contained != nullptr);
    const Color contain_letterbox = contained->get_pixel(100, 30);
    CHECK(contain_letterbox.a < 0.05f);
    CHECK(is_red(contained->get_pixel(30, 100)));
    CHECK(is_yellow(contained->get_pixel(170, 100)));

    const auto covered = render_mode(FitMode::Cover);
    REQUIRE(covered != nullptr);
    const Color cover_left = covered->get_pixel(55, 100);
    const Color cover_right = covered->get_pixel(145, 100);
    CHECK(is_green(cover_left));
    CHECK(is_blue(cover_right));

    const auto unscaled = render_mode(FitMode::None);
    REQUIRE(unscaled != nullptr);
    const Color none0 = unscaled->get_pixel(97, 100);
    const Color none1 = unscaled->get_pixel(99, 100);
    const Color none2 = unscaled->get_pixel(101, 100);
    const Color none3 = unscaled->get_pixel(103, 100);
    CHECK(is_red(none0));
    CHECK(is_green(none1));
    CHECK(is_blue(none2));
    CHECK(is_yellow(none3));
}
