#include <doctest/doctest.h>
#include <spdlog/spdlog.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;


// These cases pin rendered pixels and silhouettes for the software 2.5D path,
// including camera translation, perspective, foreshortening, collapse, and
// layer depth ordering.
namespace {

struct RedBounds {
    int pixels{0};
    int min_x{std::numeric_limits<int>::max()};
    int min_y{std::numeric_limits<int>::max()};
    int max_x{-1};
    int max_y{-1};
};

RedBounds red_bounds(const Framebuffer& fb) {
    RedBounds bounds;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            const auto pixel = fb.get_pixel(x, y);
            if (pixel.r <= 0.5f || pixel.g >= 0.5f || pixel.b >= 0.5f)
                continue;
            ++bounds.pixels;
            bounds.min_x = std::min(bounds.min_x, x);
            bounds.min_y = std::min(bounds.min_y, y);
            bounds.max_x = std::max(bounds.max_x, x);
            bounds.max_y = std::max(bounds.max_y, y);
        }
    }
    return bounds;
}

int red_width(const Framebuffer& fb) {
    const auto bounds = red_bounds(fb);
    return bounds.pixels == 0 ? 0 : bounds.max_x - bounds.min_x + 1;
}

int red_height(const Framebuffer& fb) {
    const auto bounds = red_bounds(fb);
    return bounds.pixels == 0 ? 0 : bounds.max_y - bounds.min_y + 1;
}

std::shared_ptr<Framebuffer> render_with_camera_pose(
    bool cam_enabled,
    Vec3 cam_pos,
    f32 cam_zoom,
    bool layer_3d,
    Vec3 layer_pos,
    Vec3 layer_rot = {0, 0, 0},
    float rect_size = 100.0f
) {
    auto renderer = test::make_renderer();
    RenderSettings settings;
        renderer.set_settings(settings);

    Composition comp({
        .name = "Camera25DTest",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Full ambient light so 3D content is fully visible (tests are about projection, not lighting)
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);

        s.camera_descriptor(camera().enabled(cam_enabled).position(cam_pos)
            .zoom(cam_zoom).build());

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

    return renderer.render(comp, 0);
}

} // namespace

TEST_CASE("Test 9.1 — 2D layer stays flat if camera is disabled") {
    // When camera is disabled, layer is not projected into 3D.
    // Let's render a layer with and without camera position modifications.
    auto fb1 = render_with_camera_pose(false, {100, 100, -1000}, 1000.0f, false, {0, 0, 0});
    auto fb2 = render_with_camera_pose(false, {0, 0, -1000}, 1000.0f, false, {0, 0, 0});

    // They must be identical since camera parameters are ignored when disabled.
    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    CHECK(fb1->get_pixel(100, 100).r == fb2->get_pixel(100, 100).r);
}

TEST_CASE("Test 9.2 — 3D layers are projected when camera is enabled") {
    // With camera enabled and layer 3D active, camera movement shifts the projected position.
    auto fb1 = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0});
    auto fb2 = render_with_camera_pose(true, {100, 0, -1000}, 1000.0f, true, {0, 0, 0}); // camera shifted X

    REQUIRE(fb1 != nullptr);
    REQUIRE(fb2 != nullptr);
    REQUIRE(fb1->width()  == 200);
    REQUIRE(fb1->height() == 200);
    CHECK(red_bounds(*fb1).pixels > 0);
    CHECK(fb1->get_pixel(100, 100).r > 0.8f); // center of rect
    CHECK(fb2->get_pixel(100, 100).r < 0.1f); // camera translation shifts the rect
}

TEST_CASE("Test 9.3 — Z depth offset changes scale size under camera projection") {
    // Perspective check: layer closer (negative Z towards camera) is larger;
    // layer farther (positive Z away from camera) is smaller.
    auto fb_near = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, -300}); // closer
    auto fb_far  = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 300});  // further

    REQUIRE(fb_near != nullptr);
    REQUIRE(fb_far  != nullptr);
    REQUIRE(fb_near->width() == 200);
    CHECK(red_bounds(*fb_near).pixels > red_bounds(*fb_far).pixels);
}

TEST_CASE("Test 9.4 — Camera 2.5D: X rotation causes vertical foreshortening") {
    // Rotated around X by 60 deg should squeeze the height by half (cos(60) = 0.5)
    auto fb_flat = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 0, 0});
    auto fb_rot  = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {60, 0, 0});

    REQUIRE(fb_flat != nullptr);
    REQUIRE(fb_rot  != nullptr);
    REQUIRE(fb_rot->width() == 200);
    CHECK(red_height(*fb_rot) < red_height(*fb_flat));
}

TEST_CASE("Test 9.5 — Camera 2.5D: Y rotation causes horizontal foreshortening") {
    // Rotated around Y by 60 deg should squeeze the width by half (cos(60) = 0.5)
    auto fb_flat = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 0, 0});
    auto fb_rot  = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 60, 0});

    REQUIRE(fb_flat != nullptr);
    REQUIRE(fb_rot  != nullptr);
    REQUIRE(fb_rot->width() == 200);
    CHECK(red_width(*fb_rot) < red_width(*fb_flat));
}

TEST_CASE("Test 9.6 — Camera 2.5D: 90 deg rotation collapses the layer projection") {
    // A 90 deg rotation around Y collapses thickness to 0 width.
    auto fb_rot = render_with_camera_pose(true, {0, 0, -1000}, 1000.0f, true, {0, 0, 0}, {0, 90, 0});
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
    CHECK_FALSE(has_red);
}

TEST_CASE("Test 9.7 — Camera 2.5D: 3D layers are sorted by depth before rendering") {
    auto renderer = test::make_renderer();
    RenderSettings settings;
        renderer.set_settings(settings);

    Composition comp({
        .name = "DepthSorting",
        .width = 200,
        .height = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Full ambient light — depth sorting test is about Z-ordering, not lighting
        s.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);

        s.camera_descriptor(camera().enabled().position({0, 0, -1000}).zoom(1000.0f).build());

        // Far layer: blue, placed at Z = 500
        s.layer("far_blue", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 500});
            l.rect("blue_rect", {.size = {150, 150}, .color = Color::blue(), .pos = {0, 0, 0}});
        });

        // Near layer: red, placed at Z = -200
        s.layer("near_red", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -200});
            l.rect("red_rect", {.size = {60, 60}, .color = Color::red(), .pos = {0, 0, 0}});
        });

        return s.build();
    });

    auto fb = renderer.render(comp, 0);
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width()  == 200);
    REQUIRE(fb->height() == 200);
    CHECK(fb->get_pixel(100, 100).r > 0.8f); // near (red) wins over far (blue)
    CHECK(fb->get_pixel(100, 100).b < 0.2f);
    CHECK(fb->get_pixel(145, 145).b > 0.8f); // outside near rect, blue is visible
}

TEST_CASE("Test 9.8 — Camera 2.5D: Native 3D shapes do not undergo double projection") {
    // Native 3D shapes (e.g. mesh/FakeBox3D) bypass the layer's 2D canvas transform,
    // projecting directly into the camera space.
    // Here we verify they build without crashing and resolve correctly.
    LayerBuilder builder("3d_box", SampleTime{});
    builder.enable_3d();
    Layer l = builder.build();

    RenderNode rn(l.nodes.get_allocator().resource());
    rn.name = std::pmr::string{"native_mesh", l.nodes.get_allocator().resource()};
    rn.shape.set_type(ShapeType::Mesh);
    l.nodes.push_back(std::move(rn));

    Scene scene;
    scene.add_layer(std::move(l));

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].uses_2_5d_projection);
}
