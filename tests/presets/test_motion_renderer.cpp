#include <doctest/doctest.h>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <tests/helpers/test_utils.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;
using namespace chronon3d::test;

TEST_CASE("draw_motion_object skips object outside time") {
    auto ctx = make_ctx(0);
    SceneBuilder s(ctx);

    auto obj = MotionObject::rect("box").time(10, 20);
    draw_motion_object(s, ctx, obj);

    auto scene = s.build();
    CHECK(scene.layers().empty());
}

TEST_CASE("draw_motion_object creates a layer for a rect inside time") {
    auto ctx = make_ctx(15);
    SceneBuilder s(ctx);

    auto obj = MotionObject::rect("box")
        .size({200.0f, 100.0f})
        .color(Color{1.0f, 0.0f, 0.0f, 1.0f})
        .time(10, 20);

    draw_motion_object(s, ctx, obj);

    auto scene = s.build();
    CHECK_FALSE(scene.layers().empty());
}

TEST_CASE("draw_motion_object creates glow text layers") {
    auto ctx = make_ctx(15);
    SceneBuilder s(ctx);

    auto obj = MotionObject::text("title", "TEST TITLE")
        .font_size(96.0f)
        .glow(true)
        .time(0, 30);

    draw_motion_object(s, ctx, obj);

    auto scene = s.build();
    // Glow is applied as an inline effect on the text layer, so at least 1 layer is expected.
    CHECK(scene.layers().size() >= 1);
}

TEST_CASE("draw_motion_objects draws multiple objects") {
    auto ctx = make_ctx(20);
    SceneBuilder s(ctx);

    std::vector<MotionObject> objects = {
        MotionObject::rect("bg").size({400.0f, 200.0f}).time(0, 60),
        MotionObject::text("title", "HELLO").time(0, 60),
    };

    draw_motion_objects(s, ctx, objects);

    auto scene = s.build();
    CHECK(scene.layers().size() >= 2);
}
