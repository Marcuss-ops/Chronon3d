#include <doctest/doctest.h>

#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <cmath>

using namespace chronon3d;

namespace {

Composition make_depth_stack() {
    return composition({.name = "DepthStackContract", .width = 256,
                        .height = 160, .duration = 40}, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        scene.camera_descriptor(camera().enabled(true).position({0, 0, -700})
            .zoom(700).look_at({0, 0, 0}).build());

        scene.layer("far-sky", [](LayerBuilder& layer) {
            layer.enable_3d().position({0, 0, 250})
                .rect("sky", {.size = {210, 130}, .color = {0.05f, 0.15f, 0.85f, 1},
                               .pos = {-105, -65, 0}});
        });
        scene.layer("middle-cloud", [](LayerBuilder& layer) {
            layer.enable_3d().position({18, 0, 120})
                .rect("cloud", {.size = {140, 90}, .color = {0.05f, 0.85f, 0.15f, 1},
                                 .pos = {-70, -45, 0}});
        });
        scene.layer("near-subject", [](LayerBuilder& layer) {
            layer.enable_3d().position({-14, 0, 0})
                .rect("subject", {.size = {74, 58}, .color = {0.9f, 0.05f, 0.05f, 1},
                                   .pos = {-37, -29, 0}});
        });
        return scene.build();
    });
}

Composition make_staggered_opacity_stack() {
    return composition({.name = "StaggeredDepthOpacity", .width = 256,
                        .height = 160, .duration = 40}, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        scene.camera_descriptor(camera().enabled(true).position({0, 0, -700})
            .zoom(700).look_at({0, 0, 0}).build());
        scene.layer("early", [](LayerBuilder& layer) {
            layer.enable_3d().position({-70, 0, 0})
                .rect("early-card", {.size = {44, 44}, .color = {0.95f, 0.1f, 0.05f, 1},
                                      .pos = {-22, -22, 0}});
            layer.opacity_anim().key(0, 1.0f).key(39, 1.0f);
        });
        scene.layer("middle", [](LayerBuilder& layer) {
            layer.enable_3d().position({0, 0, 0})
                .rect("middle-card", {.size = {44, 44}, .color = {0.05f, 0.9f, 0.1f, 1},
                                       .pos = {-22, -22, 0}});
            layer.opacity_anim().key(0, 0.0f).key(12, 1.0f);
        });
        scene.layer("late", [](LayerBuilder& layer) {
            layer.enable_3d().position({70, 0, 0})
                .rect("late-card", {.size = {44, 44}, .color = {0.05f, 0.2f, 0.95f, 1},
                                     .pos = {-22, -22, 0}});
            layer.opacity_anim().key(0, 0.0f).key(24, 1.0f);
        });
        return scene.build();
    });
}

Composition make_look_at_subject_comp() {
    return composition({.name = "TwoNodeCameraSubjectLock", .width = 256,
                        .height = 160, .duration = 40}, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        camera_v1::CameraDescriptor descriptor;
        descriptor.id = "two-node-subject-lock";
        descriptor.base.enabled = true;
        descriptor.base.projection = camera_v1::ZoomProjection{AnimatedValue<f32>{700.0f}};
        camera_v1::PoseTracksSource motion;
        motion.position.set(Vec3{0, 0, -700});
        motion.position.key(0, Vec3{0, 0, -700});
        motion.position.key(30, Vec3{130, 0, -700});
        motion.target.set(Vec3{0, 0, 0});
        motion.use_target = true;
        descriptor.source = motion;
        descriptor.orientation = camera_v1::LookAtPoint{Vec3{0, 0, 0}};
        scene.camera_descriptor(std::move(descriptor));
        scene.layer("subject", [](LayerBuilder& layer) {
            layer.enable_3d().position({0, 0, 0})
                .rect("subject-mark", {.size = {24, 24}, .color = {0.95f, 0.08f, 0.04f, 1},
                                        .pos = {-12, -12, 0}});
        });
        return scene.build();
    });
}

} // namespace

