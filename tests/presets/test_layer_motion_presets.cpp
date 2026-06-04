#include <doctest/doctest.h>
#include <cmath>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

TEST_CASE("LayerBuilder::slide_in creates animated keyframes") {
    LayerBuilder builder("test", Frame{0});
    builder.position({100.0f, 50.0f, 0.0f})
           .opacity(1.0f)
           .slide_in(Vec3{-200.0f, 0.0f, 0.0f}, Frame{45});

    Layer l = builder.build();
    CHECK(l.anim_transform.is_animated());
    CHECK(l.anim_transform.position.is_animated());
    CHECK(l.anim_transform.opacity.is_animated());

    // At frame 0: should be at from position with 0 opacity
    Transform t0 = l.anim_transform.evaluate(Frame{0});
    CHECK(t0.position.x == doctest::Approx(-200.0f));
    CHECK(t0.opacity == doctest::Approx(0.0f));

    // At frame 45: should be at target position with full opacity
    Transform t45 = l.anim_transform.evaluate(Frame{45});
    CHECK(t45.position.x == doctest::Approx(100.0f));
    CHECK(t45.opacity == doctest::Approx(1.0f));
}

TEST_CASE("LayerBuilder::soft_pop creates scale + opacity keyframes") {
    LayerBuilder builder("test", Frame{0});
    builder.scale({1.0f, 1.0f, 1.0f})
           .opacity(1.0f)
           .soft_pop(Frame{30});

    Layer l = builder.build();
    CHECK(l.anim_transform.scale.is_animated());
    CHECK(l.anim_transform.opacity.is_animated());

    Transform t0 = l.anim_transform.evaluate(Frame{0});
    CHECK(t0.scale.x == doctest::Approx(0.90f));
    CHECK(t0.opacity == doctest::Approx(0.0f));

    Transform t30 = l.anim_transform.evaluate(Frame{30});
    CHECK(t30.scale.x == doctest::Approx(1.0f));
    CHECK(t30.opacity == doctest::Approx(1.0f));
}

TEST_CASE("LayerBuilder::float_idle creates looping position keyframes") {
    LayerBuilder builder("test", Frame{0});
    builder.position({0.0f, 0.0f, 0.0f})
           .float_idle(12.0f, Frame{120});

    Layer l = builder.build();
    CHECK(l.anim_transform.position.is_animated());

    // Quarter cycle should be at peak amplitude and snapped to an integer pixel.
    Transform t30 = l.anim_transform.evaluate(Frame{30});
    CHECK(t30.position.y == doctest::Approx(12.0f).epsilon(0.1f));
    CHECK(t30.position.y == doctest::Approx(std::round(t30.position.y)));

    // Half cycle should be back to origin.
    Transform t60 = l.anim_transform.evaluate(Frame{60});
    CHECK(t60.position.y == doctest::Approx(0.0f).epsilon(0.1f));

    // End of cycle should also be back to origin.
    Transform t120 = l.anim_transform.evaluate(Frame{120});
    CHECK(t120.position.y == doctest::Approx(0.0f).epsilon(0.1f));
}

TEST_CASE("LayerBuilder::depth_reveal enables 3D and animates Z") {
    LayerBuilder builder("test", Frame{0});
    builder.position({0.0f, 0.0f, 0.0f})
           .opacity(1.0f)
           .depth_reveal(260.0f, Frame{45});

    Layer l = builder.build();
    CHECK(l.uses_2_5d_projection);
    CHECK(l.anim_transform.position.is_animated());

    Transform t0 = l.anim_transform.evaluate(Frame{0});
    CHECK(t0.position.z == doctest::Approx(260.0f));

    Transform t45 = l.anim_transform.evaluate(Frame{45});
    CHECK(t45.position.z == doctest::Approx(0.0f));
}

TEST_CASE("LayerBuilder::card_flip_2_5d enables 3D and rotates Y") {
    LayerBuilder builder("test", Frame{0});
    builder.rotate({0.0f, 0.0f, 0.0f})
           .opacity(1.0f)
           .card_flip_2_5d(Frame{60});

    Layer l = builder.build();
    CHECK(l.uses_2_5d_projection);
    CHECK(l.anim_transform.rotation_euler.is_animated());

    // rotation is sampled as euler then converted to quat in evaluate()
    Transform t0 = l.anim_transform.evaluate(Frame{0});
    Vec3 euler0 = glm::degrees(glm::eulerAngles(t0.rotation));
    CHECK(euler0.y == doctest::Approx(-90.0f).epsilon(1.0f));

    Transform t60 = l.anim_transform.evaluate(Frame{60});
    Vec3 euler60 = glm::degrees(glm::eulerAngles(t60.rotation));
    CHECK(euler60.y == doctest::Approx(0.0f).epsilon(1.0f));
}

TEST_CASE("LayerBuilder::settle creates overshoot keyframes") {
    LayerBuilder builder("test", Frame{0});
    builder.scale({1.0f, 1.0f, 1.0f})
           .position({0.0f, 0.0f, 0.0f})
           .settle(0.08f, Frame{20});

    Layer l = builder.build();
    CHECK(l.anim_transform.scale.is_animated());

    Transform t0 = l.anim_transform.evaluate(Frame{0});
    CHECK(t0.scale.x == doctest::Approx(1.08f).epsilon(0.01f));

    Transform t20 = l.anim_transform.evaluate(Frame{20});
    CHECK(t20.scale.x == doctest::Approx(1.0f).epsilon(0.01f));
}
