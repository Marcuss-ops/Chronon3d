#include <doctest/doctest.h>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/spring.hpp>

using namespace chronon3d;

TEST_CASE("Composition Builder and Immutability") {
    Composition::Builder builder;
    builder.name("TestComp").size(1280, 720).fps({60, 1}).duration(600);
    
    auto comp = builder.build();

    CHECK(comp->name() == "TestComp");
    CHECK(comp->width() == 1280);
    CHECK(comp->height() == 720);
    CHECK(comp->frame_rate().fps() == 60.0);
    CHECK(comp->duration() == 600);
}

TEST_CASE("Deterministic Spring") {
    SpringValue<f32> spring(0.0f);
    spring.set_target(100.0f);

    // After 0 seconds, it should be at initial position
    CHECK(spring.evaluate(0.0f) == 0.0f);
    
    // After some time, it should move towards target
    f32 mid = spring.evaluate(0.5f);
    CHECK(mid > 0.0f);
    CHECK(mid < 200.0f); // might overshoot depending on damping

    // Multiple evaluations at SAME dt should give SAME result (pure function of dt)
    SpringValue<f32> spring2(0.0f);
    spring2.set_target(100.0f);
    CHECK(spring2.evaluate(0.5f) == mid);
}

TEST_CASE("Pure Frame Evaluation") {
    Composition::Builder builder;
    builder.size(100, 100);
    
    auto& layer = builder.add_layer("L1", LayerType::Null);
    layer.range = {0, 100};
    layer.transform.position.add_keyframe(0, Vec3(0.0f));
    layer.transform.position.add_keyframe(100, Vec3(100.0f));

    auto comp = builder.build();

    Scene s1 = comp->evaluate(50);
    Scene s2 = comp->evaluate(50);

    CHECK(s1.nodes().size() == 1);
    CHECK(s1.nodes()[0].world_transform.position.x == 50.0f);
    
    // Determinism check
    CHECK(s1.nodes()[0].world_transform.position.x == s2.nodes()[0].world_transform.position.x);
}
