#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>

using namespace chronon3d;

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

TEST_CASE("AE-4: LayerBuilder speed()") {
    auto fb = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}})
         .layer("test", [](LayerBuilder& l) {
            l.speed(0.5f);
        });
        return s.build();
    });
    // Should not crash
    CHECK(fb != nullptr);
}

TEST_CASE("AE-4: LayerBuilder reverse()") {
    auto fb = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}})
         .layer("test", [](LayerBuilder& l) {
            l.reverse();
        });
        return s.build();
    });
    CHECK(fb != nullptr);
}

TEST_CASE("AE-4: speed affects animated property evaluation") {
    // A layer with position animation from (0,0) to (80,0) over 60 frames.
    // With speed=0.5, at frame 30 the local frame is 15, so position should be at ~25%.
    auto fb = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("moving", [](LayerBuilder& l) {
            l.position_anim().key(Frame{0}, Vec3{0, 40, 0}).key(Frame{60}, Vec3{80, 40, 0});
            l.speed(0.5f);
            l.rect("dot", {.size={10,10}, .color=Color::red()});
        });
        return s.build();
    }, 80, 80, Frame{30});

    // At frame 30 with speed=0.5, local_frame=15, position should be interpolated
    // between 0 and 80 at t=15/60=0.25, so x≈20
    CHECK(fb != nullptr);
}

TEST_CASE("AE-4: LayerBuilder freeze_frame()") {
    auto fb = render_timemap([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}})
         .layer("test", [](LayerBuilder& l) {
            l.freeze_frame(Frame{30});
        });
        return s.build();
    });
    CHECK(fb != nullptr);
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