TEST_CASE("same camera shot composites multiple 3D layers by camera depth") {
    const auto frame = test::render_modular(make_depth_stack());
    REQUIRE(frame);
    const Color center = frame->get_pixel(frame->width() / 2, frame->height() / 2);
    CHECK(center.r > center.g);
    CHECK(center.r > center.b);

    // At an exposed edge of the middle card, green must cover the far blue
    // background; farther right, the sky remains visible.
    const Color middle_edge = frame->get_pixel(frame->width() / 2 + 48,
                                                frame->height() / 2);
    const Color sky_edge = frame->get_pixel(frame->width() / 2 + 85,
                                             frame->height() / 2);
    CHECK(middle_edge.g > middle_edge.b);
    CHECK(sky_edge.b > sky_edge.g);
}

TEST_CASE("projected card scale follows camera-space distance") {
    Camera2_5D camera_state;
    camera_state.enabled = true;
    camera_state.position = {0, 0, -700};
    camera_state.point_of_interest_enabled = true;
    camera_state.point_of_interest = {0, 0, 0};
    camera_state.zoom = 700;
    camera_state.optics_mode = CameraOpticsMode::Zoom;

    Transform near_layer;
    near_layer.position = {0, 0, 0};
    Transform far_layer;
    far_layer.position = {0, 0, 300};
    const auto near_projected = project_layer_2_5d(near_layer, camera_state, 1000, 1000);
    const auto far_projected = project_layer_2_5d(far_layer, camera_state, 1000, 1000);
    REQUIRE(near_projected.visible);
    REQUIRE(far_projected.visible);
    CHECK(near_projected.perspective_scale > far_projected.perspective_scale);
    CHECK(near_projected.depth < far_projected.depth);
    CHECK(near_projected.perspective_scale / far_projected.perspective_scale ==
          doctest::Approx(far_projected.depth / near_projected.depth).epsilon(0.02));
}

TEST_CASE("staggered opacity keys reveal independent layers without changing earlier cards") {
    const auto scene = make_staggered_opacity_stack();
    const auto at_12 = test::render_modular(scene, Frame{12});
    const auto at_34 = test::render_modular(scene, Frame{34});
    REQUIRE(at_12);
    REQUIRE(at_34);

    const int cy = at_12->height() / 2;
    const int cx = at_12->width() / 2;
    const Color early_12 = at_12->get_pixel(cx - 70, cy);
    const Color middle_12 = at_12->get_pixel(cx, cy);
    const Color late_12 = at_12->get_pixel(cx + 70, cy);
    const Color early_34 = at_34->get_pixel(cx - 70, cy);
    const Color middle_34 = at_34->get_pixel(cx, cy);
    const Color late_34 = at_34->get_pixel(cx + 70, cy);

    CHECK(early_12.r > 0.5f);
    CHECK(middle_12.g > 0.5f);
    CHECK(late_12.b < 0.2f);
    CHECK(early_34.r == doctest::Approx(early_12.r).epsilon(0.03));
    CHECK(middle_34.g == doctest::Approx(middle_12.g).epsilon(0.03));
    CHECK(late_34.b > 0.5f);
}

TEST_CASE("two-node camera keeps its subject framed while the camera moves") {
    const auto scene = make_look_at_subject_comp();
    const auto first = test::render_modular(scene, Frame{0});
    const auto moved = test::render_modular(scene, Frame{30});
    REQUIRE(first);
    REQUIRE(moved);

    const auto red_centroid = [](const Framebuffer& frame) {
        double x_sum = 0.0;
        double y_sum = 0.0;
        std::size_t count = 0;
        for (int y = 0; y < frame.height(); ++y)
            for (int x = 0; x < frame.width(); ++x) {
                const Color p = frame.get_pixel(x, y);
                if (p.r > 0.7f && p.r > p.g * 2.0f && p.r > p.b * 2.0f) {
                    x_sum += x;
                    y_sum += y;
                    ++count;
                }
            }
        if (count == 0) return Vec2{-1.0f, -1.0f};
        return Vec2{static_cast<float>(x_sum / count), static_cast<float>(y_sum / count)};
    };

    const Vec2 first_center = red_centroid(*first);
    const Vec2 moved_center = red_centroid(*moved);
    REQUIRE(first_center.x >= 0.0f);
    REQUIRE(moved_center.x >= 0.0f);
    CHECK(glm::length(first_center - moved_center) < 2.0f);
}
