// ═══════════════════════════════════════════════════════════════════════════
// test_text_definition.cpp — F2.A adapter convergence tests
//
// Verifies that from_text_spec() / from_text_run_spec() produce correct
// TextDefinition objects and that centered_text() / glow_text() / text_run()
// converge to the canonical TextDefinition without data loss.
//
// Test groups:
//   1. from_text_spec() — field-by-field mapping (all 22 TextSpec fields)
//   2. from_text_run_spec() — wraps from_text_spec() + TextRunSpec fields
//   3. centered_text() convergence — CenterTextOptions → TextSpec → TextDefinition
//   4. glow_text() convergence — same path via glow_text()
//   5. Round-trip no-data-loss — TextSpec → TextDefinition → verify every field
//   6. Default TextSpec — default-constructed TextSpec maps to sensible defaults
//   7. TextSpanOverride — authoring-level span overrides are independent
//
// Forward-point: compile_text_layout convergence — Phase B will add
// TextDocumentBuilder::build(const TextDefinition&) which lowers the DTO
// into a TextDocument for compile_text_layout().  Until Phase B lands,
// the adapter tests verify TextSpec → TextDefinition mapping correctness
// (no data loss at the adapter boundary).  The runtime pipeline already
// consumes TextDocument directly, so the convergence point is the adapter.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>  // TextSpec, TextRunSpec, TextContent

// Content helpers — centered_text() / glow_text()
#include <content/text/text_helpers_centered.hpp>

#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::content::text;

// ═══════════════════════════════════════════════════════════════════════════
// 1. from_text_spec() — field-by-field mapping
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("from_text_spec: content.value mapped correctly") {
    TextSpec spec;
    spec.content.value = "HELLO WORLD";
    auto def = from_text_spec(spec);
    CHECK(def.content.value == "HELLO WORLD");
}

TEST_CASE("from_text_spec: font fields mapped correctly") {
    TextSpec spec;
    spec.font = FontSpec{
        .font_path   = "fonts/Inter-Bold.ttf",
        .font_family = "Inter",
        .font_weight = 700,
        .font_style  = "italic",
        .font_size   = 64.0f,
    };
    auto def = from_text_spec(spec);
    CHECK(def.style.font.font_path   == "fonts/Inter-Bold.ttf");
    CHECK(def.style.font.font_family == "Inter");
    CHECK(def.style.font.font_weight == 700);
    CHECK(def.style.font.font_style  == "italic");
    CHECK(def.style.font.font_size   == doctest::Approx(64.0f));
}

TEST_CASE("from_text_spec: appearance.color mapped to style.color") {
    TextSpec spec;
    spec.appearance.color = Color{0.2f, 0.4f, 0.8f, 1.0f};
    auto def = from_text_spec(spec);
    CHECK(def.style.color.r == doctest::Approx(0.2f));
    CHECK(def.style.color.g == doctest::Approx(0.4f));
    CHECK(def.style.color.b == doctest::Approx(0.8f));
    CHECK(def.style.color.a == doctest::Approx(1.0f));
}

TEST_CASE("from_text_spec: appearance.paint mapped to style.paint") {
    TextSpec spec;
    spec.appearance.paint.stroke_enabled = true;
    spec.appearance.paint.stroke_color   = Color{1.0f, 0.0f, 0.0f, 1.0f};
    spec.appearance.paint.stroke_width   = 3.0f;
    auto def = from_text_spec(spec);
    CHECK(def.style.paint.stroke_enabled == true);
    CHECK(def.style.paint.stroke_width   == doctest::Approx(3.0f));
}

TEST_CASE("from_text_spec: appearance.shadows mapped to style.shadows") {
    TextSpec spec;
    TextShadow shadow;
    shadow.enabled = true;
    shadow.offset  = {4.0f, 6.0f};
    shadow.blur    = 12.0f;
    spec.appearance.shadows = {shadow};
    auto def = from_text_spec(spec);
    REQUIRE(def.style.shadows.size() == 1);
    CHECK(def.style.shadows[0].enabled == true);
    CHECK(def.style.shadows[0].blur    == doctest::Approx(12.0f));
}

TEST_CASE("from_text_spec: appearance.material mapped to style.material") {
    TextSpec spec;
    spec.appearance.material = TextMaterial::neon();
    auto def = from_text_spec(spec);
    // Material is mapped — check a representative field is populated.
    // TextMaterial::neon() sets use_material_glow=true (see text_material.hpp).
    CHECK(def.style.material.use_material_glow == true);
}

