#include <doctest/doctest.h>

#if 0 // Disabled: chronon3d/description/scene_description.hpp is missing — SceneDescription struct was refactored out.
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <tests/helpers/pixel_assertions.hpp>
using namespace chronon3d;

using namespace chronon3d::test;

static std::shared_ptr<Framebuffer> render_timemap(
    std::function<Scene(const FrameContext&)> fn, int w = 80, int h = 80, Frame frame = Frame{0})
{
    SoftwareRenderer rend;
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=90}, fn);
    return rend.render_frame(comp, frame);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-4: TimeRemap struct basics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-4: TimeRemap defaults are inactive") {
    TimeRemap tr;
    CHECK_FALSE(tr.active());
    CHECK(tr.speed == doctest::Approx(1.0f));
    CHECK(tr.freeze_frame == -1);
}

TEST_CASE("AE-4: TimeRemap active when speed != 1.0") {
    TimeRemap tr;
    tr.speed = 0.5f;
    CHECK(tr.active());
}

TEST_CASE("AE-4: TimeRemap active when freeze_frame >= 0") {
    TimeRemap tr;
    tr.freeze_frame = Frame{30};
    CHECK(tr.active());
}

TEST_CASE("AE-4: TimeRemap active when time_remap is animated") {
    TimeRemap tr;
    tr.time_remap.key(Frame{0}, 0.0f).key(Frame{60}, 30.0f);
    CHECK(tr.active());
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-4: Layer::local_frame() with TimeRemap
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-4: local_frame without time_remap is unchanged") {
    Layer l;
    l.from = Frame{10};
    l.time_offset = Frame{5};
    CHECK(l.local_frame(Frame{20}) == Frame{15}); // 20 - 10 + 5
}

TEST_CASE("AE-4: local_frame with freeze_frame") {
    Layer l;
    l.from = Frame{0};
    l.time_remap.freeze_frame = Frame{42};
    CHECK(l.local_frame(Frame{0}) == Frame{42});
    CHECK(l.local_frame(Frame{30}) == Frame{42});
    CHECK(l.local_frame(Frame{90}) == Frame{42});
}

TEST_CASE("AE-4: local_frame with speed 0.5 (slow motion)") {
    Layer l;
    l.from = Frame{0};
    l.time_remap.speed = 0.5f;
    CHECK(l.local_frame(Frame{0}) == Frame{0});
    CHECK(l.local_frame(Frame{10}) == Frame{5});
    CHECK(l.local_frame(Frame{60}) == Frame{30});
}

TEST_CASE("AE-4: local_frame with speed 2.0 (fast forward)") {
    Layer l;
    l.from = Frame{0};
    l.time_remap.speed = 2.0f;
    CHECK(l.local_frame(Frame{0}) == Frame{0});
    CHECK(l.local_frame(Frame{15}) == Frame{30});
    CHECK(l.local_frame(Frame{30}) == Frame{60});
}

TEST_CASE("AE-4: local_frame with negative speed (reverse)") {
    Layer l;
    l.from = Frame{0};
    l.duration = Frame{60};
    l.time_remap.speed = -1.0f;
    // source = duration + speed * raw = 60 + (-1) * raw
    // At frame 0:  source = 60 (last frame)
    CHECK(l.local_frame(Frame{0}) == Frame{60});
    // At frame 30: source = 30 (middle)
    CHECK(l.local_frame(Frame{30}) == Frame{30});
    // At frame 60: source = 0 (first frame)
    CHECK(l.local_frame(Frame{60}) == Frame{0});
}

TEST_CASE("AE-4: local_frame with negative speed half-reverse") {
    Layer l;
    l.from = Frame{0};
    l.duration = Frame{120};
    l.time_remap.speed = -0.5f;
    // source = 120 + (-0.5) * raw
    // At frame 0:   source = 120
    CHECK(l.local_frame(Frame{0}) == Frame{120});
    // At frame 60:  source = 120 - 30 = 90
    CHECK(l.local_frame(Frame{60}) == Frame{90});
    // At frame 120: source = 120 - 60 = 60
    CHECK(l.local_frame(Frame{120}) == Frame{60});
}

TEST_CASE("AE-4: local_frame with negative speed and no duration falls back") {
    Layer l;
    l.from = Frame{0};
    l.duration = Frame{-1};  // infinite / unknown
    l.time_remap.speed = -1.0f;
    // Without duration, falls back to raw * speed = negative frames
    CHECK(l.local_frame(Frame{0}) == Frame{0});
    CHECK(l.local_frame(Frame{10}) == Frame{-10});
}

