#include <doctest/doctest.h>

#include <chronon3d/render_graph/core/scene_hasher.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

// Minimal layer with no animation and no transitions so the
// layer_is_static* checks all pass and the only signal left for
// the camera test is camera_is_static().
Layer make_inert_layer(const char* name, Frame duration = 60) {
    Layer l;
    l.name = name;
    l.duration = duration;
    l.kind = LayerKind::Shape;
    l.visible = true;
    return l;
}

} // namespace

TEST_CASE("SceneHasher: disabled camera is treated as static") {
    Scene scene;
    scene.add_layer(make_inert_layer("bg"));

    Camera2_5D cam;
    cam.enabled = false;
    cam.is_animated = true; // intentionally wrong: disabled should override
    scene.set_camera_2_5d(cam);

    SceneHasher hasher;
    CHECK(hasher.is_static_scene(scene));
}

TEST_CASE("SceneHasher: enabled static camera (is_animated=false) is static") {
    Scene scene;
    scene.add_layer(make_inert_layer("bg"));

    Camera2_5D cam;
    cam.enabled = true;
    cam.is_animated = false; // produced by AnimatedCamera2_5D::evaluate()
                              // when no property is keyframed
    scene.set_camera_2_5d(cam);

    SceneHasher hasher;
    CHECK(hasher.is_static_scene(scene));
}

TEST_CASE("SceneHasher: enabled animated camera (is_animated=true) is NOT static") {
    Scene scene;
    scene.add_layer(make_inert_layer("bg"));

    // Simulate what AnimatedCamera2_5D::evaluate() produces: a Camera2_5D
    // whose is_animated flag was set because at least one camera property
    // (here zoom) has keyframes.
    Camera2_5D cam;
    cam.enabled = true;
    cam.is_animated = true;
    cam.zoom = 1234.0f;
    scene.set_camera_2_5d(cam);

    SceneHasher hasher;
    CHECK_FALSE(hasher.is_static_scene(scene));
}

TEST_CASE("SceneHasher: AnimatedCamera2_5D::evaluate() propagates is_animated correctly") {
    Scene scene;
    scene.add_layer(make_inert_layer("bg"));

    // Case 1: all defaults → is_animated should be false
    AnimatedCamera2_5D acam_a;
    Camera2_5D cam_a = acam_a.evaluate(Frame{0});
    scene.set_camera_2_5d(cam_a);
    SceneHasher hasher;
    CHECK(hasher.is_static_scene(scene));

    // Replace camera with one that has keyframed zoom → is_animated should be true.
    // We re-use a fresh Scene because the SceneHasher check is one-shot
    // per scene; the new scene with the animated camera must report
    // NOT static.
    Scene scene_anim;
    scene_anim.add_layer(make_inert_layer("bg"));
    AnimatedCamera2_5D acam_b;
    acam_b.zoom.key(Frame{0}, 1000.0f);
    acam_b.zoom.key(Frame{60}, 1200.0f);
    Camera2_5D cam_b = acam_b.evaluate(Frame{30});
    REQUIRE(cam_b.is_animated); // AnimatedCamera2_5D wires the flag
    scene_anim.set_camera_2_5d(cam_b);
    CHECK_FALSE(hasher.is_static_scene(scene_anim));
}
