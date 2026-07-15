#include "authoring_dsl_test_support.hpp"
#include "../support/text_run_builder_inspection.hpp"

using chronon3d::authoring::testing::TextRunBuilderInspector;

namespace {
chronon3d::CanvasInfo explicit_canvas(float width, float height) {
    return chronon3d::CanvasInfo::with_safe_area(
        width, height, chronon3d::SafeAreaPreset{});
}
} // namespace

// ============================================================================
// Scene + Composition wrapper tests
// ============================================================================

TEST_CASE("Authoring/CompositionBuilder: fields accumulate via fluent setters") {
    using chronon3d::authoring::CompositionBuilder;
    CompositionBuilder cb;
    cb.name("hero-showcase")
      .width(1920)
      .height(1080)
      .duration(Frame{60})
      .frame_rate(FrameRate{30, 1})
      .assets_root(std::filesystem::path{"assets"});

    chronon3d::Composition comp = std::move(cb).build();
    CHECK(comp.name() == "hero-showcase");
    CHECK(comp.width() == 1920);
    CHECK(comp.height() == 1080);
    CHECK(comp.duration().integral() == 60);
    CHECK(comp.frame_rate().numerator == 30);
    CHECK(comp.assets_root() == "assets");
}

TEST_CASE("Authoring/CompositionBuilder: empty composition (no .scene()) renders zero layers") {
    using chronon3d::authoring::CompositionBuilder;
    CompositionBuilder cb;
    cb.name("empty").width(640).height(480);
    chronon3d::Composition comp = std::move(cb).build();
    chronon3d::Scene scene = comp.evaluate(Frame{0});
    CHECK(scene.layers().empty());
    CHECK(scene.nodes().empty());
}

TEST_CASE("Authoring/Scene + Layer: SFINAE wrap branch populates authored text in evaluated Scene") {
    using chronon3d::authoring::composition;
    chronon3d::Composition comp = composition()
        .name("dual-wrap")
        .width(1920).height(1080).duration(Frame{1})
        .scene([](chronon3d::authoring::Scene& scene,
                  const chronon3d::FrameContext&) {
            scene.layer("title", [](chronon3d::authoring::Layer& layer) {
                layer.text("HELLO")
                     .id("hello_text")
                     .font("assets/fonts/Poppins-Bold.ttf", 96.0f);
            });
        })
        .build();

    chronon3d::Scene evaluated = comp.evaluate(Frame{0});
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "title");
    REQUIRE(evaluated.layers()[0].nodes.size() == 1);
}

TEST_CASE("Authoring/Scene: SFINAE passthrough branch (LayerBuilder& closure) is honored") {
    using chronon3d::authoring::composition;
    int draw_count = 0;
    chronon3d::Composition comp = composition()
        .name("passthrough")
        .width(800).height(600)
        .scene([&draw_count](chronon3d::authoring::Scene& scene,
                             const chronon3d::FrameContext& ctx) {
            scene.layer("raw", [&ctx, &draw_count](LayerBuilder& layer) {
                layer.screen_dimensions(
                    static_cast<float>(ctx.width), static_cast<float>(ctx.height));
                layer.rect("bg", {
                    .size = {static_cast<float>(ctx.width), static_cast<float>(ctx.height)},
                    .color = Color::white()
                });
                ++draw_count;
            });
        })
        .build();

    (void)comp.evaluate(Frame{0});
    CHECK(draw_count == 1);
}

TEST_CASE("Authoring/CompositionBuilder: canonical FrameContext flows into Scene closure") {
    using chronon3d::authoring::composition;
    int ctx_width = 0;
    int ctx_height = 0;
    chronon3d::Composition comp = composition()
        .name("ctx-flow")
        .width(1280).height(720)
        .scene([&ctx_width, &ctx_height](chronon3d::authoring::Scene& scene,
                                        const chronon3d::FrameContext& ctx) {
            ctx_width = ctx.width;
            ctx_height = ctx.height;
            scene.layer("bg", [](LayerBuilder& layer) {
                layer.screen_dimensions(1280.0f, 720.0f);
                layer.fullscreen_rect("fs", Color::white());
            });
        })
        .build();

    (void)comp.evaluate(Frame{0});
    CHECK(ctx_width == 1280);
    CHECK(ctx_height == 720);
}

