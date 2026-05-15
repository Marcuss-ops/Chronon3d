#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

TEST_CASE("Layer transform translates child nodes") {
    Composition comp = composition({
        .name = "LayerTransformTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("layer", [](LayerBuilder& l) {
            l.position({100, 100, 0});
            l.rect("box", {
                .size = {40, 40},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    // Pixel at (100, 100) should be red because the rect is centered at (100, 100) via layer transform
    auto p = fb->get_pixel(100, 100);
    CHECK(p.r > 0.5f);
    CHECK(p.g == 0.0f);
    CHECK(p.b == 0.0f);
}

TEST_CASE("Layer opacity affects child nodes") {
    Composition comp = composition({
        .name = "LayerOpacityTest",
        .width = 100,
        .height = 100,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("layer", [](LayerBuilder& l) {
            l.position({50, 50, 0})
             .opacity(0.5f);
            l.rect("box", {
                .size = {100, 100},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    auto p = fb->get_pixel(50, 50);
    // Background is black (0,0,0). White (1,1,1) with 0.5 opacity on black should be (0.5, 0.5, 0.5)
    CHECK(p.r == doctest::Approx(0.5f));
    CHECK(p.g == doctest::Approx(0.5f));
    CHECK(p.b == doctest::Approx(0.5f));
}

TEST_CASE("Layer draw order is insertion order") {
    Composition comp = composition({
        .name = "LayerDrawOrderTest",
        .width = 100,
        .height = 100,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("red", [](LayerBuilder& l) {
            l.position({50, 50, 0});
            l.rect("red-rect", {
                .size = {60, 60},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });
        });

        s.layer("blue", [](LayerBuilder& l) {
            l.position({50, 50, 0});
            l.rect("blue-rect", {
                .size = {60, 60},
                .color = Color{0, 0, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    auto p = fb->get_pixel(50, 50);
    // Blue layer was added last, so it should be on top
    CHECK(p.b > 0.5f);
    CHECK(p.r < 0.1f);
}

TEST_CASE("Child draw order inside layer is insertion order") {
    Composition comp = composition({
        .name = "LayerChildOrderTest",
        .width = 100,
        .height = 100,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("group", [](LayerBuilder& l) {
            l.position({50, 50, 0});

            l.rect("red", {
                .size = {60, 60},
                .color = Color{1, 0, 0, 1},
                .pos = {0, 0, 0}
            });

            l.rect("green", {
                .size = {40, 40},
                .color = Color{0, 1, 0, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    auto p = fb->get_pixel(50, 50);
    // Green child was added last, so it should be on top of red child
    CHECK(p.g > 0.5f);
    CHECK(p.r < 0.1f);
}

TEST_CASE("Root nodes render before layers") {
    Composition comp = composition({
        .name = "RootBeforeLayersTest",
        .width = 100,
        .height = 100,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.rect("root-red", {
            .size = {80, 80},
            .color = Color{1, 0, 0, 1},
            .pos = {50, 50, 0}
        });

        s.layer("blue-layer", [](LayerBuilder& l) {
            l.position({50, 50, 0});
            l.rect("blue", {
                .size = {60, 60},
                .color = Color{0, 0, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    auto p = fb->get_pixel(50, 50);
    // Blue layer should be on top of root red rect
    CHECK(p.b > 0.5f);
    CHECK(p.r < 0.1f);
}