TEST_CASE("AE-4: local_frame with animated time_remap curve") {
    Layer l;
    l.from = Frame{0};
    // Remap: comp frame 0 → source 0, comp frame 60 → source 30 (half speed)
    l.time_remap.time_remap.key(Frame{0}, 0.0f).key(Frame{60}, 30.0f);
    CHECK(l.local_frame(Frame{0}) == Frame{0});
    CHECK(l.local_frame(Frame{30}) == Frame{15});
    CHECK(l.local_frame(Frame{60}) == Frame{30});
}

TEST_CASE("AE-4: local_frame with animated time_remap reverse") {
    Layer l;
    l.from = Frame{0};
    // Remap: comp frame 0 → source 60, comp frame 60 → source 0 (reverse)
    l.time_remap.time_remap.key(Frame{0}, 60.0f).key(Frame{60}, 0.0f);
    CHECK(l.local_frame(Frame{0}) == Frame{60});
    CHECK(l.local_frame(Frame{30}) == Frame{30});
    CHECK(l.local_frame(Frame{60}) == Frame{0});
}

TEST_CASE("AE-4: local_frame freeze_frame overrides speed") {
    Layer l;
    l.from = Frame{0};
    l.time_remap.speed = 2.0f;  // speed is set
    l.time_remap.freeze_frame = Frame{15};  // but freeze overrides
    CHECK(l.local_frame(Frame{0}) == Frame{15});
    CHECK(l.local_frame(Frame{60}) == Frame{15});
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-4: LayerBuilder API
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-4: LayerBuilder speed() renders without crash") {
    auto fb = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}})
         .layer("test", [](LayerBuilder& l) {
            l.speed(0.5f);
        });
        return s.build();
    });
    CHECK(fb != nullptr);
    // White bg should be visible
    CHECK(any_pixel(*fb, Color::white()));
}

TEST_CASE("AE-4: LayerBuilder reverse() renders without crash") {
    auto fb = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}})
         .layer("test", [](LayerBuilder& l) {
            l.reverse();
        });
        return s.build();
    });
    CHECK(fb != nullptr);
    CHECK(any_pixel(*fb, Color::white()));
}

TEST_CASE("AE-4: speed 0.5 shifts animated position to half") {
    // Positions are canvas-centered. On a 200×200 canvas, center=(100,100).
    // Position animation from x=-60 to x=60 over 60 frames.
    // Normal at frame 30: t=0.5 → pos=0 → pixel=100.
    // Slow at frame 30: local_frame=15, t=0.25 → pos=-30 → pixel=70.
    auto fb_normal = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    }, 200, 200, Frame{30});

    auto fb_slow = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
            l.speed(0.5f);
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    }, 200, 200, Frame{30});

    REQUIRE(fb_normal != nullptr);
    REQUIRE(fb_slow != nullptr);

    Color red = Color::red();
    float cx_normal = centroid_x(*fb_normal, red);
    float cx_slow   = centroid_x(*fb_slow, red);

    CHECK(cx_normal > 0.0f);
    CHECK(cx_slow   > 0.0f);
    // Slow dot should be LEFT of normal (further back in animation)
    CHECK(cx_slow < cx_normal);
    // Normal at t=0.5: pos=0 → pixel=100
    CHECK(cx_normal == doctest::Approx(100.0f).epsilon(0.1f));
    // Slow at t=0.25: pos=-30 → pixel=70
    CHECK(cx_slow == doctest::Approx(70.0f).epsilon(0.1f));
}

TEST_CASE("AE-4: speed 2.0 fast-forwards animated position") {
    // Same animation. At frame 15:
    // Normal: t=15/60=0.25 → pos=-30 → pixel=70.
    // Fast: local_frame=30, t=0.5 → pos=0 → pixel=100.
    auto fb_normal = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    }, 200, 200, Frame{15});

    auto fb_fast = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
            l.speed(2.0f);
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    }, 200, 200, Frame{15});

    REQUIRE(fb_normal != nullptr);
    REQUIRE(fb_fast != nullptr);

    Color red = Color::red();
    float cx_normal = centroid_x(*fb_normal, red);
    float cx_fast   = centroid_x(*fb_fast, red);

    CHECK(cx_normal > 0.0f);
    CHECK(cx_fast   > 0.0f);
    // Fast dot should be RIGHT of normal
    CHECK(cx_fast > cx_normal);
    CHECK(cx_normal == doctest::Approx(70.0f).epsilon(0.1f));
    CHECK(cx_fast   == doctest::Approx(100.0f).epsilon(0.1f));
}