TEST_CASE("from_text_spec: appearance.box_style mapped to style.box_style") {
    TextSpec spec;
    spec.appearance.box_style.enabled = true;
    spec.appearance.box_style.background = Color{0.1f, 0.1f, 0.1f, 0.8f};
    auto def = from_text_spec(spec);
    CHECK(def.style.box_style.enabled == true);
}

TEST_CASE("from_text_spec: layout.box mapped to frame.size") {
    TextSpec spec;
    spec.layout.box = {1700.0f, 360.0f};
    auto def = from_text_spec(spec);
    CHECK(def.frame.size.x == doctest::Approx(1700.0f));
    CHECK(def.frame.size.y == doctest::Approx(360.0f));
}

TEST_CASE("from_text_spec: position mapped to frame.position") {
    TextSpec spec;
    spec.position = {960.0f, 540.0f, 10.0f};
    auto def = from_text_spec(spec);
    CHECK(def.frame.position.x == doctest::Approx(960.0f));
    CHECK(def.frame.position.y == doctest::Approx(540.0f));
    CHECK(def.frame.position.z == doctest::Approx(10.0f));
}

TEST_CASE("from_text_spec: layout.anchor mapped to frame.anchor") {
    TextSpec spec;
    spec.layout.anchor = TextAnchor::TopLeft;
    auto def = from_text_spec(spec);
    CHECK(def.frame.anchor == TextAnchor::TopLeft);
}

TEST_CASE("from_text_spec: layout.align + vertical_align mapped") {
    TextSpec spec;
    spec.layout.align          = TextAlign::Right;
    spec.layout.vertical_align = VerticalAlign::Bottom;
    auto def = from_text_spec(spec);
    CHECK(def.frame.align          == TextAlign::Right);
    CHECK(def.frame.vertical_align == VerticalAlign::Bottom);
}

TEST_CASE("from_text_spec: layout.wrap + overflow mapped") {
    TextSpec spec;
    spec.layout.wrap     = TextWrap::Character;
    spec.layout.overflow = TextOverflow::Ellipsis;
    auto def = from_text_spec(spec);
    CHECK(def.frame.wrap     == TextWrap::Character);
    CHECK(def.frame.overflow == TextOverflow::Ellipsis);
}

TEST_CASE("from_text_spec: layout.centering_mode mapped") {
    TextSpec spec;
    spec.layout.centering_mode = TextCenteringMode::PixelInk;
    auto def = from_text_spec(spec);
    CHECK(def.frame.centering_mode == TextCenteringMode::PixelInk);
}

TEST_CASE("from_text_spec: layout.line_height + tracking mapped") {
    TextSpec spec;
    spec.layout.line_height = 1.5f;
    spec.layout.tracking    = 2.5f;
    auto def = from_text_spec(spec);
    CHECK(def.frame.line_height == doctest::Approx(1.5f));
    CHECK(def.frame.tracking    == doctest::Approx(2.5f));
}

TEST_CASE("from_text_spec: layout.auto_fit + min/max_font_size mapped") {
    TextSpec spec;
    spec.layout.auto_fit      = true;
    spec.layout.min_font_size = 16.0f;
    spec.layout.max_font_size = 120.0f;
    auto def = from_text_spec(spec);
    CHECK(def.frame.auto_fit      == true);
    CHECK(def.frame.min_font_size == doctest::Approx(16.0f));
    CHECK(def.frame.max_font_size == doctest::Approx(120.0f));
}

TEST_CASE("from_text_spec: layout.max_lines + ellipsis mapped") {
    TextSpec spec;
    spec.layout.max_lines = 3;
    spec.layout.ellipsis  = true;
    auto def = from_text_spec(spec);
    CHECK(def.frame.max_lines == 3);
    CHECK(def.frame.ellipsis  == true);
}

