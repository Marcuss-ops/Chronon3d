#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;


static std::shared_ptr<Framebuffer> render_stack_fn(
    std::function<Scene(const FrameContext&)> fn, int w = 80, int h = 80)
{
    auto rend = test::make_renderer();
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=1}, fn);
    return rend.render(comp, 0);
}

template <typename T>
static bool holds_effect(const EffectInstance& inst) {
    return std::holds_alternative<T>(inst.params);
}

template <typename T>
static const T& get_effect(const EffectInstance& inst) {
    return std::get<T>(inst.params);
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
    LayerBuilder lb("test", SampleTime{}, std::pmr::get_default_resource());
    lb.blur(8.0f);
    auto layer = lb.build();
    REQUIRE(layer.effects().size() == 1);
    REQUIRE(holds_effect<BlurParams>(layer.effects()[0]));
    CHECK(get_effect<BlurParams>(layer.effects()[0]).radius == doctest::Approx(8.0f));
}

TEST_CASE("EffectStack: tint() adds TintParams to stack") {
    LayerBuilder lb("test", SampleTime{}, std::pmr::get_default_resource());
    lb.tint(Color::red(), 0.5f);
    auto layer = lb.build();
    REQUIRE(layer.effects().size() == 1);
    REQUIRE(holds_effect<TintParams>(layer.effects()[0]));
}

TEST_CASE("EffectStack: chained effects preserve order") {
    LayerBuilder lb("test", SampleTime{}, std::pmr::get_default_resource());
    lb.blur(4.0f).tint(Color::blue(), 1.0f).brightness(0.1f);
    auto layer = lb.build();
    REQUIRE(layer.effects().size() == 3);
    CHECK(holds_effect<BlurParams>(layer.effects()[0]));
    CHECK(holds_effect<TintParams>(layer.effects()[1]));
    CHECK(holds_effect<BrightnessParams>(layer.effects()[2]));
}

TEST_CASE("EffectStack: drop_shadow and glow added to stack") {
    LayerBuilder lb("test", SampleTime{}, std::pmr::get_default_resource());
    lb.drop_shadow({4,4}, Color::black(), 8.0f)
      .glow(GlowParams{.radius = 12.0f, .intensity = 0.9f, .color = Color::white()});
    auto layer = lb.build();
    CHECK(layer.effects().size() == 2);
    CHECK(holds_effect<DropShadowParams>(layer.effects()[0]));
    CHECK(holds_effect<GlowParams>(layer.effects()[1]));
}

TEST_CASE("EffectStack: glow preset overload stores full GlowParams") {
    LayerBuilder lb("test", SampleTime{}, std::pmr::get_default_resource());
    lb.glow(GlowPresets::neon_blue(55.0f));

    auto layer = lb.build();
    REQUIRE(layer.effects().size() == 1);
    REQUIRE(holds_effect<GlowParams>(layer.effects()[0]));

    const auto& glow = get_effect<GlowParams>(layer.effects()[0]);
    CHECK(glow.radius == doctest::Approx(55.0f));
    CHECK(glow.intensity == doctest::Approx(1.25f));
    CHECK(glow.additive);
    CHECK(glow.core_strength > glow.bloom_strength);
}

TEST_CASE("EffectStack: fake_3d_wave adds Fake3DWaveParams to stack") {
    LayerBuilder lb("test", SampleTime{}, std::pmr::get_default_resource());
    lb.fake_3d_wave(Fake3DWaveParams{
        .amplitude_px = 12.0f,
        .frequency = 1.5f,
        .speed = 2.0f,
        .depth_px = 35.0f,
        .phase = 0.0f,
        .slices = 16,
        .axis = WaveAxis::Horizontal,
        .perspective = 0.05f,
        .highlight = 0.2f,
        .side_darkening = 0.15f,
        .shadow_enabled = true,
        .shadow_color = {1.0f, 0.05f, 0.05f, 0.75f},
        .shadow_offset = {10.0f, 8.0f},
        .shadow_blur = 0.0f,
        .expand_bounds = true,
    });
    auto layer = lb.build();
    CHECK(layer.effects().size() == 1);
    REQUIRE(holds_effect<Fake3DWaveParams>(layer.effects()[0]));
    CHECK(get_effect<Fake3DWaveParams>(layer.effects()[0]).slices == 16);
}

// ---------------------------------------------------------------------------
// Rendered output: tint via effect stack
// ---------------------------------------------------------------------------
TEST_CASE("EffectStack: tint applied via stack produces coloured output") {
    auto fb = render_stack_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({0,0,0});
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
