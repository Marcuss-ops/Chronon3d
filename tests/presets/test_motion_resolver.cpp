#include <doctest/doctest.h>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_resolver.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;

static FrameContext ctx_at(Frame frame) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = FrameRate{30, 1};
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

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

    auto st = resolve_motion_state(ctx_at(5), obj);

    CHECK_FALSE(st.visible);
    CHECK(st.opacity == doctest::Approx(0.0f));
}

TEST_CASE("resolve_motion_state keeps base transform for None preset") {
    auto obj = MotionObject::rect("box")
        .at({100.0f, 200.0f, 30.0f})
        .opacity(0.75f)
        .time(0, 60);

    auto st = resolve_motion_state(ctx_at(30), obj);

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

    auto start = resolve_motion_state(ctx_at(0), obj);
    auto end = resolve_motion_state(ctx_at(100), obj);

    CHECK(start.position.z != doctest::Approx(end.position.z));
    CHECK(start.rotation.y != doctest::Approx(end.rotation.y));
}
