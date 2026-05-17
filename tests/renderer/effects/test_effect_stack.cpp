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

template <typename T>
static bool holds_effect(const EffectInstance& inst) {
    if (std::any_cast<T>(&inst.params)) {
        return true;
    }
    if (auto* params = std::any_cast<EffectParams>(&inst.params)) {
        return std::holds_alternative<T>(*params);
    }
    return false;
}

template <typename T>
static const T& get_effect(const EffectInstance& inst) {
    if (auto* val = std::any_cast<T>(&inst.params)) {
        return *val;
    }
    return std::get<T>(*std::any_cast<EffectParams>(&inst.params));
}

// ---------------------------------------------------------------------------
// EffectInstance construction
// ---------------------------------------------------------------------------
TEST_CASE("EffectStack: EffectInstance holds BlurParams") {
    EffectInstance inst{BlurParams{10.0f}};
    CHECK(inst.enabled);
    REQUIRE(holds_effect<BlurParams>(inst));
    CHECK(get_effect<BlurParams>(inst).radius == doctest::Approx(10.0f));
}

TEST_CASE("EffectStack: EffectInstance disabled flag") {
    EffectInstance inst{TintParams{Color::red(), 0.5f}};
    inst.enabled = false;
    CHECK_FALSE(inst.enabled);
}

TEST_CASE("EffectStack: EffectStack is a vector of instances") {
    EffectStack stack;
    stack.push_back(EffectInstance{BlurParams{5.0f}});
    stack.push_back(EffectInstance{TintParams{Color::green(), 1.0f}});
    CHECK(stack.size() == 2);
}

// ---------------------------------------------------------------------------
// LayerBuilder API builds the stack
// ---------------------------------------------------------------------------
TEST_CASE("EffectStack: blur() adds BlurParams to stack") {
    LayerBuilder lb("test");
    lb.blur(8.0f);
    auto layer = lb.build();
    REQUIRE(layer.effects.size() == 1);
    REQUIRE(holds_effect<BlurParams>(layer.effects[0]));
    CHECK(get_effect<BlurParams>(layer.effects[0]).radius == doctest::Approx(8.0f));
}

TEST_CASE("EffectStack: tint() adds TintParams to stack") {
    LayerBuilder lb("test");
    lb.tint(Color::red(), 0.5f);
    auto layer = lb.build();
    REQUIRE(layer.effects.size() == 1);
    REQUIRE(holds_effect<TintParams>(layer.effects[0]));
}

TEST_CASE("EffectStack: chained effects preserve order") {
    LayerBuilder lb("test");
    lb.blur(4.0f).tint(Color::blue(), 1.0f).brightness(0.1f);
    auto layer = lb.build();
    REQUIRE(layer.effects.size() == 3);
    CHECK(holds_effect<BlurParams>(layer.effects[0]));
    CHECK(holds_effect<TintParams>(layer.effects[1]));
    CHECK(holds_effect<BrightnessParams>(layer.effects[2]));
}

TEST_CASE("EffectStack: drop_shadow and glow added to stack") {
    LayerBuilder lb("test");
    lb.drop_shadow({4,4}, Color::black(), 8.0f)
      .glow(12.0f, 0.9f);
    auto layer = lb.build();
    CHECK(layer.effects.size() == 2);
    CHECK(holds_effect<DropShadowParams>(layer.effects[0]));
    CHECK(holds_effect<GlowParams>(layer.effects[1]));
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
