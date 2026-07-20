// ═══════════════════════════════════════════════════════════════════════════
// test_text_rich_authoring.cpp — Rich text span builder end-to-end tests
//
// Validates that authoring::Text::span() produces TextSpanOverride entries
// and that they flow through to the runtime TextDocument without creating
// multiple layers.
// ═══════════════════════════════════════════════════════════════════════════

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/core/types/sample_time.hpp>

using namespace chronon3d;
using namespace chronon3d::authoring;

static CanvasInfo default_canvas_info() {
    return CanvasInfo::from_dimensions(1920.0f, 1080.0f);
}

static LayerBuilder make_layer_builder() {
    LayerBuilder lb("rich_text_test_layer", SampleTime{});
    lb.screen_dimensions(1920.0f, 1080.0f);
    return lb;
}

TEST_CASE("Rich text: span() appends text and creates a TextSpanOverride") {
    auto lb = make_layer_builder();
    chronon3d::authoring::Layer lyr(lb, default_canvas_info());

    auto t = lyr.text("");
    t.span("QUESTA ").color(Color::yellow());

    CHECK(t.pending().params.text.content.value == "QUESTA ");
    REQUIRE(t.pending().params.text.spans.size() == 1);
    CHECK(t.pending().params.text.spans[0].byte_start == 0);
    CHECK(t.pending().params.text.spans[0].byte_end == 7);
    REQUIRE(t.pending().params.text.spans[0].color.has_value());
    CHECK(t.pending().params.text.spans[0].color->r == doctest::Approx(1.0f));
    CHECK(t.pending().params.text.spans[0].color->g == doctest::Approx(1.0f));
    CHECK(t.pending().params.text.spans[0].color->b == doctest::Approx(0.0f));
}

TEST_CASE("Rich text: chained spans preserve byte ranges and styles") {
    auto lb = make_layer_builder();
    chronon3d::authoring::Layer lyr(lb, default_canvas_info());

    auto t = lyr.text("");
    t.span("QUESTA ").color(Color::yellow())
     .span("PAROLA").color(Color::yellow()).weight(800)
     .span(" IMPORTANTE");

    const auto& spans = t.pending().params.text.spans;
    const auto& content = t.pending().params.text.content.value;

    CHECK(content == "QUESTA PAROLA IMPORTANTE");
    REQUIRE(spans.size() == 3);

    // Span 1: "QUESTA "
    CHECK(spans[0].byte_start == 0);
    CHECK(spans[0].byte_end == 7);
    REQUIRE(spans[0].color.has_value());

    // Span 2: "PAROLA"
    CHECK(spans[1].byte_start == 7);
    CHECK(spans[1].byte_end == 13);
    REQUIRE(spans[1].color.has_value());
    REQUIRE(spans[1].font.has_value());
    CHECK(spans[1].font->font_weight == 800);

    // Span 3: " IMPORTANTE"
    CHECK(spans[2].byte_start == 13);
    CHECK(spans[2].byte_end == 24);
    CHECK_FALSE(spans[2].color.has_value());
}

TEST_CASE("Rich text: span supports tracking, stroke, semantic_id") {
    auto lb = make_layer_builder();
    chronon3d::authoring::Layer lyr(lb, default_canvas_info());

    auto t = lyr.text("");
    t.span("Hello")
        .tracking(2.5f)
        .stroke(3.0f, Color::red())
        .semantic_id("greeting");

    const auto& spans = t.pending().params.text.spans;
    REQUIRE(spans.size() == 1);
    REQUIRE(spans[0].tracking.has_value());
    CHECK(spans[0].tracking.value() == doctest::Approx(2.5f));
    REQUIRE(spans[0].stroke.has_value());
    CHECK(spans[0].stroke->width_em == doctest::Approx(3.0f));
    REQUIRE(spans[0].stroke->color.has_value());
    CHECK(spans[0].stroke->color->r == doctest::Approx(1.0f));
    REQUIRE(spans[0].semantic_id.has_value());
    CHECK(*spans[0].semantic_id == "greeting");
}

TEST_CASE("Rich text: to_text_document converts span overrides to TextStyleSpan") {
    auto lb = make_layer_builder();
    chronon3d::authoring::Layer lyr(lb, default_canvas_info());

    auto t = lyr.text("");
    t.span("A").color(Color::red()).weight(700).tracking(1.0f)
     .span("B").stroke(2.0f, Color::white());

    TextDefinition def;
    def.content.value = t.pending().params.text.content.value;
    def.style.font = t.pending().params.text.font;
    def.spans = t.pending().params.text.spans;

    TextDocument doc = to_text_document(def);

    CHECK(doc.utf8 == "AB");
    REQUIRE(doc.spans.size() == 2);

    const auto& s0 = doc.spans[0];
    CHECK(s0.byte_start == 0);
    CHECK(s0.byte_end == 1);
    REQUIRE(s0.appearance.has_value());
    CHECK(s0.appearance->color.r == doctest::Approx(1.0f));
    REQUIRE(s0.font.has_value());
    CHECK(s0.font->font_weight == 700);
    REQUIRE(s0.tracking.has_value());
    CHECK(s0.tracking.value() == doctest::Approx(1.0f));

    const auto& s1 = doc.spans[1];
    CHECK(s1.byte_start == 1);
    CHECK(s1.byte_end == 2);
    REQUIRE(s1.appearance.has_value());
    CHECK(s1.appearance->paint.stroke_enabled == true);
    CHECK(s1.appearance->paint.stroke_width == doctest::Approx(2.0f));
}

TEST_CASE("Rich text: ligature preservation across same-font spans") {
    // A ligature like "fi" should remain a single shaped glyph when the
    // only difference between spans is color, because the resolver only
    // splits runs when the effective font changes.
    auto lb = make_layer_builder();
    chronon3d::authoring::Layer lyr(lb, default_canvas_info());

    auto t = lyr.text("");
    t.span("fi").color(Color::red());

    TextDefinition def;
    def.content.value = t.pending().params.text.content.value;
    def.style.font.font_path = "assets/fonts/Inter-Bold.ttf";
    def.style.font.font_size = 64.0f;
    def.spans = t.pending().params.text.spans;

    TextDocument doc = to_text_document(def);
    doc.split_paragraphs();

    CHECK(doc.utf8 == "fi");
    REQUIRE(doc.spans.size() == 1);
    CHECK(doc.spans[0].appearance->color.r == doctest::Approx(1.0f));
}
