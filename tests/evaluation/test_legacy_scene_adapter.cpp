#include <doctest/doctest.h>
#include <chronon3d/evaluation/legacy_scene_adapter.hpp>
#include <chronon3d/evaluation/timeline_evaluator.hpp>
#include <chronon3d/description/scene_description.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static EvaluatedScene eval_at(SceneDescription& s, Frame f) {
    return TimelineEvaluator{}.evaluate(s, f);
}

// ---------------------------------------------------------------------------
// Round-trip: SceneDescription -> EvaluatedScene -> Scene
// ---------------------------------------------------------------------------
TEST_CASE("LegacySceneAdapter: empty scene converts cleanly") {
    SceneDescription s; s.width=800; s.height=600;
    auto ev  = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    CHECK(scene.layers().empty());
    CHECK(scene.nodes().empty());
}

TEST_CASE("LegacySceneAdapter: layer count preserved") {
    SceneDescription s;
    s.layers.push_back(LayerDesc{.id="a", .name="A"});
    s.layers.push_back(LayerDesc{.id="b", .name="B"});

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    CHECK(scene.layers().size() == 2);
}

TEST_CASE("LegacySceneAdapter: RectParams visual becomes RenderNode") {
    SceneDescription s;
    LayerDesc ld; ld.name = "rect-layer";
    ld.position.set(Vec3{640, 360, 0});
    ld.visuals.emplace_back(RectParams{.size={200,100}, .color=Color::red(), .pos={0,0,0}});
    s.layers.push_back(ld);

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    REQUIRE(scene.layers().size() == 1);
    const auto& layer = scene.layers()[0];
    REQUIRE(layer.nodes.size() == 1);
    const auto& node = layer.nodes[0];
    CHECK(node.shape.type == ShapeType::Rect);
    CHECK(node.shape.rect.size.x == doctest::Approx(200.0f));
    CHECK(node.color.r == doctest::Approx(1.0f));
}

TEST_CASE("LegacySceneAdapter: RoundedRectParams preserved") {
    SceneDescription s;
    LayerDesc ld; ld.name = "rrect";
    ld.visuals.emplace_back(RoundedRectParams{.size={300,150}, .radius=20.0f, .color=Color::blue()});
    s.layers.push_back(ld);

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    const auto& node = scene.layers()[0].nodes[0];
    CHECK(node.shape.type == ShapeType::RoundedRect);
    CHECK(node.shape.rounded_rect.radius == doctest::Approx(20.0f));
}

TEST_CASE("LegacySceneAdapter: TextParams visual preserved") {
    SceneDescription s;
    LayerDesc ld; ld.name = "text-layer";
    ld.visuals.emplace_back(TextParams{
        .content = "HELLO",
        .style   = TextStyle{.font_path="assets/fonts/Inter-Bold.ttf", .size=48, .color=Color::white()},
        .pos     = {0,0,0}
    });
    s.layers.push_back(ld);

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    const auto& node = scene.layers()[0].nodes[0];
    CHECK(node.shape.type == ShapeType::Text);
    CHECK(node.shape.text.text == "HELLO");
    CHECK(node.shape.text.style.size == doctest::Approx(48.0f));
}

TEST_CASE("LegacySceneAdapter: multiple visuals -> multiple nodes per layer") {
    SceneDescription s;
    LayerDesc ld; ld.name = "multi";
    ld.visuals.emplace_back(RectParams{.size={100,100}, .color=Color::red()});
    ld.visuals.emplace_back(CircleParams{.radius=50, .color=Color::green()});
    s.layers.push_back(ld);

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    CHECK(scene.layers()[0].nodes.size() == 2);
    CHECK(scene.layers()[0].nodes[0].shape.type == ShapeType::Rect);
    CHECK(scene.layers()[0].nodes[1].shape.type == ShapeType::Circle);
}

TEST_CASE("LegacySceneAdapter: layer effect carried over") {
    SceneDescription s;
    LayerDesc ld; ld.name = "blurred";
    ld.effects.emplace_back(BlurEffectDesc{.radius=16.0f});
    s.layers.push_back(ld);

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    CHECK(scene.layers()[0].effect.blur_radius == doctest::Approx(16.0f));
}

TEST_CASE("LegacySceneAdapter: camera propagated to Scene") {
    SceneDescription s;
    Camera2_5DDesc cam;
    cam.enabled = true;
    cam.position.set(Vec3{0,0,-1000});
    cam.zoom = 1000.0f;
    s.camera = cam;

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    CHECK(scene.camera_2_5d().enabled);
    CHECK(scene.camera_2_5d().zoom   == doctest::Approx(1000.0f));
    CHECK(scene.camera_2_5d().position.z == doctest::Approx(-1000.0f));
}

TEST_CASE("LegacySceneAdapter: layer 3D flag and depth_role preserved") {
    SceneDescription s;
    LayerDesc ld; ld.name = "subject";
    ld.is_3d      = true;
    ld.depth_role = DepthRole::Subject;
    s.layers.push_back(ld);

    auto ev    = eval_at(s, 0);
    auto scene = LegacySceneAdapter{}.convert(ev);

    CHECK(scene.layers()[0].is_3d);
    CHECK(scene.layers()[0].depth_role == DepthRole::Subject);
}

TEST_CASE("LegacySceneAdapter: animated position resolves into layer transform") {
    SceneDescription s;
    LayerDesc ld; ld.name = "moving";
    ld.position.key(0, Vec3{0,360,0}).key(60, Vec3{1280,360,0});
    s.layers.push_back(ld);

    auto ev30  = eval_at(s, 30);
    auto scene = LegacySceneAdapter{}.convert(ev30);

    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(640.0f));
}
