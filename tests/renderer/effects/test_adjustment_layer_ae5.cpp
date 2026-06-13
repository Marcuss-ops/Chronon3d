#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>

using namespace chronon3d;

static std::shared_ptr<Framebuffer> render_adj(
    std::function<Scene(const FrameContext&)> fn, int w = 80, int h = 80)
{
    SoftwareRenderer rend;
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=1}, fn);
    return rend.render_frame(comp, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-5: Adjustment Layer Wiring
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-5: adjustment_layer receives screen_dimensions") {
    // Verify that the adjustment layer builder has access to screen dimensions
    // by using an effect that depends on them (vignette uses frame diagonal).
    auto fb = render_adj([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}});
        s.adjustment_layer("vig", [](LayerBuilder& l) {
            l.vignette(0.3f, 0.3f, 1.0f);  // strong vignette
        });
        return s.build();
    });

    // Center should be close to white (vignette is weaker at center)
    Color center = fb->get_pixel(40, 40);
    CHECK(center.r > 0.5f);

    // Corner should be darker (vignette is stronger at edges)
    Color corner = fb->get_pixel(2, 2);
    CHECK(corner.r < center.r);
}

TEST_CASE("AE-5: saturation effect reduces color") {
    auto fb = render_adj([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color{1.0f, 0.0f, 0.0f, 1.0f}, .pos={40,40,0}});
        s.adjustment_layer("desat", [](LayerBuilder& l) {
            l.saturation(0.0f);  // full greyscale
        });
        return s.build();
    });

    Color c = fb->get_pixel(40, 40);
    // Pure red with saturation=0 should become grey (all channels equal)
    CHECK(std::abs(c.r - c.g) < 0.05f);
    CHECK(std::abs(c.g - c.b) < 0.05f);
}

TEST_CASE("AE-5: invert effect inverts colors") {
    auto fb = render_adj([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}});
        s.adjustment_layer("inv", [](LayerBuilder& l) {
            l.invert(1.0f);
        });
        return s.build();
    });

    Color c = fb->get_pixel(40, 40);
    // White inverted should be black
    CHECK(c.r < 0.1f);
    CHECK(c.g < 0.1f);
    CHECK(c.b < 0.1f);
}

TEST_CASE("AE-5: hue_rotate 180 degrees on red") {
    auto fb = render_adj([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color{1.0f, 0.0f, 0.0f, 1.0f}, .pos={40,40,0}});
        s.adjustment_layer("hue", [](LayerBuilder& l) {
            l.hue_rotate(180.0f);
        });
        return s.build();
    });

    Color c = fb->get_pixel(40, 40);
    // Red rotated 180 degrees → cyan-ish
    CHECK(c.r < 0.3f);
    CHECK(c.g > 0.3f);
}

TEST_CASE("AE-5: multiple adjustment effects in order") {
    auto fb = render_adj([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {.size={80,80}, .color=Color::white(), .pos={40,40,0}});
        s.adjustment_layer("grade", [](LayerBuilder& l) {
            l.brightness(-0.3f)
             .contrast(1.2f)
             .saturation(0.5f);
        });
        return s.build();
    });

    Color c = fb->get_pixel(40, 40);
    // White after brightness -0.3, contrast 1.2, saturation 0.5
    CHECK(c.r < 1.0f);
    CHECK(c.r > 0.3f);
}

// ═══════════════════════════════════════════════════════════════════════════
// AE-5: Declarative API (LayerDesc kind + TimelineEvaluator)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AE-5: LayerDesc kind field defaults to Normal") {
    LayerDesc ld;
    CHECK(ld.kind == LayerKind::Normal);
}

TEST_CASE("AE-5: LayerDesc kind=Adjustment propagates through TimelineEvaluator") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc bg;
    bg.name = "bg";
    bg.position = AnimatedValue<Vec3>{Vec3{40, 40, 0}};

    LayerDesc adj;
    adj.name = "grade";
    adj.kind = LayerKind::Adjustment;
    adj.effects.push_back(BrightnessContrastEffectDesc{.brightness = -0.5f});

    desc.layers.push_back(bg);
    desc.layers.push_back(adj);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 2);
    CHECK(scene.layers()[0].kind == LayerKind::Normal);
    CHECK(scene.layers()[1].kind == LayerKind::Adjustment);
}

TEST_CASE("AE-5: declarative saturation effect desc") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc bg;
    bg.name = "bg";

    LayerDesc adj;
    adj.name = "desat";
    adj.kind = LayerKind::Adjustment;
    adj.effects.push_back(SaturationEffectDesc{.value = 0.0f});

    desc.layers.push_back(bg);
    desc.layers.push_back(adj);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 2);
    CHECK(scene.layers()[1].kind == LayerKind::Adjustment);
    CHECK(!scene.layers()[1].effects.empty());
}

TEST_CASE("AE-5: declarative vignette effect desc") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc bg;
    bg.name = "bg";

    LayerDesc adj;
    adj.name = "vig";
    adj.kind = LayerKind::Adjustment;
    adj.effects.push_back(VignetteEffectDesc{.radius = 0.5f, .softness = 0.5f, .amount = 1.0f});

    desc.layers.push_back(bg);
    desc.layers.push_back(adj);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 2);
    CHECK(scene.layers()[1].kind == LayerKind::Adjustment);
    CHECK(!scene.layers()[1].effects.empty());
}

TEST_CASE("AE-5: declarative hue_rotate effect desc") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc adj;
    adj.name = "hue";
    adj.kind = LayerKind::Adjustment;
    adj.effects.push_back(HueRotateEffectDesc{.degrees = 90.0f});

    desc.layers.push_back(adj);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].kind == LayerKind::Adjustment);
    CHECK(!scene.layers()[0].effects.empty());
}

TEST_CASE("AE-5: declarative invert effect desc") {
    SceneDescription desc;
    desc.width = 80;
    desc.height = 80;
    desc.frame_rate = {30, 1};

    LayerDesc adj;
    adj.name = "inv";
    adj.kind = LayerKind::Adjustment;
    adj.effects.push_back(InvertEffectDesc{.amount = 1.0f});

    desc.layers.push_back(adj);

    TimelineEvaluator evaluator;
    Scene scene = evaluator.evaluate(desc, Frame{0});

    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].kind == LayerKind::Adjustment);
    CHECK(!scene.layers()[0].effects.empty());
}
