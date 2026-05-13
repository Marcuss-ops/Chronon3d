#include <doctest/doctest.h>

#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

TEST_CASE("RenderGraph 2.5D: near layer covers far layer regardless of insertion order") {
    Composition comp = composition({
        .name = "Graph25DSortingTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {200, 200},
            .color = Color::black(),
            .pos = {0, 0, 0}
        });

        // Near red inserted first.
        s.layer("near_red", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -500})
             .rect("red", {
                 .size = {100, 100},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        // Far blue inserted second.
        s.layer("far_blue", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, 1000})
             .rect("blue", {
                 .size = {200, 200},
                 .color = Color::blue(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    auto center = fb->get_pixel(100, 100);

    CHECK(center.r > 0.8f);
    CHECK(center.b < 0.2f);
}

TEST_CASE("RenderGraph 2.5D: 2D overlay stays above previous 3D bin") {
    Composition comp = composition({
        .name = "Graph25DOverlayTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.layer("foreground_3d", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -500})
             .rect("red", {
                 .size = {200, 200},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        // 2D overlay inserted after 3D layer.
        s.layer("overlay_2d", [](LayerBuilder& l) {
            l.rect("white", {
                .size = {80, 80},
                .color = Color::white(),
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    auto center = fb->get_pixel(100, 100);

    CHECK(center.r > 0.9f);
    CHECK(center.g > 0.9f);
    CHECK(center.b > 0.9f);
}

TEST_CASE("RenderGraph 2.5D: layer behind camera is culled") {
    Composition comp = composition({
        .name = "Graph25DCullingTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {200, 200},
            .color = Color{0.2f, 0.2f, 0.2f, 1},
            .pos = {0, 0, 0}
        });

        s.layer("behind_camera", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -1200})
             .rect("magenta", {
                 .size = {200, 200},
                 .color = Color{1, 0, 1, 1},
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    auto center = fb->get_pixel(100, 100);

    // Color{0.2, 0.2, 0.2} sRGB is approx 0.0331 linear.
    CHECK(center.r == doctest::Approx(0.0331f).epsilon(0.01f));
    CHECK(center.g == doctest::Approx(0.0331f).epsilon(0.01f));
    CHECK(center.b == doctest::Approx(0.0331f).epsilon(0.01f));
}

TEST_CASE("RenderGraph parenting: child inherits null position") {
    Composition comp = composition({
        .name = "Parenting2DTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.null_layer("rig", [](LayerBuilder& l) {
            l.position({50, 0, 0});
        });

        s.layer("child", [](LayerBuilder& l) {
            l.parent("rig")
             .rect("white", {
                 .size = {40, 40},
                 .color = Color::white(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    // Center-origin: world x=50 should appear around pixel x=150.
    auto p = fb->get_pixel(150, 100);

    CHECK(p.r > 0.9f);
    CHECK(p.g > 0.9f);
    CHECK(p.b > 0.9f);
}

TEST_CASE("RenderGraph parenting: 3D child inherits parent Z before projection") {
    Composition comp = composition({
        .name = "Parenting25DTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        // Parent moves child closer: z = -500, so scale should become 2x.
        s.null_layer("rig", [](LayerBuilder& l) {
            l.position({0, 0, -500});
        });

        s.layer("child", [](LayerBuilder& l) {
            l.parent("rig")
             .enable_3d()
             .rect("red", {
                 .size = {40, 40},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    auto fb = renderer.render_frame(comp, 0);

    // If scale became 2x, rect size is 80x80.
    // Center is (100, 100). Rect should cover from x=60 to x=140.
    auto center = fb->get_pixel(100, 100);
    auto right  = fb->get_pixel(135, 100);

    CHECK(center.r > 0.8f);
    CHECK(right.r > 0.8f);
}
