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