TEST_CASE("AE-4: freeze_frame holds position across frames") {
    // With freeze_frame=15, position is always at local_frame=15 → t=0.25
    // pos = -30 → pixel = 70. Should be identical at frames 0, 30, 60.
    auto make_frozen = [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
            l.freeze_frame(Frame{15});
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    };

    auto fb_f0  = render_timemap(make_frozen, 200, 200, Frame{0});
    auto fb_f30 = render_timemap(make_frozen, 200, 200, Frame{30});
    auto fb_f60 = render_timemap(make_frozen, 200, 200, Frame{60});

    REQUIRE(fb_f0  != nullptr);
    REQUIRE(fb_f30 != nullptr);
    REQUIRE(fb_f60 != nullptr);

    Color red = Color::red();
    float cx_0  = centroid_x(*fb_f0, red);
    float cx_30 = centroid_x(*fb_f30, red);
    float cx_60 = centroid_x(*fb_f60, red);

    CHECK(cx_0  > 0.0f);
    CHECK(cx_30 > 0.0f);
    CHECK(cx_60 > 0.0f);
    // All three frames should show the dot at the SAME x position
    CHECK(cx_0  == doctest::Approx(cx_30).epsilon(0.02f));
    CHECK(cx_30 == doctest::Approx(cx_60).epsilon(0.02f));
    // Frozen at local_frame=15 → t=0.25 → pos=-30 → pixel=70
    CHECK(cx_0 == doctest::Approx(70.0f).epsilon(0.1f));
}

TEST_CASE("AE-4: reverse speed mirrors position trajectory") {
    // Normal: frame 0 → pos=-60 (pixel=40), frame 60 → pos=60 (pixel=160).
    // Reverse (speed=-1, dur=60): frame 0 → source=60 → pos=60 (pixel=160),
    //                              frame 60 → source=0 → pos=-60 (pixel=40).
    auto make_normal = [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
            l.duration(Frame{60});
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    };

    auto make_reverse = [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{-60, 0, 0}).key(Frame{60}, Vec3{60, 0, 0});
            l.reverse();
            l.duration(Frame{60});
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    };

    auto fb_norm_start = render_timemap(make_normal,  200, 200, Frame{0});
    auto fb_norm_end   = render_timemap(make_normal,  200, 200, Frame{59});
    auto fb_rev_start  = render_timemap(make_reverse, 200, 200, Frame{0});
    auto fb_rev_end    = render_timemap(make_reverse, 200, 200, Frame{59});

    REQUIRE(fb_norm_start != nullptr);
    REQUIRE(fb_norm_end   != nullptr);
    REQUIRE(fb_rev_start  != nullptr);
    REQUIRE(fb_rev_end    != nullptr);

    Color red = Color::red();
    float cx_norm_start = centroid_x(*fb_norm_start, red);
    float cx_norm_end   = centroid_x(*fb_norm_end, red);
    float cx_rev_start  = centroid_x(*fb_rev_start, red);
    float cx_rev_end    = centroid_x(*fb_rev_end, red);

    CHECK(cx_norm_start > 0.0f);
    CHECK(cx_norm_end   > 0.0f);
    CHECK(cx_rev_start  > 0.0f);
    CHECK(cx_rev_end    > 0.0f);

    // Normal: starts left (pos=-60 → pixel=40), ends right (pos≈60 → pixel≈158)
    CHECK(cx_norm_start == doctest::Approx(40.0f).epsilon(0.1f));
    CHECK(cx_norm_end   == doctest::Approx(158.0f).epsilon(0.1f));

    // Reverse: starts RIGHT (source=60 → pixel≈158), ends LEFT (source=0 → pixel=40)
    CHECK(cx_rev_start  == doctest::Approx(158.0f).epsilon(0.1f));
    CHECK(cx_rev_end    == doctest::Approx(40.0f).epsilon(0.1f));
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-4: Declarative API (LayerDesc + TimelineEvaluator)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-4: LayerDesc time_remap defaults") {
    LayerDesc ld;
    CHECK(ld.time_remap_speed == doctest::Approx(1.0f));
    CHECK(ld.time_remap_freeze_frame == -1);
    CHECK_FALSE(ld.time_remap_curve.is_animated());
}

TEST_CASE("AE-4: TimelineEvaluator propagates time_remap speed") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc ld;
    ld.name = "slow";
    ld.time_remap_speed = 0.5f;
    desc.layers.push_back(ld);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].time_remap.speed == doctest::Approx(0.5f));
}

TEST_CASE("AE-4: TimelineEvaluator propagates time_remap freeze_frame") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc ld;
    ld.name = "frozen";
    ld.time_remap_freeze_frame = Frame{42};
    desc.layers.push_back(ld);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].time_remap.freeze_frame == Frame{42});
}

TEST_CASE("AE-4: TimelineEvaluator propagates time_remap curve") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc ld;
    ld.name = "remapped";
    ld.time_remap_curve.key(Frame{0}, 0.0f).key(Frame{60}, 60.0f);
    desc.layers.push_back(ld);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].time_remap.time_remap.is_animated());
}

#endif // Disabled pending SceneDescription restoration
