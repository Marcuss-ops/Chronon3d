#include <doctest/doctest.h>
#include <chronon3d/description/scene_description.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// LayerTimeRange
// ---------------------------------------------------------------------------
TEST_CASE("LayerTimeRange: infinite duration (default)") {
    LayerTimeRange r;
    CHECK(r.contains(0));
    CHECK(r.contains(999));
}

TEST_CASE("LayerTimeRange: finite duration [10, 40)") {
    LayerTimeRange r{10, 30};
    CHECK_FALSE(r.contains(9));
    CHECK(r.contains(10));
    CHECK(r.contains(39));
    CHECK_FALSE(r.contains(40));
}

TEST_CASE("LayerTimeRange: frame before start") {
    LayerTimeRange r{20, 10};
    CHECK_FALSE(r.contains(5));
    CHECK(r.contains(20));
}

// ---------------------------------------------------------------------------
// LayerDesc construction
// ---------------------------------------------------------------------------
TEST_CASE("LayerDesc: default opacity is 1.0") {
    LayerDesc l;
    CHECK(l.opacity.value_at(0) == doctest::Approx(1.0f));
}

TEST_CASE("LayerDesc: default scale is (1,1,1)") {
    LayerDesc l;
    Vec3 s = l.scale.value_at(0);
    CHECK(s.x == doctest::Approx(1.0f));
    CHECK(s.y == doctest::Approx(1.0f));
    CHECK(s.z == doctest::Approx(1.0f));
}

TEST_CASE("LayerDesc: animated position") {
    LayerDesc l;
    l.position.key(0, Vec3{0,0,0}).key(60, Vec3{640,360,0});

    Vec3 mid = l.position.value_at(30);
    CHECK(mid.x == doctest::Approx(320.0f));
    CHECK(mid.y == doctest::Approx(180.0f));
}

TEST_CASE("LayerDesc: visuals and effects vectors") {
    LayerDesc l;
    l.visuals.emplace_back(RectParams{.size={200,100}, .color=Color::red(), .pos={0,0,0}});
    l.effects.emplace_back(BlurEffectDesc{.radius=8.0f});

    CHECK(l.visuals.size() == 1);
    CHECK(l.effects.size() == 1);
    CHECK(std::holds_alternative<RectParams>(l.visuals[0]));
    CHECK(std::holds_alternative<BlurEffectDesc>(l.effects[0]));
}

// ---------------------------------------------------------------------------
// SceneDescription
// ---------------------------------------------------------------------------
TEST_CASE("SceneDescription: default resolution") {
    SceneDescription s;
    CHECK(s.width  == 1280);
    CHECK(s.height == 720);
}

TEST_CASE("SceneDescription: add layers") {
    SceneDescription s;
    s.layers.push_back(LayerDesc{.id="bg", .name="Background"});
    s.layers.push_back(LayerDesc{.id="title", .name="Title"});

    CHECK(s.layers.size() == 2);
    CHECK(s.layers[0].id == "bg");
    CHECK(s.layers[1].name == "Title");
}

TEST_CASE("SceneDescription: optional camera absent by default") {
    SceneDescription s;
    CHECK_FALSE(s.camera.has_value());
}

TEST_CASE("SceneDescription: camera with animated position") {
    SceneDescription s;
    Camera2_5DDesc cam;
    cam.enabled = true;
    cam.position.key(0, Vec3{0,0,-1000}).key(60, Vec3{200,0,-1000});
    s.camera = cam;

    REQUIRE(s.camera.has_value());
    Vec3 pos_mid = s.camera->position.value_at(30);
    CHECK(pos_mid.x == doctest::Approx(100.0f));
    CHECK(pos_mid.z == doctest::Approx(-1000.0f));
}
