#include <doctest/doctest.h>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_renderer.hpp>

using namespace chronon3d;
using namespace chronon3d::presets::motion;

static FrameContext make_ctx(Frame frame) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = FrameRate{30, 1};
    ctx.width = 1920;
    ctx.height = 1080;
    return ctx;
}

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

    auto obj = MotionObject::text("title", "LIL DIRK")
        .font_size(96.0f)
        .glow(true)
        .time(0, 30);

    draw_motion_object(s, ctx, obj);

    auto scene = s.build();
    CHECK(scene.layers().size() >= 2);
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
