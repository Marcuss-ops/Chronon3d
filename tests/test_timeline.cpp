#include <doctest/doctest.h>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/animated_transform.hpp>

using namespace chronon3d;

TEST_CASE("Quaternion interpolation") {
    Quat q1 = Quat::identity();
    Quat q2(1.0f, 0.0f, 0.0f, 0.0f); // 180 deg around X (simplified)

    SUBCASE("Lerp/Slerp placeholder") {
        Quat mid = lerp(q1, q2, 0.5f);
        CHECK(mid.w > 0.0f);
        CHECK(mid.x > 0.0f);
    }
}

TEST_CASE("AnimatedTransform evaluation") {
    AnimatedTransform at;
    at.position.add_keyframe(0, Vec3(0.0f));
    at.position.add_keyframe(100, Vec3(10.0f, 20.0f, 30.0f));

    SUBCASE("Evaluate middle frame") {
        Transform t = at.evaluate(50);
        CHECK(t.position.x == 5.0f);
        CHECK(t.position.y == 10.0f);
        CHECK(t.position.z == 15.0f);
    }
}

TEST_CASE("Composition and Layers") {
    Composition::Builder builder;
    builder.name("MainComp").size(1920, 1080).fps({30, 1});
    
    auto& layer = builder.add_layer("Background", LayerType::Shape);
    layer.range = {0, 300};
    layer.transform.position.add_keyframe(0, Vec3(0.0f));
    layer.transform.position.add_keyframe(300, Vec3(100.0f, 0.0f, 0.0f));

    auto comp = builder.build();

    SUBCASE("Layer management") {
        // We can't access layers directly if they are private, but evaluate() works
        Scene s = comp->evaluate(0);
        CHECK(s.nodes().size() == 1);
        CHECK(s.nodes()[0].name == "Background");
    }

    SUBCASE("Layer activity") {
        CHECK(layer.is_active(0));
        CHECK(layer.is_active(150));
        CHECK_FALSE(layer.is_active(300));
    }
}
