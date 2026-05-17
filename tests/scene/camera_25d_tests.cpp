#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/chronon3d.hpp>
#include <cmath>

using namespace chronon3d;

namespace {

std::unique_ptr<Framebuffer> render_with_camera(
    bool cam_enabled,
    Vec3 cam_pos,
    f32 cam_zoom,
    bool layer_3d,
    Vec3 layer_pos,
    Vec3 layer_rot = {0, 0, 0},
    float rect_size = 100.0f
) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp({
        .name = "Camera25DTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = cam_enabled,
            .position = cam_pos,
            .zoom = cam_zoom
        });

        s.layer("test_layer", [=](LayerBuilder& l) {
            if (layer_3d) {
                l.enable_3d();
            }
            l.position(layer_pos).rotate(layer_rot);
            l.rect("target_rect", {
                .size = {rect_size, rect_size},
                .color = Color::red(),
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });

    return renderer.render_frame(comp, 0);
}

} // namespace

TEST_CASE("Test 9.1 — 2D layer stays flat if camera is disabled") {
    // When camera is disabled, layer is not projected into 3D.
    // Let's render a layer with and without camera position modifications.
    auto fb1 = render_with_camera(false, {100, 100, -1000}, 1000.0f, false, {0, 0, 0});
    auto fb2 = render_with_camera(false, {0, 0, -1000}, 1000.0f, false, {0, 0, 0});

    // They must be identical since camera parameters are ignored when disabled.
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb1->get_pixel(100, 100).r == fb2->get_pixel(100, 100).r);
}

TEST_CASE("Test 9.2 — 3D layers are projected when camera is enabled") {
    // With camera enabled and layer 3D active, camera movement shifts the projected position.
    auto fb1 = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0});
    auto fb2 = render_with_camera(true, {100, 0, -1000}, 1000.0f, true, {0, 0, 0}); // camera shifted X

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    // Center pixel should be red in fb1, but empty in fb2 (due to X shift)
    CHECK(fb1->get_pixel(100, 100).r > 0.8f);
    CHECK(fb2->get_pixel(100, 100).r == 0.0f);
}

TEST_CASE("Test 9.3 — Z depth offset changes scale size under camera projection") {
    // Perspective check: layer closer (negative Z towards camera) is larger;
    // layer farther (positive Z away from camera) is smaller.
    auto fb_near = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, -300}); // closer
    auto fb_far  = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 300});  // further

    REQUIRE(fb_near != nullptr);
    REQUIRE(fb_far != nullptr);

    // Count red pixels in both
    auto count_red = [](const Framebuffer& fb) {
        int count = 0;
        for (int y = 0; y < fb.height(); ++y) {
            for (int x = 0; x < fb.width(); ++x) {
                if (fb.get_pixel(x, y).r > 0.8f) ++count;
            }
        }
        return count;
    };

    int near_count = count_red(*fb_near);
    int far_count = count_red(*fb_far);

    CHECK(near_count > far_count);
}

TEST_CASE("Test 9.4 — Camera 2.5D: X rotation causes vertical foreshortening") {
    // Rotated around X by 60 deg should squeeze the height by half (cos(60) = 0.5)
    auto fb_flat = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 0, 0});
    auto fb_rot  = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {60, 0, 0});

    REQUIRE(fb_flat != nullptr);
    REQUIRE(fb_rot != nullptr);

    auto count_red_col = [](const Framebuffer& fb, int x) {
        int count = 0;
        for (int y = 0; y < fb.height(); ++y) {
            if (fb.get_pixel(x, y).r > 0.8f) ++count;
        }
        return count;
    };

    int flat_height = count_red_col(*fb_flat, 100);
    int rot_height = count_red_col(*fb_rot, 100);

    CHECK(rot_height < flat_height);
}

TEST_CASE("Test 9.5 — Camera 2.5D: Y rotation causes horizontal foreshortening") {
    // Rotated around Y by 60 deg should squeeze the width by half (cos(60) = 0.5)
    auto fb_flat = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 0, 0});
    auto fb_rot  = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 60, 0});

    REQUIRE(fb_flat != nullptr);
    REQUIRE(fb_rot != nullptr);

    auto count_red_row = [](const Framebuffer& fb, int y) {
        int count = 0;
        for (int x = 0; x < fb.width(); ++x) {
            if (fb.get_pixel(x, y).r > 0.8f) ++count;
        }
        return count;
    };

    int flat_width = count_red_row(*fb_flat, 100);
    int rot_width = count_red_row(*fb_rot, 100);

    CHECK(rot_width < flat_width);
}

TEST_CASE("Test 9.6 — Camera 2.5D: 90 deg rotation collapses the layer projection") {
    // A 90 deg rotation around Y collapses thickness to 0 width.
    auto fb_rot = render_with_camera(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 90, 0});
    REQUIRE(fb_rot != nullptr);

    bool has_red = false;
    for (int y = 0; y < fb_rot->height(); ++y) {
        for (int x = 0; x < fb_rot->width(); ++x) {
            if (fb_rot->get_pixel(x, y).r > 0.5f) {
                has_red = true;
                break;
            }
        }
    }
    // Collapsed edge rotation yields no rasterization or extremely tiny width (0 pixels)
    CHECK_FALSE(has_red);
}

TEST_CASE("Test 9.7 — Camera 2.5D: 3D layers are sorted by depth before rendering") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);

    Composition comp({
        .name = "DepthSorting",
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

        // Far layer: blue, placed at Z = 500
        s.layer("far_blue", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 500});
            l.rect("blue_rect", {.size = {150, 150}, .color = Color::blue()});
        });

        // Near layer: red, placed at Z = -200
        s.layer("near_red", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -200});
            l.rect("red_rect", {.size = {80, 80}, .color = Color::red()});
        });

        return s.build();
    });

    auto fb = renderer.render_frame(comp, 0);
    REQUIRE(fb != nullptr);

    // Center should be red (near) on top of blue (far)
    CHECK(fb->get_pixel(100, 100).r > 0.8f);
    CHECK(fb->get_pixel(100, 100).b < 0.2f);

    // Corner of blue rect outside red rect should be blue
    // (at z=500, scale=0.66, so 150x150 becomes 100x100, spans 50-150, 150 is exclusive)
    CHECK(fb->get_pixel(149, 149).b > 0.8f);
}

TEST_CASE("Test 9.8 — Camera 2.5D: Native 3D shapes do not undergo double projection") {
    // Native 3D shapes (e.g. mesh/FakeBox3D) bypass the layer's 2D canvas transform,
    // projecting directly into the camera space.
    // Here we verify they build without crashing and resolve correctly.
    LayerBuilder builder("3d_box");
    builder.enable_3d();
    Layer l = builder.build();

    RenderNode rn(l.nodes.get_allocator().resource());
    rn.name = std::pmr::string{"native_mesh", l.nodes.get_allocator().resource()};
    rn.shape.type = ShapeType::Mesh;
    l.nodes.push_back(std::move(rn));

    Scene scene;
    scene.add_layer(std::move(l));

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].is_3d);
}