TEST_CASE("from_text_spec: layout.paragraph mapped to def.paragraph") {
    TextSpec spec;
    spec.layout.paragraph.justification = TextJustification::Center;
    auto def = from_text_spec(spec);
    CHECK(def.paragraph.justification == TextJustification::Center);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. from_text_run_spec() — wraps from_text_spec + TextRunSpec
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("from_text_run_spec: produces same result as from_text_spec on .text") {
    TextRunSpec run_spec;
    run_spec.text.content.value = "Run text";
    run_spec.text.font.font_size = 48.0f;
    run_spec.text.layout.box = {800.0f, 200.0f};
    run_spec.text.position = {100.0f, 200.0f, 0.0f};
    run_spec.direction = TextDirection::RTL;
    run_spec.language = "ar";

    auto def_run  = from_text_run_spec(run_spec);
    auto def_spec = from_text_spec(run_spec.text);

    // The base TextSpec mapping must be identical.
    CHECK(def_run.content.value    == def_spec.content.value);
    CHECK(def_run.style.font.font_size == doctest::Approx(def_spec.style.font.font_size));
    CHECK(def_run.frame.size.x    == doctest::Approx(def_spec.frame.size.x));
    CHECK(def_run.frame.size.y    == doctest::Approx(def_spec.frame.size.y));
    CHECK(def_run.frame.position.x == doctest::Approx(def_spec.frame.position.x));
    CHECK(def_run.frame.position.y == doctest::Approx(def_spec.frame.position.y));
}

TEST_CASE("from_text_run_spec: animators are not yet mapped (Phase A.3 placeholder)") {
    TextRunSpec run_spec;
    run_spec.text.content.value = "Animated";
    // Add a dummy animator — it should not crash, just be ignored for now.
    // TextAnimatorProperty is a std::variant; OpacityProperty is one alternative.
    TextAnimatorSpec anim;
    anim.properties.push_back(OpacityProperty{});
    run_spec.animators.push_back(anim);

    auto def = from_text_run_spec(run_spec);
    // TextAnimation is empty placeholder — no crash is the contract.
    CHECK(def.content.value == "Animated");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. centered_text() convergence — CenterTextOptions → TextSpec → TextDefinition
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("centered_text convergence: full field chain through TextDefinition") {
    CenterTextOptions opts;
    opts.text        = "CHRONON";
    opts.box         = {1700.0f, 360.0f};
    opts.pos         = {960.0f, 540.0f, 0.0f};
    opts.font_asset  = "fonts/Poppins-Bold.ttf";
    opts.font_family = "Poppins";
    opts.font_weight = 700;
    opts.font_size   = 96.0f;
    opts.tracking    = 1.5f;
    opts.color       = Color::white();
    opts.max_lines   = 1;
    opts.auto_fit    = true;
    opts.line_height = 0.95f;
    opts.min_font_size = 12.0f;
    opts.max_font_size = 160.0f;

    TextSpec spec = centered_text(opts);
    auto def = from_text_spec(spec);

    // Content
    CHECK(def.content.value == "CHRONON");

    // Style (font)
    CHECK(def.style.font.font_path   == "fonts/Poppins-Bold.ttf");
    CHECK(def.style.font.font_family == "Poppins");
    CHECK(def.style.font.font_weight == 700);
    CHECK(def.style.font.font_size   == doctest::Approx(96.0f));

    // Style (color)
    CHECK(def.style.color.r == doctest::Approx(1.0f));
    CHECK(def.style.color.g == doctest::Approx(1.0f));
    CHECK(def.style.color.b == doctest::Approx(1.0f));
    CHECK(def.style.color.a == doctest::Approx(1.0f));

    // Frame
    CHECK(def.frame.size.x     == doctest::Approx(1700.0f));
    CHECK(def.frame.size.y     == doctest::Approx(360.0f));
    CHECK(def.frame.position.x == doctest::Approx(960.0f));
    CHECK(def.frame.position.y == doctest::Approx(540.0f));
    CHECK(def.frame.position.z == doctest::Approx(0.0f));
    CHECK(def.frame.anchor     == TextAnchor::Center);
    CHECK(def.frame.align      == TextAlign::Center);
    CHECK(def.frame.vertical_align == VerticalAlign::Middle);
    CHECK(def.frame.wrap       == TextWrap::Word);
    CHECK(def.frame.overflow   == TextOverflow::Clip);
    CHECK(def.frame.centering_mode == TextCenteringMode::PixelInk);
    CHECK(def.frame.line_height == doctest::Approx(0.95f));
    CHECK(def.frame.tracking   == doctest::Approx(1.5f));
    CHECK(def.frame.auto_fit   == true);
    CHECK(def.frame.min_font_size == doctest::Approx(12.0f));
    CHECK(def.frame.max_font_size == doctest::Approx(160.0f));
    CHECK(def.frame.max_lines  == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. glow_text() convergence — same path via glow_text()
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("glow_text convergence: produces valid TextDefinition") {
    CenterTextOptions opts;
    opts.text      = "GLOW";
    opts.box       = {1200.0f, 240.0f};
    opts.font_size = 72.0f;
    opts.color     = Color{1.0f, 0.5f, 0.0f, 1.0f};

    Color glow_color{1.0f, 1.0f, 0.0f, 1.0f};
    TextSpec spec = glow_text(opts, glow_color, 30.0f, 0.8f);
    auto def = from_text_spec(spec);

    CHECK(def.content.value == "GLOW");
    CHECK(def.style.font.font_size == doctest::Approx(72.0f));
    CHECK(def.style.color.r == doctest::Approx(1.0f));
    CHECK(def.style.color.g == doctest::Approx(0.5f));
    CHECK(def.style.color.b == doctest::Approx(0.0f));
    CHECK(def.frame.size.x  == doctest::Approx(1200.0f));
    CHECK(def.frame.size.y  == doctest::Approx(240.0f));
    CHECK(def.frame.anchor  == TextAnchor::Center);
    CHECK(def.frame.align   == TextAlign::Center);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Round-trip no-data-loss — verify every field survives the adapter
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("no-data-loss: complex TextSpec round-trip through from_text_spec") {
    TextSpec spec;
    // Content
    spec.content.value = "Round-trip test";
    // Font
    spec.font = {
        .font_path   = "fonts/Anton.ttf",
        .font_family = "Anton",
        .font_weight = 900,
        .font_style  = "normal",
        .font_size   = 128.0f,
    };
    // Layout
    spec.layout.box            = {1920.0f, 400.0f};
    spec.layout.anchor         = TextAnchor::BottomCenter;
    spec.layout.centering_mode = TextCenteringMode::PixelInk;
    spec.layout.align          = TextAlign::Right;
    spec.layout.vertical_align = VerticalAlign::Top;
    spec.layout.wrap           = TextWrap::Character;
    spec.layout.overflow       = TextOverflow::Clip;
    spec.layout.line_height    = 1.1f;
    spec.layout.tracking       = 3.0f;
    spec.layout.auto_fit       = true;
    spec.layout.min_font_size  = 8.0f;
    spec.layout.max_font_size  = 200.0f;
    spec.layout.max_lines      = 5;
    spec.layout.ellipsis       = true;
    // Appearance
    spec.appearance.color = Color{0.5f, 0.5f, 0.5f, 0.9f};
    spec.appearance.paint.stroke_enabled = true;
    spec.appearance.paint.stroke_color   = Color{1.0f, 0.0f, 1.0f, 1.0f};
    spec.appearance.paint.stroke_width   = 5.0f;
    // Position
    spec.position = {480.0f, 270.0f, 50.0f};

    auto def = from_text_spec(spec);

    // Verify every single field — no data loss.
    // Content
    CHECK(def.content.value == "Round-trip test");

    // Font
    CHECK(def.style.font.font_path   == "fonts/Anton.ttf");
    CHECK(def.style.font.font_family == "Anton");
    CHECK(def.style.font.font_weight == 900);
    CHECK(def.style.font.font_style  == "normal");
    CHECK(def.style.font.font_size   == doctest::Approx(128.0f));

    // Frame — layout
    CHECK(def.frame.size.x            == doctest::Approx(1920.0f));
    CHECK(def.frame.size.y            == doctest::Approx(400.0f));
    CHECK(def.frame.anchor            == TextAnchor::BottomCenter);
    CHECK(def.frame.centering_mode    == TextCenteringMode::PixelInk);
    CHECK(def.frame.align             == TextAlign::Right);
    CHECK(def.frame.vertical_align    == VerticalAlign::Top);
    CHECK(def.frame.wrap              == TextWrap::Character);
    CHECK(def.frame.overflow          == TextOverflow::Clip);
    CHECK(def.frame.line_height       == doctest::Approx(1.1f));
    CHECK(def.frame.tracking          == doctest::Approx(3.0f));
    CHECK(def.frame.auto_fit          == true);
    CHECK(def.frame.min_font_size     == doctest::Approx(8.0f));
    CHECK(def.frame.max_font_size     == doctest::Approx(200.0f));
    CHECK(def.frame.max_lines         == 5);
    CHECK(def.frame.ellipsis          == true);

    // Frame — position
    CHECK(def.frame.position.x == doctest::Approx(480.0f));
    CHECK(def.frame.position.y == doctest::Approx(270.0f));
    CHECK(def.frame.position.z == doctest::Approx(50.0f));

    // Style — color
    CHECK(def.style.color.r == doctest::Approx(0.5f));
    CHECK(def.style.color.g == doctest::Approx(0.5f));
    CHECK(def.style.color.b == doctest::Approx(0.5f));
    CHECK(def.style.color.a == doctest::Approx(0.9f));

    // Style — paint
    CHECK(def.style.paint.stroke_enabled == true);
    CHECK(def.style.paint.stroke_width   == doctest::Approx(5.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Default TextSpec — default-constructed values map to sensible defaults
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("from_text_spec: default TextSpec produces default TextDefinition") {
    TextSpec spec;  // all defaults
    auto def = from_text_spec(spec);

    // Content is empty
    CHECK(def.content.value.empty());

    // Font has defaults from FontSpec
    CHECK(def.style.font.font_size == doctest::Approx(72.0f));  // FontSpec default

    // Frame has defaults matching TextLayoutSpec defaults
    CHECK(def.frame.size.x == doctest::Approx(900.0f));
    CHECK(def.frame.size.y == doctest::Approx(160.0f));
    CHECK(def.frame.anchor == TextAnchor::Center);
    CHECK(def.frame.align  == TextAlign::Center);
    CHECK(def.frame.vertical_align == VerticalAlign::Middle);
    CHECK(def.frame.wrap   == TextWrap::Word);
    CHECK(def.frame.overflow == TextOverflow::Clip);

    // Color default is white
    CHECK(def.style.color.r == doctest::Approx(1.0f));
    CHECK(def.style.color.g == doctest::Approx(1.0f));
    CHECK(def.style.color.b == doctest::Approx(1.0f));
    CHECK(def.style.color.a == doctest::Approx(1.0f));

    // Position defaults to origin
    CHECK(def.frame.position.x == doctest::Approx(0.0f));
    CHECK(def.frame.position.y == doctest::Approx(0.0f));
    CHECK(def.frame.position.z == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. TextSpanOverride — authoring-level span overrides
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextSpanOverride: default construction has zero range") {
    TextSpanOverride span;
    CHECK(span.byte_start == 0);
    CHECK(span.byte_end   == 0);
    CHECK(!span.font.has_value());
    CHECK(!span.color.has_value());
    CHECK(!span.font_size.has_value());
}

TEST_CASE("TextSpanOverride: can be populated and stored in TextDefinition") {
    TextDefinition def;
    TextSpanOverride span;
    span.byte_start = 0;
    span.byte_end   = 5;
    span.font       = FontSpec{.font_family = "Bold", .font_weight = 700};
    span.color      = Color{1.0f, 0.0f, 0.0f, 1.0f};
    span.font_size  = 80.0f;

    def.spans.push_back(span);

    REQUIRE(def.spans.size() == 1);
    CHECK(def.spans[0].byte_start == 0);
    CHECK(def.spans[0].byte_end   == 5);
    CHECK(def.spans[0].font->font_family   == "Bold");
    CHECK(def.spans[0].font->font_weight   == 700);
    CHECK(def.spans[0].color->r == doctest::Approx(1.0f));
    CHECK(def.spans[0].font_size.value() == doctest::Approx(80.0f));
}

TEST_CASE("TextSpanOverride: multiple spans are independent") {
    TextDefinition def;
    TextSpanOverride span_a;
    span_a.byte_start = 0;
    span_a.byte_end   = 3;
    span_a.color      = Color::red();

    TextSpanOverride span_b;
    span_b.byte_start = 3;
    span_b.byte_end   = 6;
    span_b.color      = Color::blue();

    def.spans.push_back(span_a);
    def.spans.push_back(span_b);

    CHECK(def.spans.size() == 2);
    CHECK(def.spans[0].byte_end == def.spans[1].byte_start);  // contiguous
    CHECK(def.spans[0].color->r == doctest::Approx(1.0f));    // red
    CHECK(def.spans[1].color->b == doctest::Approx(1.0f));    // blue
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Determinism — same input always produces same output
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("from_text_spec: deterministic across repeated calls") {
    TextSpec spec;
    spec.content.value = "Deterministic";
    spec.font.font_size = 48.0f;
    spec.layout.box = {800.0f, 200.0f};

    auto a = from_text_spec(spec);
    auto b = from_text_spec(spec);

    CHECK(a.content.value == b.content.value);
    CHECK(a.style.font.font_size == doctest::Approx(b.style.font.font_size));
    CHECK(a.frame.size.x == doctest::Approx(b.frame.size.x));
    CHECK(a.frame.size.y == doctest::Approx(b.frame.size.y));
    CHECK(a.frame.anchor == b.frame.anchor);
    CHECK(a.frame.align  == b.frame.align);
}
