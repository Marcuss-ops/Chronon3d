#include <doctest/doctest.h>
#include <chronon3d/evaluation/timeline_evaluator.hpp>
#include <chronon3d/description/scene_description.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static SceneDescription make_scene_with_layer(LayerDesc ld) {
    SceneDescription s;
    s.width = 1280; s.height = 720;
    s.layers.push_back(std::move(ld));
    return s;
}

// ---------------------------------------------------------------------------
// Time range filtering
// ---------------------------------------------------------------------------
TEST_CASE("TimelineEvaluator: layer outside time range is skipped") {
    LayerDesc ld; ld.name = "hidden";
    ld.time_range = {10, 20};  // active [10, 30)

    auto scene = make_scene_with_layer(ld);
    TimelineEvaluator ev;

    CHECK(ev.evaluate(scene, 5).layers.empty());
    CHECK(ev.evaluate(scene, 30).layers.empty());
    CHECK(ev.evaluate(scene, 10).layers.size() == 1);
    CHECK(ev.evaluate(scene, 29).layers.size() == 1);
}

TEST_CASE("TimelineEvaluator: infinite duration layer always active") {
    LayerDesc ld; ld.name = "always";
    // default LayerTimeRange: duration = -1 (infinite)

    auto scene = make_scene_with_layer(ld);
    TimelineEvaluator ev;

    CHECK(ev.evaluate(scene, 0).layers.size()   == 1);
    CHECK(ev.evaluate(scene, 999).layers.size() == 1);
}

// ---------------------------------------------------------------------------
// Animated property evaluation
// ---------------------------------------------------------------------------
TEST_CASE("TimelineEvaluator: position evaluated at frame") {
    LayerDesc ld; ld.name = "moving";
    ld.position.key(0, Vec3{0,0,0}).key(60, Vec3{120,60,0});

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 30);

    REQUIRE(r.layers.size() == 1);
    CHECK(r.layers[0].world_transform.position.x == doctest::Approx(60.0f));
    CHECK(r.layers[0].world_transform.position.y == doctest::Approx(30.0f));
}

TEST_CASE("TimelineEvaluator: opacity evaluated at frame") {
    LayerDesc ld; ld.name = "fade";
    ld.opacity.key(0, 0.0f).key(60, 1.0f);

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 30);

    CHECK(r.layers[0].opacity == doctest::Approx(0.5f));
}

// ---------------------------------------------------------------------------
// Depth role resolution
// ---------------------------------------------------------------------------
TEST_CASE("TimelineEvaluator: depth role overrides explicit Z") {
    LayerDesc ld; ld.name = "subject";
    ld.is_3d = true;
    ld.depth_role = DepthRole::Subject;         // => z = 0
    ld.position.set(Vec3{640, 360, 999});        // explicit Z ignored when role active

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 0);

    CHECK(r.layers[0].resolved_z == doctest::Approx(0.0f));
    CHECK(r.layers[0].world_transform.position.z == doctest::Approx(0.0f));
}

TEST_CASE("TimelineEvaluator: DepthRole::Foreground maps to z=-500") {
    LayerDesc ld; ld.name = "fg";
    ld.is_3d = true;
    ld.depth_role = DepthRole::Foreground;

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 0);

    CHECK(r.layers[0].resolved_z == doctest::Approx(-500.0f));
}

TEST_CASE("TimelineEvaluator: depth_offset added to role Z") {
    LayerDesc ld; ld.name = "nudged";
    ld.is_3d = true;
    ld.depth_role   = DepthRole::Subject;  // z = 0
    ld.depth_offset = 50.0f;

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 0);

    CHECK(r.layers[0].resolved_z == doctest::Approx(50.0f));
}

TEST_CASE("TimelineEvaluator: no depth role uses explicit Z") {
    LayerDesc ld; ld.name = "explicit-z";
    ld.is_3d = true;
    ld.depth_role = DepthRole::None;
    ld.position.set(Vec3{0, 0, 300});

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 0);

    CHECK(r.layers[0].resolved_z == doctest::Approx(300.0f));
}

// ---------------------------------------------------------------------------
// Effect resolution
// ---------------------------------------------------------------------------
TEST_CASE("TimelineEvaluator: blur effect resolved") {
    LayerDesc ld; ld.name = "blurred";
    ld.effects.emplace_back(BlurEffectDesc{.radius = 12.0f});

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 0);

    CHECK(r.layers[0].resolved_effect.blur_radius == doctest::Approx(12.0f));
}

TEST_CASE("TimelineEvaluator: brightness/contrast effect resolved") {
    LayerDesc ld; ld.name = "bright";
    ld.effects.emplace_back(BrightnessContrastEffectDesc{.brightness=0.3f, .contrast=1.4f});

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 0);

    CHECK(r.layers[0].resolved_effect.brightness == doctest::Approx(0.3f));
    CHECK(r.layers[0].resolved_effect.contrast   == doctest::Approx(1.4f));
}

// ---------------------------------------------------------------------------
// Camera evaluation
// ---------------------------------------------------------------------------
TEST_CASE("TimelineEvaluator: no camera when not set") {
    SceneDescription s; s.width=1280; s.height=720;
    TimelineEvaluator ev;
    CHECK_FALSE(ev.evaluate(s, 0).camera.has_value());
}

TEST_CASE("TimelineEvaluator: animated camera position resolved") {
    SceneDescription s; s.width=1280; s.height=720;
    Camera2_5DDesc cam;
    cam.enabled = true;
    cam.position.key(0, Vec3{0,0,-1000}).key(60, Vec3{300,0,-1000});
    s.camera = cam;

    TimelineEvaluator ev;
    auto r = ev.evaluate(s, 30);

    REQUIRE(r.camera.has_value());
    CHECK(r.camera->position.x == doctest::Approx(150.0f));
    CHECK(r.camera->position.z == doctest::Approx(-1000.0f));
}

// ---------------------------------------------------------------------------
// Metadata passthrough
// ---------------------------------------------------------------------------
TEST_CASE("TimelineEvaluator: frame and resolution preserved") {
    SceneDescription s; s.width=800; s.height=450;
    TimelineEvaluator ev;
    auto r = ev.evaluate(s, 42);

    CHECK(r.frame  == 42);
    CHECK(r.width  == 800);
    CHECK(r.height == 450);
}

TEST_CASE("TimelineEvaluator: visuals copied to evaluated layer") {
    LayerDesc ld; ld.name = "rect-layer";
    ld.visuals.emplace_back(RectParams{.size={200,100}, .color=Color::red(), .pos={0,0,0}});

    TimelineEvaluator ev;
    auto r = ev.evaluate(make_scene_with_layer(ld), 0);

    REQUIRE(r.layers[0].visuals.size() == 1);
    CHECK(std::holds_alternative<RectParams>(r.layers[0].visuals[0]));
}
