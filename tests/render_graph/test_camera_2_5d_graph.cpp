#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/chronon3d.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <cmath>

using namespace chronon3d;

namespace {

std::unique_ptr<Framebuffer> render_modular(const Composition& comp, Frame frame = 0) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer.render_frame(comp, frame);
}

Composition make_graph_25d_sorting_test() {
    return composition({
        .name = "Graph25DSortingTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.rect("bg", {
            .size = {200, 200},
            .color = Color::black(),
            .pos = {0, 0, 0}
        });

        s.layer("near_red", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -500})
             .rect("red", {
                 .size = {100, 100},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

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
}

Composition make_graph_25d_overlay_test() {
    return composition({
        .name = "Graph25DOverlayTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
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

        s.layer("overlay_2d", [](LayerBuilder& l) {
            l.rect("white", {
                .size = {80, 80},
                .color = Color::white(),
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

Composition make_graph_25d_culling_test() {
    return composition({
        .name = "Graph25DCullingTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
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
}

Composition make_parenting_2d_test() {
    return composition({
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
}

Composition make_parenting_25d_test() {
    return composition({
        .name = "Parenting25DTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

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
}

Composition make_parenting_orbit_test(f32 parent_rotation_z) {
    return composition({
        .name = "Parenting25DOrbitTest",
        .width = 240,
        .height = 240,
        .duration = 1
    }, [parent_rotation_z](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0, 0, -1000},
            .zoom = 1000.0f
        });

        s.null_layer("rig", [parent_rotation_z](LayerBuilder& l) {
            l.enable_3d()
             .position({0, 0, -500})
             .rotate({0, 0, parent_rotation_z});
        });

        s.layer("child", [](LayerBuilder& l) {
            l.parent("rig")
             .enable_3d()
             .position({100, 0, 0})
             .rect("red", {
                 .size = {40, 40},
                 .color = Color::red(),
                 .pos = {0, 0, 0}
             });
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("RenderGraph 2.5D: near layer covers far layer regardless of insertion order") {
    auto fb = render_modular(make_graph_25d_sorting_test());

    auto center = fb->get_pixel(100, 100);

    CHECK(center.r > 0.8f);
    CHECK(center.b < 0.2f);
}

TEST_CASE("RenderGraph 2.5D: 2D overlay stays above previous 3D bin") {
    auto fb = render_modular(make_graph_25d_overlay_test());

    auto center = fb->get_pixel(100, 100);

    CHECK(center.r > 0.9f);
    CHECK(center.g > 0.9f);
    CHECK(center.b > 0.9f);
}

TEST_CASE("RenderGraph 2.5D: layer behind camera is culled") {
    auto fb = render_modular(make_graph_25d_culling_test());

    auto center = fb->get_pixel(100, 100);

    // Color{0.2, 0.2, 0.2} sRGB is approx 0.0331 linear.
    CHECK(center.r == doctest::Approx(0.0331f).epsilon(0.01f));
    CHECK(center.g == doctest::Approx(0.0331f).epsilon(0.01f));
    CHECK(center.b == doctest::Approx(0.0331f).epsilon(0.01f));
}

TEST_CASE("RenderGraph parenting: child inherits null position") {
    auto fb = render_modular(make_parenting_2d_test());

    // Current modular 2D path is centered-relative: the rig at x=50 should place
    // the child rect around pixel 100 + 50 = 150.
    auto p = fb->get_pixel(150, 110);

    CHECK(p.r > 0.9f);
    CHECK(p.g > 0.9f);
    CHECK(p.b > 0.9f);
}

TEST_CASE("RenderGraph parenting: 3D child inherits parent Z before projection") {
    auto fb = render_modular(make_parenting_25d_test());

    // If scale became 2x, rect size is 80x80.
    // Center is (100, 100). Rect should cover from x=60 to x=140.
    auto center = fb->get_pixel(100, 100);
    auto right  = fb->get_pixel(135, 100);

    CHECK(center.r > 0.8f);
    CHECK(right.r > 0.8f);
}

TEST_CASE("RenderGraph parenting: parent rotation changes child orbit") {
    auto fb0 = render_modular(make_parenting_orbit_test(0.0f));
    auto fb1 = render_modular(make_parenting_orbit_test(45.0f));

    if (fb0) {
        test::save_debug(*fb0, "output/debug/full_3d_parenting/rotation_0.png");
    }
    if (fb1) {
        test::save_debug(*fb1, "output/debug/full_3d_parenting/rotation_45.png");
    }

    REQUIRE(fb0 != nullptr);
    REQUIRE(fb1 != nullptr);

    auto c0 = renderer::bright_centroid(*fb0);
    auto c1 = renderer::bright_centroid(*fb1);
    REQUIRE(c0.has_value());
    REQUIRE(c1.has_value());

    CHECK(std::abs(c0->x - c1->x) > 10.0f);
    CHECK(std::abs(c0->y - c1->y) > 10.0f);
}
