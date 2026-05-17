#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

TEST_CASE("SceneBuilder minimal") {
    SceneBuilder builder;
    builder.rect("test-box", {10, 20, 30}, Color::red());
    
    Scene scene = builder.build();
    
    REQUIRE(scene.nodes().size() == 1);
    const auto& node = scene.nodes()[0];
    CHECK(node.name == "test-box");
    CHECK(node.world_transform.position.x == 10.0f);
    CHECK(node.color.r == Color::red().r);
    CHECK(node.color.g == Color::red().g);
    CHECK(node.color.b == Color::red().b);
    CHECK(node.color.a == Color::red().a);
}

TEST_CASE("SceneBuilder fluent API") {
    auto scene = SceneBuilder{}
        .rect("box1", {0, 0, 0}, Color::white())
        .rect("box2", {1, 1, 1}, Color::black())
        .build();
        
    CHECK(scene.nodes().size() == 2);
}

TEST_CASE("SceneBuilder lighting API stores scene lights") {
    auto scene = SceneBuilder{}
        .ambient_light(Color{0.8f, 0.7f, 0.6f, 1.0f}, 0.25f)
        .directional_light({0.0f, 0.0f, -1.0f}, Color{1.0f, 0.9f, 0.8f, 1.0f}, 0.75f)
        .build();

    CHECK(scene.light_context().enabled);
    CHECK(scene.light_context().ambient_enabled);
    CHECK(scene.light_context().directional_enabled);
    CHECK(scene.light_context().ambient == doctest::Approx(0.25f));
    CHECK(scene.light_context().diffuse == doctest::Approx(0.75f));
    CHECK(scene.light_context().ambient_color.r == doctest::Approx(0.8f));
    CHECK(scene.light_context().directional_color.g == doctest::Approx(0.9f));
}

TEST_CASE("LayerBuilder accepts_lights toggles material") {
    auto scene = SceneBuilder{}
        .layer("card", [](LayerBuilder& l) {
            Material2_5D mat;
            mat.accepts_lights = false;
            mat.ambient_multiplier = 0.5f;
            l.material(mat);
            l.rect("fill", {.size = {100, 100}, .color = Color::white(), .pos = {0, 0, 0}});
        })
        .build();

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].material.accepts_lights == false);
    CHECK(scene.layers()[0].material.ambient_multiplier == doctest::Approx(0.5f));
}
