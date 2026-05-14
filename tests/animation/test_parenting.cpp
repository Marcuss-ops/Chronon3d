#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/transform.hpp>

using namespace chronon3d;

TEST_CASE("Layer Parenting basic translation") {
    FrameContext ctx{.frame = 0};
    SceneBuilder s(ctx);

    s.null("parent", [](LayerBuilder& l) {
        l.position({100, 200, 0});
    });

    s.layer("child", [](LayerBuilder& l) {
        l.parent("parent")
         .position({50, 50, 0});
    });

    Scene scene = s.build();
    auto& layers = scene.layers();

    REQUIRE(layers.size() == 2);
    
    // Parent should be at its local position
    CHECK(layers[0].name == "parent");
    CHECK(layers[0].transform.position.x == doctest::Approx(100.0f));
    CHECK(layers[0].transform.position.y == doctest::Approx(200.0f));

    // Child should be at world position: 100+50, 200+50
    CHECK(layers[1].name == "child");
    CHECK(layers[1].transform.position.x == doctest::Approx(150.0f));
    CHECK(layers[1].transform.position.y == doctest::Approx(250.0f));
}

TEST_CASE("Layer Parenting nested") {
    FrameContext ctx{.frame = 0};
    SceneBuilder s(ctx);

    s.null("grandparent", [](LayerBuilder& l) {
        l.position({100, 0, 0});
    });

    s.null("parent", [](LayerBuilder& l) {
        l.parent("grandparent")
         .position({100, 0, 0});
    });

    s.layer("child", [](LayerBuilder& l) {
        l.parent("parent")
         .position({100, 0, 0});
    });

    Scene scene = s.build();
    auto& layers = scene.layers();

    REQUIRE(layers.size() == 3);
    CHECK(layers[2].transform.position.x == doctest::Approx(300.0f));
}

TEST_CASE("Layer Parenting with rotation") {
    FrameContext ctx{.frame = 0};
    SceneBuilder s(ctx);

    // Parent at origin, rotated 90 deg around Z
    s.null("parent", [](LayerBuilder& l) {
        l.position({0, 0, 0})
         .rotate({0, 0, 90});
    });

    // Child offset by 100 on X locally.
    // Since parent is rotated 90 deg (X becomes Y), 
    // child world position should be around {0, 100, 0}.
    s.layer("child", [](LayerBuilder& l) {
        l.parent("parent")
         .position({100, 0, 0});
    });

    Scene scene = s.build();
    auto& layers = scene.layers();

    CHECK(layers[1].transform.position.x == doctest::Approx(0.0f));
    CHECK(layers[1].transform.position.y == doctest::Approx(100.0f));
}
