#include <doctest/doctest.h>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// MotionBlurSettings struct
// ---------------------------------------------------------------------------
TEST_CASE("MotionBlur: MotionBlurSettings defaults") {
    MotionBlurSettings mb;
    CHECK_FALSE(mb.enabled);
    CHECK(mb.samples       == 8);
    CHECK(mb.shutter_angle == doctest::Approx(180.0f));
}

TEST_CASE("MotionBlur: SoftwareRenderer default disabled") {
    SoftwareRenderer rend;
    CHECK_FALSE(rend.motion_blur().enabled);
}

// ---------------------------------------------------------------------------
// FrameContext: frame_time field
// ---------------------------------------------------------------------------
TEST_CASE("MotionBlur: FrameContext has frame_time = 0 by default") {
    FrameContext ctx;
    CHECK(ctx.frame_time == doctest::Approx(0.0f));
}

TEST_CASE("MotionBlur: effective_frame = frame + frame_time") {
    FrameContext ctx;
    ctx.frame      = 10;
    ctx.frame_time = 0.25f;
    CHECK(ctx.effective_frame() == doctest::Approx(10.25f));
}

// ---------------------------------------------------------------------------
// Composition::evaluate with frame_time
// ---------------------------------------------------------------------------
TEST_CASE("MotionBlur: composition evaluate passes frame_time to context") {
    float captured_time = -1.0f;
    Composition comp(CompositionSpec{.width=10,.height=10,.duration=30},
        [&](const FrameContext& ctx) {
            captured_time = ctx.frame_time;
            SceneBuilder s(ctx);
            return s.build();
        });

    [[maybe_unused]] auto evaluated = comp.evaluate(5, 0.333f);
    CHECK(captured_time == doctest::Approx(0.333f));
}

// ---------------------------------------------------------------------------
// Motion blur accumulation: static scene should produce same result regardless
// ---------------------------------------------------------------------------
TEST_CASE("MotionBlur: static scene same with or without motion blur") {
    auto make_static_scene = [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={60,60}, .color=Color::white(), .pos={30,30,0}});
        return s.build();
    };
    Composition comp(CompositionSpec{.width=60,.height=60,.duration=1}, make_static_scene);

    SoftwareRenderer rend_normal;
    auto fb_normal = rend_normal.render_frame(comp, 0);

    SoftwareRenderer rend_mb;
    rend_mb.set_motion_blur({.enabled=true, .samples=4, .shutter_angle=180.0f});
    auto fb_mb = rend_mb.render_frame(comp, 0);

    // Static scene: motion blur average should equal a single frame
    Color c_normal = fb_normal->get_pixel(30, 30);
    Color c_mb     = fb_mb->get_pixel(30, 30);
    CHECK(c_mb.r == doctest::Approx(c_normal.r).epsilon(0.01f));
    CHECK(c_mb.g == doctest::Approx(c_normal.g).epsilon(0.01f));
    CHECK(c_mb.b == doctest::Approx(c_normal.b).epsilon(0.01f));
}

// ---------------------------------------------------------------------------
// Motion blur with moving object: result should be blurred/smeared
// ---------------------------------------------------------------------------
TEST_CASE("MotionBlur: moving object produces intermediate alpha") {
    // A rect that slides from left to right based on frame_time
    auto make_moving = [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        float x = 10.0f + ctx.frame_time * 40.0f;  // moves 40px over the shutter
        s.rect("r", {.size={10,10}, .color=Color::white(), .pos={x, 30, 0}});
        return s.build();
    };
    Composition comp(CompositionSpec{.width=60,.height=60,.duration=1}, make_moving);

    SoftwareRenderer rend;
    rend.set_motion_blur({.enabled=true, .samples=8, .shutter_angle=360.0f});
    auto fb = rend.render_frame(comp, 0);

    // No single pixel should be fully white since the rect moves across
    // multiple pixels -- the centre of travel should have partial coverage.
    Color mid = fb->get_pixel(30, 30);  // middle of travel
    // With 8 samples and 40px travel, mid pixel should see the rect ~2/8 of the time
    CHECK(mid.r > 0.0f);   // something there
    CHECK(mid.r < 1.0f);   // not fully covered
}
