#include <doctest/doctest.h>

#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <src/render_graph/builder/graph_builder_pipeline.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <cmath>

using namespace chronon3d;

namespace {

Composition make_look_at_subject_comp() {
    return composition({.name = "TwoNodeCameraSubjectLock", .width = 256,
                        .height = 160, .duration = 40}, [](const FrameContext& ctx) {
        SceneBuilder scene(ctx);
        scene.ambient_light(Color::white(), 1.0f);
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

TEST_CASE("camera depth sorting draws far layers before near layers") {
    std::vector<graph::LayerGraphItem> layers(3);
    layers[0].depth = 700.0f;
    layers[0].insertion_index = 2;
    layers[1].depth = 950.0f;
    layers[1].insertion_index = 0;
    layers[2].depth = 820.0f;
    layers[2].insertion_index = 1;

    graph::detail::sort_camera25d_layers(layers);
    REQUIRE(layers.size() == 3);
    CHECK(layers[0].depth == doctest::Approx(950.0f));
    CHECK(layers[1].depth == doctest::Approx(820.0f));
    CHECK(layers[2].depth == doctest::Approx(700.0f));
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
    constexpr FrameRate fps{30, 1};
    AnimatedValue<float> early{1.0f};
    early.key(Frame{0}, 1.0f).key(Frame{39}, 1.0f);
    AnimatedValue<float> middle{0.0f};
    middle.key(Frame{0}, 0.0f).key(Frame{12}, 1.0f);
    AnimatedValue<float> late{0.0f};
    late.key(Frame{24}, 0.0f).key(Frame{34}, 1.0f);

    const auto time = [fps](int frame) {
        return SampleTime::from_frame_int(Frame{frame}, fps);
    };
    const float early_12 = early.evaluate(time(12));
    const float middle_12 = middle.evaluate(time(12));
    const float late_12 = late.evaluate(time(12));
    CHECK(early_12 == doctest::Approx(1.0f));
    CHECK(middle_12 == doctest::Approx(1.0f));
    CHECK(late_12 == doctest::Approx(0.0f));
    CHECK(early.evaluate(time(34)) == doctest::Approx(early_12));
    CHECK(middle.evaluate(time(34)) == doctest::Approx(middle_12));
    CHECK(late.evaluate(time(34)) == doctest::Approx(1.0f));
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
