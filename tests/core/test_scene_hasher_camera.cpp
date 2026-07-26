#include <doctest/doctest.h>

#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
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
    l.kind = LayerKind::Normal;
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
    cam.is_animated = false; // produced by the canonical camera evaluator
                              // when no property is keyframed
    scene.set_camera_2_5d(cam);

    SceneHasher hasher;
    CHECK(hasher.is_static_scene(scene));
}

TEST_CASE("SceneHasher: enabled animated camera (is_animated=true) is NOT static") {
    Scene scene;
    scene.add_layer(make_inert_layer("bg"));

    // Simulate what the canonical camera evaluator produces: a Camera2_5D
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
