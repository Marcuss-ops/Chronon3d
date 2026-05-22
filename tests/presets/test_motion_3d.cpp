#include <doctest/doctest.h>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_resolver.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;
using namespace chronon3d::test;

TEST_CASE("ParallaxDrift moves object over time") {
    auto obj = MotionObject::image("img", "assets/images/person.png")
        .enable_3d(-200.0f)
        .preset(MotionPreset::ParallaxDrift)
        .time(0, 120)
        .parallax(2.0f);

    auto a = resolve_motion_state(make_ctx(0), obj);
    auto b = resolve_motion_state(make_ctx(60), obj);

    CHECK(a.position.x != doctest::Approx(b.position.x));
    CHECK(a.position.y != doctest::Approx(b.position.y));
}

TEST_CASE("Orbit2_5D changes rotation and depth") {
    auto obj = MotionObject::rect("card")
        .enable_3d(-120.0f)
        .preset(MotionPreset::Orbit2_5D)
        .time(0, 100);

    auto a = resolve_motion_state(make_ctx(0), obj);
    auto b = resolve_motion_state(make_ctx(50), obj);
    auto c = resolve_motion_state(make_ctx(100), obj);

    CHECK(a.rotation.y != doctest::Approx(c.rotation.y));
    CHECK(b.position.z != doctest::Approx(a.position.z));
}
