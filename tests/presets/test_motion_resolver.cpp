#include <doctest/doctest.h>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_resolver.hpp>
#include <chronon3d/presets/anim_factory.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;
using namespace chronon3d::test;

TEST_CASE("MotionTime normalizes frame range") {
    MotionTime t{10, 20};

    CHECK(t.normalized(5) == doctest::Approx(0.0f));
    CHECK(t.normalized(10) == doctest::Approx(0.0f));
    CHECK(t.normalized(15) == doctest::Approx(0.5f));
    CHECK(t.normalized(20) == doctest::Approx(1.0f));
    CHECK(t.normalized(25) == doctest::Approx(1.0f));
}

TEST_CASE("resolve_motion_state hides object outside time") {
    auto obj = MotionObject::rect("box").time(10, 20);

    auto st = resolve_motion_state(make_ctx(5), obj);

    CHECK_FALSE(st.visible);
    CHECK(st.opacity == doctest::Approx(0.0f));
}

TEST_CASE("resolve_motion_state keeps base transform for None preset") {
    auto obj = MotionObject::rect("box")
        .at({100.0f, 200.0f, 30.0f})
        .opacity(0.75f)
        .time(0, 60);

    auto st = resolve_motion_state(make_ctx(30), obj);

    CHECK(st.visible);
    CHECK(st.position.x == doctest::Approx(100.0f));
    CHECK(st.position.y == doctest::Approx(200.0f));
    CHECK(st.position.z == doctest::Approx(30.0f));
    CHECK(st.opacity == doctest::Approx(0.75f));
}

TEST_CASE("PushIn3D changes z and rotation") {
    auto obj = MotionObject::text("title", "LIL DIRK")
        .enable_3d(-100.0f)
        .preset(MotionPreset::PushIn3D)
        .time(0, 100);

    auto start = resolve_motion_state(make_ctx(0), obj);
    auto end = resolve_motion_state(make_ctx(100), obj);

    CHECK(start.position.z != doctest::Approx(end.position.z));
    CHECK(start.rotation.y != doctest::Approx(end.rotation.y));
}

TEST_CASE("MotionObject fluent API with custom animations") {
    auto obj = MotionObject::rect("box")
        .time(0, 60)
        .animate(anim::fade_in(0, 30))
        .animate(anim::up(100.0f, 0, 30, Easing::Linear));

    auto start = resolve_motion_state(make_ctx(0), obj);
    auto mid = resolve_motion_state(make_ctx(15), obj);
    auto end = resolve_motion_state(make_ctx(30), obj);

    // fade_in maps opacity: 0.0 at frame 0, 0.5 at frame 15, 1.0 at frame 30
    CHECK(start.opacity == doctest::Approx(0.0f));
    CHECK(mid.opacity == doctest::Approx(0.5f));
    CHECK(end.opacity == doctest::Approx(1.0f));

    // up adds (1.0f - progress) * distance to position.y
    // base position is 0.0f.
    // at frame 0, progress = 0.0 -> offset = 100.0 -> y = 100.0
    // at frame 15, progress = 0.5 -> offset = 50.0 -> y = 50.0
    // at frame 30, progress = 1.0 -> offset = 0.0 -> y = 0.0
    CHECK(start.position.y == doctest::Approx(100.0f));
    CHECK(mid.position.y == doctest::Approx(50.0f));
    CHECK(end.position.y == doctest::Approx(0.0f));
}

TEST_CASE("Spring physics computes correct physical response") {
    auto obj = MotionObject::rect("spring_box")
        .time(0, 100)
        .animate(anim::spring_up(100.0f, 0, 1.0f, 100.0f, 10.0f));

    auto start = resolve_motion_state(make_ctx(0), obj);
    auto mid = resolve_motion_state(make_ctx(30), obj);
    auto end = resolve_motion_state(make_ctx(90), obj);

    // Initial position.y should be 100.0 (progress = 0, spring_value = 0, offset = 100)
    CHECK(start.position.y == doctest::Approx(100.0f));

    // Over time it oscillating/moving towards 0.0. After 90 frames (3 seconds), it should be settled close to 0.0.
    CHECK(std::abs(end.position.y) < 1.0f);
    CHECK(start.position.y != mid.position.y);
}

TEST_CASE("3D space animations push and flip") {
    auto obj = MotionObject::rect("card")
        .time(0, 60)
        .animate(anim::push_in_3d(-500.0f, 0, 30))
        .animate(anim::flip_y(-180.0f, 0, 30));

    auto start = resolve_motion_state(make_ctx(0), obj);
    auto end = resolve_motion_state(make_ctx(30), obj);

    // push_in_3d adds (1.0 - progress) * start_z to position.z
    // at frame 0, offset = -500.0 -> z = -500.0
    CHECK(start.position.z == doctest::Approx(-500.0f));
    CHECK(end.position.z == doctest::Approx(0.0f));

    // flip_y adds (1.0 - progress) * start_degrees to rotation.y
    // at frame 0, offset = -180.0 -> y = -180.0
    // at frame 30, offset = 0.0 -> y = 0.0
    CHECK(start.rotation.y == doctest::Approx(-180.0f));
    CHECK(end.rotation.y == doctest::Approx(0.0f));
}
