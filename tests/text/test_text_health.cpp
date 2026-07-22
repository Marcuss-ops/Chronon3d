#include <doctest/doctest.h>

#include <chronon3d/core/config.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using namespace chronon3d;

namespace {

struct TextHealthEnvironment {
    Config config{};
    std::unique_ptr<chronon3d::runtime::RenderRuntime> runtime;
    FontEngine font_engine;

    TextHealthEnvironment()
        : runtime(chronon3d::runtime::RenderRuntime::create(
              chronon3d::runtime::RuntimeConfig{config, std::nullopt}).value()),
          font_engine(runtime->resolver()) {}
};

[[nodiscard]] std::optional<FontSpec> find_health_font(
    FontEngine& engine,
    float size
) {
    std::array<FontSpec, 4> candidates{};

    // Prefer a broad Unicode system font, then the bundled project font.
    candidates[0].font_family = "DejaVu Sans";
    candidates[0].font_weight = 400;

    candidates[1].font_path = "assets/fonts/Inter-Bold.ttf";
    candidates[1].font_family = "Inter";
    candidates[1].font_weight = 700;

    candidates[2].font_family = "Liberation Sans";
    candidates[2].font_weight = 400;

    candidates[3].font_family = "Arial";
    candidates[3].font_weight = 400;

    for (auto candidate : candidates) {
        candidate.font_size = size;
        if (engine.can_load(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

[[nodiscard]] TextRunSpec make_health_spec(
    std::string text,
    FontSpec font,
    float box_width = 1000.0f
) {
    TextRunSpec spec;
    spec.text.content.value = std::move(text);
    spec.text.font = std::move(font);
    spec.text.layout.box = {box_width, 320.0f};
    spec.text.layout.anchor = TextAnchor::Center;
    spec.text.layout.align = TextAlign::Center;
    spec.text.layout.vertical_align = VerticalAlign::Middle;
    spec.text.layout.wrap = TextWrap::Word;
    spec.text.layout.line_height = 1.2f;
    spec.text.placement = TextPlacement{
        TextPlacementKind::CanvasCenter,
        {0.0f, 0.0f}
    };
    spec.text.appearance.color = Color{0.92f, 0.96f, 1.0f, 1.0f};
    spec.text.appearance.paint.fill = spec.text.appearance.color;
    spec.direction = TextDirection::LTR;
    spec.language = "en";
    spec.cache_layout = true;
    return spec;
}

[[nodiscard]] std::shared_ptr<TextRunShape> materialize_or_fail(
    const TextRunSpec& spec,
    FontEngine& engine
) {
    return materialize_text_run_shape(spec, &engine, SampleTime{});
}

void require_valid_shape(
    const std::shared_ptr<TextRunShape>& shape,
    std::string_view expected_text
) {
    REQUIRE_MESSAGE(shape != nullptr,
        "Text materialization returned nullptr. Check font resolution and shaping.");
    REQUIRE_MESSAGE(shape->layout != nullptr,
        "TextRunShape was created without a TextRunLayout.");
    CHECK(shape->layout->source_text == expected_text);
    REQUIRE_MESSAGE(!shape->layout->placed.glyphs.empty(),
        "Text shaping produced zero glyphs for non-empty input.");
    CHECK(shape->glyphs.size() == shape->layout->placed.glyphs.size());
    CHECK(shape->layout->units.glyph_to_grapheme.size()
          == shape->layout->placed.glyphs.size());
    CHECK(shape->layout->bounds.x > 0.0f);
    CHECK(shape->layout->bounds.y > 0.0f);

    for (const auto& glyph : shape->glyphs) {
        CHECK(std::isfinite(glyph.layout_position.x));
        CHECK(std::isfinite(glyph.layout_position.y));
        CHECK(std::isfinite(glyph.opacity));
        CHECK(glyph.opacity >= 0.0f);
        CHECK(glyph.opacity <= 1.0f);
    }
}

} // namespace

TEST_CASE("Text health / real font creates a valid glyph-bearing TextRunShape") {
    TextHealthEnvironment env;
    const auto font = find_health_font(env.font_engine, 48.0f);
    REQUIRE_MESSAGE(font.has_value(),
        "No usable text-health font found. Expected bundled Inter or a common system font.");

    const auto spec = make_health_spec("Chronon3D text health", *font);
    const auto shape = materialize_or_fail(spec, env.font_engine);

    require_valid_shape(shape, spec.text.content.value);
    CHECK(shape->layout->font.font_size == doctest::Approx(48.0f));
    CHECK(shape->layout->font_size == doctest::Approx(48.0f));
    CHECK(shape->paint.fill == spec.text.appearance.paint.fill);
    CHECK(shape->layout->direction == TextDirection::LTR);
    CHECK(shape->layout->language == "en");
}

TEST_CASE("Text health / LayerBuilder emits one materialized TextRun node") {
    TextHealthEnvironment env;
    const auto font = find_health_font(env.font_engine, 56.0f);
    REQUIRE_MESSAGE(font.has_value(), "No usable font found for LayerBuilder text test.");

    auto spec = make_health_spec("Builder to node", *font);

    LayerBuilder builder("text_health_layer", SampleTime{});
    builder.screen_dimensions(1280.0f, 720.0f);
    builder.font_engine(&env.font_engine);
    builder.animated_text("health_text", std::move(spec)).commit();

    Layer layer = builder.build();
    REQUIRE(layer.nodes.size() == 1);

    const RenderNode& node = layer.nodes.front();
    CHECK(node.name == "health_text");
    CHECK(node.shape.type() == ShapeType::TextRun);

    const auto& handle = node.shape.text_run_shape_handle();
    require_valid_shape(handle.value, "Builder to node");
    CHECK(node.font_engine == &env.font_engine);
}

TEST_CASE("Text health / fluent opacity animator reaches every glyph") {
    TextHealthEnvironment env;
    const auto font = find_health_font(env.font_engine, 44.0f);
    REQUIRE_MESSAGE(font.has_value(), "No usable font found for animator text test.");

    auto spec = make_health_spec("Animated opacity", *font);

    LayerBuilder builder("animated_text_health", SampleTime{});
    builder.screen_dimensions(1280.0f, 720.0f);
    builder.font_engine(&env.font_engine);
    builder.animated_text("animated_text", std::move(spec))
        .opacity(0.35f)
        .commit();

    Layer layer = builder.build();
    REQUIRE(layer.nodes.size() == 1);
    REQUIRE(layer.nodes.front().shape.type() == ShapeType::TextRun);

    const auto shape = layer.nodes.front().shape.text_run_shape_handle().value;
    require_valid_shape(shape, "Animated opacity");
    REQUIRE(!shape->glyphs.empty());

    for (const auto& glyph : shape->glyphs) {
        CHECK(glyph.opacity == doctest::Approx(0.35f));
    }
}

TEST_CASE("Text health / word wrapping increases layout height and respects box width") {
    TextHealthEnvironment env;
    const auto font = find_health_font(env.font_engine, 34.0f);
    REQUIRE_MESSAGE(font.has_value(), "No usable font found for wrapping test.");

    auto spec = make_health_spec(
        "Chronon3D wraps this sentence across multiple visible lines",
        *font,
        260.0f);
    spec.text.layout.box.y = 500.0f;
    spec.text.layout.align = TextAlign::Left;

    const auto shape = materialize_or_fail(spec, env.font_engine);
    require_valid_shape(shape, spec.text.content.value);
    CHECK(shape->layout->wrap == TextWrap::Word);
    CHECK(shape->layout->line_height > 0.0f);
    CHECK(shape->layout->bounds.y > shape->layout->line_height * 1.5f);
    CHECK(shape->layout->bounds.x <= spec.text.layout.box.x * 1.05f);
}

TEST_CASE("Text health / repeated materialization reuses the cached layout") {
    TextHealthEnvironment env;
    const auto font = find_health_font(env.font_engine, 40.0f);
    REQUIRE_MESSAGE(font.has_value(), "No usable font found for text cache test.");

    const auto spec = make_health_spec("Stable cached layout", *font);
    const auto first = materialize_or_fail(spec, env.font_engine);
    const auto second = materialize_or_fail(spec, env.font_engine);

    require_valid_shape(first, spec.text.content.value);
    require_valid_shape(second, spec.text.content.value);
    CHECK(first->layout.get() == second->layout.get());
    CHECK(first->layout->layout_hash() == second->layout->layout_hash());
}

TEST_CASE("Text health / Unicode and RTL text survive shaping and materialization") {
    TextHealthEnvironment env;
    const auto font = find_health_font(env.font_engine, 46.0f);
    REQUIRE_MESSAGE(font.has_value(), "No usable font found for Unicode text test.");

    auto spec = make_health_spec("مرحبا بالعالم", *font, 800.0f);
    spec.direction = TextDirection::RTL;
    spec.language = "ar";
    spec.script = 0x41726162u; // OpenType 'Arab'.

    const auto shape = materialize_or_fail(spec, env.font_engine);
    require_valid_shape(shape, spec.text.content.value);
    CHECK(shape->layout->direction == TextDirection::RTL);
    CHECK(shape->layout->language == "ar");
    CHECK(shape->layout->units.grapheme_count > 0);
    CHECK(shape->layout->units.word_count >= 2);
}