TEST_CASE("Authoring/CompositionBuilder: custom_builder(factory) is invoked per evaluate()") {
    using chronon3d::authoring::composition;
    int factory_call_count = 0;
    chronon3d::Composition comp = composition()
        .name("custom")
        .width(100).height(100)
        .custom_builder([&factory_call_count](const chronon3d::FrameContext& ctx) {
            ++factory_call_count;
            return SceneBuilder(ctx);
        })
        .scene([](chronon3d::authoring::Scene& scene,
                  const chronon3d::FrameContext&) {
            scene.layer("bg", [](chronon3d::authoring::Layer&) {});
        })
        .build();

    (void)comp.evaluate(Frame{0});
    CHECK(factory_call_count == 1);
}

TEST_CASE("Authoring/CompositionBuilder: build() consumes builder by rvalue") {
    using chronon3d::authoring::CompositionBuilder;
    CHECK(!std::is_copy_constructible_v<CompositionBuilder>);
    CHECK(!std::is_copy_assignable_v<CompositionBuilder>);
    CHECK(std::is_move_constructible_v<CompositionBuilder>);
    CHECK(std::is_move_assignable_v<CompositionBuilder>);
}

TEST_CASE("Authoring/Layer: explicit ctor throws when parent builder has no screen_dimensions") {
    LayerBuilder lb("no_dims");
    bool caught = false;
    std::string what_msg;
    try {
        chronon3d::authoring::Layer layer(lb);
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& error) {
        caught = true;
        what_msg = error.what();
    } catch (...) {
        FAIL("expected std::runtime_error specifically");
    }
    REQUIRE(caught);
    CHECK(what_msg.find("no_dims") != std::string::npos);
    CHECK(what_msg.find("screen_dimensions") != std::string::npos);
}

TEST_CASE("Authoring/Layer: explicit ctor succeeds when parent builder has screen_dimensions set") {
    LayerBuilder lb("with_dims");
    lb.screen_dimensions(1920.0f, 1080.0f);
    REQUIRE_NOTHROW(chronon3d::authoring::Layer{lb});
}

TEST_CASE("Authoring/Layer: explicit CanvasInfo ctor works without screen_dimensions") {
    LayerBuilder lb("explicit_canvas");
    REQUIRE_NOTHROW(chronon3d::authoring::Layer{
        lb, explicit_canvas(1920.0f, 1080.0f)});
}

TEST_CASE("Authoring/Text: script(uint32_t) chain mutates pending params") {
    LayerBuilder lb("script_round_trip");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);
    chronon3d::authoring::Text text = layer.text("ŁATIN");
    text.script(0x4C61746Eu);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.script == 0x4C61746Eu);
}

TEST_CASE("Authoring/Text: default script=0u is preserved") {
    LayerBuilder lb("script_default");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);
    chronon3d::authoring::Text text = layer.text("AUTODETECT");
    CHECK(TextRunBuilderInspector::pending_of(text)->params.script == 0u);
}

TEST_CASE("Authoring/Text: style(id) propagates shaping.script when non-zero") {
    LayerBuilder lb("script_propagate");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);

    StyleRegistry styles;
    chronon3d::TextStyle hero;
    hero.font_path = "Inter-Bold.ttf";
    hero.size = 96.0f;
    hero.shaping.direction = TextDirection::LTR;
    hero.shaping.language = "en";
    hero.shaping.script = 0x41726162u;
    styles.register_style("arabic.hero", hero);

    chronon3d::authoring::Text text = layer.text("AR");
    text.style("arabic.hero", styles);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.script == 0x41726162u);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.direction == TextDirection::LTR);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.language == "en");
}

TEST_CASE("Authoring/Text: style(id) preserves existing script when style script is zero") {
    LayerBuilder lb("script_zero");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);

    StyleRegistry styles;
    chronon3d::TextStyle default_style;
    default_style.size = 48.0f;
    default_style.shaping.script = 0u;
    styles.register_style("default.no.script", default_style);

    chronon3d::authoring::Text text = layer.text("DEFAULT");
    text.script(0x4C61746Eu);
    REQUIRE(TextRunBuilderInspector::pending_of(text)->params.script == 0x4C61746Eu);
    text.style("default.no.script", styles);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.script == 0x4C61746Eu);
}

TEST_CASE("Authoring/Text: script accepts high-bit pattern without sign extension") {
    LayerBuilder lb("script_highbit");
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb);
    chronon3d::authoring::Text text = layer.text("X");
    constexpr std::uint32_t kPattern = 0x80808080u;
    text.script(kPattern);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.script == kPattern);
    CHECK((TextRunBuilderInspector::pending_of(text)->params.script & 0x80000000u) != 0u);
}
