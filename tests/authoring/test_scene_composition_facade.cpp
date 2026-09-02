#include "authoring_dsl_test_support.hpp"

using chronon3d::SampleTime;
using chronon3d::authoring::testing::TextRunBuilderInspector;

namespace {
chronon3d::CanvasInfo explicit_canvas(float width, float height) {
    return chronon3d::CanvasInfo::with_safe_area(
        width, height, chronon3d::SafeAreaPreset{});
}

chronon3d::Scene evaluate_frame(const chronon3d::Composition& composition,
                                chronon3d::Frame frame) {
    const auto sample = chronon3d::SampleTime::from_frame_int(
        frame, composition.frame_rate());
    return composition.evaluate(chronon3d::make_frame_context({
        .global_time = sample,
        .duration = composition.duration(),
        .width = composition.width(),
        .height = composition.height(),
    }));
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
      .frame_rate(FrameRate{30, 1});

    chronon3d::Composition comp = std::move(cb).build();
    CHECK(comp.name() == "hero-showcase");
    CHECK(comp.width() == 1920);
    CHECK(comp.height() == 1080);
    CHECK(comp.duration().integral() == 60);
    CHECK(comp.frame_rate().numerator == 30);
}

TEST_CASE("Authoring/CompositionBuilder: empty composition (no .scene()) renders zero layers") {
    using chronon3d::authoring::CompositionBuilder;
    CompositionBuilder cb;
    cb.name("empty").width(640).height(480);
    chronon3d::Composition comp = std::move(cb).build();
    chronon3d::Scene scene = evaluate_frame(comp, Frame{0});
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

    chronon3d::Scene evaluated = evaluate_frame(comp, Frame{0});
    REQUIRE(evaluated.layers().size() == 1);
    CHECK(evaluated.layers()[0].name == "title");
    REQUIRE(evaluated.layers()[0].nodes.size() == 1);
}

TEST_CASE("Authoring/Scene: typed layer facade receives canvas dimensions") {
    using chronon3d::authoring::composition;
    int draw_count = 0;
    chronon3d::Composition comp = composition()
        .name("passthrough")
        .width(800).height(600)
        .scene([&draw_count](chronon3d::authoring::Scene& scene,
                             const chronon3d::FrameContext&) {
            scene.layer("raw", [&draw_count](chronon3d::authoring::Layer& layer) {
                layer.fullscreen_rect("bg", Color::white());
                ++draw_count;
            });
        })
        .build();

    (void)evaluate_frame(comp, Frame{0});
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
            scene.layer("bg", [](chronon3d::authoring::Layer& layer) {
                layer.fullscreen_rect("fs", Color::white());
            });
        })
        .build();

    (void)evaluate_frame(comp, Frame{0});
    CHECK(ctx_width == 1280);
    CHECK(ctx_height == 720);
}

TEST_CASE("Authoring/CompositionBuilder: build() consumes builder by rvalue") {
    using chronon3d::authoring::CompositionBuilder;
    CHECK(!std::is_copy_constructible_v<CompositionBuilder>);
    CHECK(!std::is_copy_assignable_v<CompositionBuilder>);
    CHECK(std::is_move_constructible_v<CompositionBuilder>);
    CHECK(std::is_move_assignable_v<CompositionBuilder>);
}

TEST_CASE("Authoring/Layer: explicit CanvasInfo ctor works without screen_dimensions") {
    LayerBuilder lb("explicit_canvas", SampleTime{});
    REQUIRE_NOTHROW(chronon3d::authoring::Layer{
        lb, explicit_canvas(1920.0f, 1080.0f)});
}

TEST_CASE("Authoring/Text: script(uint32_t) chain mutates pending params") {
    LayerBuilder lb("script_round_trip", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb, explicit_canvas(1920.0f, 1080.0f));
    chronon3d::authoring::Text text = layer.text("ŁATIN");
    text.script(0x4C61746Eu);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.shaping.script == 0x4C61746Eu);
}

TEST_CASE("Authoring/Text: default script=0u is preserved") {
    LayerBuilder lb("script_default", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb, explicit_canvas(1920.0f, 1080.0f));
    chronon3d::authoring::Text text = layer.text("AUTODETECT");
    CHECK(TextRunBuilderInspector::pending_of(text)->params.shaping.script == 0u);
}

TEST_CASE("Authoring/Text: script accepts high-bit pattern without sign extension") {
    LayerBuilder lb("script_highbit", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    chronon3d::authoring::Layer layer(lb, explicit_canvas(1920.0f, 1080.0f));
    chronon3d::authoring::Text text = layer.text("X");
    constexpr std::uint32_t kPattern = 0x80808080u;
    text.script(kPattern);
    CHECK(TextRunBuilderInspector::pending_of(text)->params.shaping.script == kPattern);
    CHECK((TextRunBuilderInspector::pending_of(text)->params.shaping.script & 0x80000000u) != 0u);
}
