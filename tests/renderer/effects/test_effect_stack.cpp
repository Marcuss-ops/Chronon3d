#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

static std::unique_ptr<Framebuffer> render_fn(
    std::function<Scene(const FrameContext&)> fn, int w = 80, int h = 80)
{
    SoftwareRenderer rend;
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=1}, fn);
    return rend.render_frame(comp, 0);
}

// ---------------------------------------------------------------------------
// EffectInstance construction
// ---------------------------------------------------------------------------
TEST_CASE("EffectStack: EffectInstance holds BlurParams") {
    EffectInstance inst{BlurParams{10.0f}};
    CHECK(inst.enabled);
    REQUIRE(std::holds_alternative<BlurParams>(inst.params));
    CHECK(std::get<BlurParams>(inst.params).radius == doctest::Approx(10.0f));
}

TEST_CASE("EffectStack: EffectInstance disabled flag") {
    EffectInstance inst{TintParams{Color::red(), 0.5f}, false};
    CHECK_FALSE(inst.enabled);
}

TEST_CASE("EffectStack: EffectStack is a vector of instances") {
    EffectStack stack;
    stack.push_back({BlurParams{5.0f}});
    stack.push_back({TintParams{Color::green(), 1.0f}});
    CHECK(stack.size() == 2);
}

// ---------------------------------------------------------------------------
// LayerBuilder API builds the stack
// ---------------------------------------------------------------------------
TEST_CASE("EffectStack: blur() adds BlurParams to stack") {
    SceneBuilder sb(nullptr);
    // Can't call s.layer() without a FrameContext -- test via manual LayerBuilder
    LayerBuilder lb("test");
    lb.blur(8.0f);
    auto layer = lb.build();
    REQUIRE(layer.effects.size() == 1);
    REQUIRE(std::holds_alternative<BlurParams>(layer.effects[0].params));
    CHECK(std::get<BlurParams>(layer.effects[0].params).radius == doctest::Approx(8.0f));
}

TEST_CASE("EffectStack: tint() adds TintParams to stack") {
    LayerBuilder lb("test");
    lb.tint(Color::red(), 0.5f);
    auto layer = lb.build();
    REQUIRE(layer.effects.size() == 1);
    REQUIRE(std::holds_alternative<TintParams>(layer.effects[0].params));
}

TEST_CASE("EffectStack: chained effects preserve order") {
    LayerBuilder lb("test");
    lb.blur(4.0f).tint(Color::blue(), 1.0f).brightness(0.1f);
    auto layer = lb.build();
    REQUIRE(layer.effects.size() == 3);
    CHECK(std::holds_alternative<BlurParams>(layer.effects[0].params));
    CHECK(std::holds_alternative<TintParams>(layer.effects[1].params));
    CHECK(std::holds_alternative<BrightnessParams>(layer.effects[2].params));
}

TEST_CASE("EffectStack: drop_shadow and glow added to stack") {
    LayerBuilder lb("test");
    lb.drop_shadow({4,4}, Color::black(), 8.0f)
      .glow(12.0f, 0.9f);
    auto layer = lb.build();
    CHECK(layer.effects.size() == 2);
    CHECK(std::holds_alternative<DropShadowParams>(layer.effects[0].params));
    CHECK(std::holds_alternative<GlowParams>(layer.effects[1].params));
}

// ---------------------------------------------------------------------------
// Rendered output: tint via effect stack
// ---------------------------------------------------------------------------
TEST_CASE("EffectStack: tint applied via stack produces coloured output") {
    auto fb = render_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({40,40,0});
            l.rect("r", {.size={60,60}, .color=Color::white()});
            l.tint(Color{1,0,0,1}, 1.0f);  // full red tint
        });
        return s.build();
    });

    Color c = fb->get_pixel(40, 40);
    CHECK(c.r > 0.9f);
    CHECK(c.g < 0.1f);
    CHECK(c.b < 0.1f);
}

