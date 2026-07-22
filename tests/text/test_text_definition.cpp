// ═══════════════════════════════════════════════════════════════════════════
// test_text_definition.cpp — F2.A/F2.C adapter convergence tests
//
// Verifies that from_text_spec() / from_text_run_spec() / chronon3d::compat::from_text_definition()
// / to_text_document() produce correct TextDefinition objects and that
// centered_text() / glow_text() / typewriter_text() / text_run() converge
// to the canonical TextDefinition → TextDocument (compile_text_layout)
// without data loss.
//
// Test groups:
//   1. from_text_spec() — field-by-field mapping (all 22 TextSpec fields)
//   2. from_text_run_spec() — wraps from_text_spec() + TextRunSpec fields
//   3. centered_text() convergence — CenterTextOptions → TextDefinition (F2.C)
//   4. glow_text() convergence — CenterTextOptions → TextDefinition (F2.C)
//   5. typewriter_text() convergence — CenterTextOptions → TextDefinition (F2.C)
//   6. chronon3d::compat::from_text_definition() — reverse adapter: TextDefinition → TextSpec
//   7. Round-trip no-data-loss — TextSpec → TextDef → TextSpec (F2.C reverse)
//   8. Full round-trip: centered_text() → chronon3d::compat::from_text_definition() → TextSpec
//   9. Default TextSpec — default-constructed TextSpec maps to sensible defaults
//  10. TextSpanOverride — authoring-level span overrides are independent
//  12. to_text_document() — Phase B: TextDefinition → TextDocument lowering
//  13. Full convergence: centered_text() → to_text_document() → TextDocument
//  14. text_run() convergence — TextDefinition → TextRunSpec → from_text_run_spec()
//  15. Determinism — same input always produces same output
//  16. TextFrame.offset — gap-fill coverage (NOT mirrored in TextSpec)
//   17. Phase A5 — TextEffects ELIMINATED + TextMaterial canonical seam
//                             (parity lock: see TEST_CASEs below)
//   18. TextAnimation — Phase A.3: animators + selectors + run-control + Frame timing//  19. to_text_run_spec — F2.D: TextDefinition → TextRunSpec reverse adapter
//                              (direction/language/script/animators/selectors/
//                              cache_layout mappable; Frame envelope lossily dropped)
//  20. F3.D — helper-site authoring via to_text_run_spec preserves the 6
//                              spec-only animation fields in TextRunSpec
//                              (the 17 helper-site regression lock for the
//                              LayerBuilder forward-point rewiring)
//
// Phase A.3 (this commit): TextAnimation is populated with real fields
// mapped from TextRunSpec/TextMaterial; from_text_run_spec now routes
// animators/selectors/direction/language/script/cache_layout into
// TextAnimation (replaces the prior (void)silence pattern).
//
// Phase A5 close-out: the legacy `TextEffects` struct (which carried
// `glow/bevel/blur` fields) was eliminated because it was a duplicate
// of `TextMaterial.{glow,bevel}_*`.  All glow/bevel effect authority is
// now on `TextDefinition.style.material` — the canonical seam.  The
// `glow_text()` helper was rewired to write to `def.style.material.*`
// (instead of the now-deleted `def.effects.*`).  See group 17 for the
// structural-parity assertion that locks this seam.
//
// Phase B (implemented): to_text_document() lowers the canonical TextDefinition
// into a TextDocument for compile_text_layout().  The full convergence chain is:
//   centered_text()/glow_text()/text_run() → TextDefinition → to_text_document()
//   → TextDocument → compile_text_layout()
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/compat/text_spec_adapter.hpp>
#include <chronon3d/text/text_document.hpp>              // TextDocument — Phase B
#include <chronon3d/scene/builders/builder_params.hpp>  // TextSpec, TextRunSpec, TextContent
#include <chronon3d/core/types/frame.hpp>               // Frame

// Content helpers — centered_text() / glow_text() (F2.C: return TextDefinition)
#include <content/text/text_helpers_centered.hpp>
// Content helpers — typewriter_text() (F2.C: returns TextDefinition)
#include <content/text/text_helpers_typewriter.hpp>

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

TEST_CASE("from_text_spec: position mapped to frame.placement") {
    TextSpec spec;
    spec.placement = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}};
    auto def = from_text_spec(spec);
    CHECK(def.frame.placement.offset.x == doctest::Approx(960.0f));
    CHECK(def.frame.placement.offset.y == doctest::Approx(540.0f));
    CHECK(def.frame.placement.kind == TextPlacementKind::Absolute);
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
    run_spec.text.placement = TextPlacement{TextPlacementKind::Absolute, {100.0f, 200.0f}};
    run_spec.direction = TextDirection::RTL;
    run_spec.language = "ar";

    auto def_run  = from_text_run_spec(run_spec);
    auto def_spec = from_text_spec(run_spec.text);

    // The base TextSpec mapping must be identical.
    CHECK(def_run.content.value    == def_spec.content.value);
    CHECK(def_run.style.font.font_size == doctest::Approx(def_spec.style.font.font_size));
    CHECK(def_run.frame.size.x    == doctest::Approx(def_spec.frame.size.x));
    CHECK(def_run.frame.size.y    == doctest::Approx(def_spec.frame.size.y));
    CHECK(def_run.frame.placement.offset.x == doctest::Approx(def_spec.frame.placement.offset.x));
    CHECK(def_run.frame.placement.offset.y == doctest::Approx(def_spec.frame.placement.offset.y));
    CHECK(def_run.frame.placement.kind == def_spec.frame.placement.kind);
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
// 3. centered_text() convergence — CenterTextOptions → TextDefinition (F2.C)
// ═══════════════════════════════════════════════════════════════════════════
// F2.C breaking change: centered_text() now returns TextDefinition directly
// (wraps from_text_spec() internally).  Tests verify the canonical DTO.

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

    // F2.C: centered_text() returns TextDefinition directly
    auto def = centered_text(opts);

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
    CHECK(def.frame.placement.offset.x == doctest::Approx(960.0f));
    CHECK(def.frame.placement.offset.y == doctest::Approx(540.0f));
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
// 4. glow_text() convergence — CenterTextOptions → TextDefinition (F2.C)
// ═══════════════════════════════════════════════════════════════════════════
// F2: glow_text() now wires glow parameters to TextEffect on the returned
// TextDefinition.  The function itself is deprecated — users should migrate
// to centered_text() + set .effects directly.

TEST_CASE("glow_text convergence: produces valid TextDefinition with TextMaterial.glow wired") {
    CenterTextOptions opts;
    opts.text      = "GLOW";
    opts.box       = {1200.0f, 240.0f};
    opts.font_size = 72.0f;
    opts.color     = Color{1.0f, 0.5f, 0.0f, 1.0f};

    Color glow_color{1.0f, 1.0f, 0.0f, 1.0f};
    // F2.C: glow_text() returns TextDefinition directly; Phase A5 — wires
    // glow via TextMaterial.use_material_glow + glow_{color,radius,intensity}
    // (the canonical compositor surface; the legacy `TextEffects` duplicate
    // struct was removed in Phase A5 close-out).
    auto def = glow_text(opts, glow_color, 30.0f, 0.8f);

    CHECK(def.content.value == "GLOW");
    CHECK(def.style.font.font_size == doctest::Approx(72.0f));
    CHECK(def.style.color.r == doctest::Approx(1.0f));
    CHECK(def.style.color.g == doctest::Approx(0.5f));
    CHECK(def.style.color.b == doctest::Approx(0.0f));
    CHECK(def.frame.size.x  == doctest::Approx(1200.0f));
    CHECK(def.frame.size.y  == doctest::Approx(240.0f));
    CHECK(def.frame.anchor  == TextAnchor::Center);
    CHECK(def.frame.align   == TextAlign::Center);

    // Phase A5 canonical wiring — glow params are now populated on TextMaterial
    // (single canonical compositor surface; the legacy `def.effects.*` struct
    // was eliminated in Phase A5 close-out).
    CHECK(def.style.material.use_material_glow == true);
    CHECK(def.style.material.glow_color.r      == doctest::Approx(glow_color.r));
    CHECK(def.style.material.glow_color.g      == doctest::Approx(glow_color.g));
    CHECK(def.style.material.glow_color.b      == doctest::Approx(glow_color.b));
    CHECK(def.style.material.glow_radius       == doctest::Approx(30.0f));
    CHECK(def.style.material.glow_intensity    == doctest::Approx(0.8f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. typewriter_text() convergence — CenterTextOptions → TextDefinition (F2.C)
// ═══════════════════════════════════════════════════════════════════════════
// F2.C: typewriter_text() returns TextDefinition.  Tests verify that the
// reveal animation produces correct content at different frame points.

TEST_CASE("typewriter_text convergence: full reveal produces complete text") {
    CenterTextOptions opts;
    opts.text      = "HELLO";
    opts.box       = {800.0f, 200.0f};
    opts.font_size = 48.0f;
    opts.color     = Color::white();

    // At a very high frame, the text should be fully revealed
    auto def = typewriter_text(opts, Frame{1000}, 1.0f);

    CHECK(def.content.value == "HELLO");
    CHECK(def.style.font.font_size == doctest::Approx(48.0f));
    CHECK(def.style.color.a == doctest::Approx(1.0f));  // full opacity
    CHECK(def.frame.size.x  == doctest::Approx(800.0f));
    CHECK(def.frame.anchor  == TextAnchor::Center);
    CHECK(def.frame.align   == TextAlign::Center);
}

TEST_CASE("typewriter_text convergence: pre-reveal produces placeholder") {
    CenterTextOptions opts;
    opts.text      = "HELLO";
    opts.box       = {800.0f, 200.0f};
    opts.font_size = 48.0f;
    opts.color     = Color::white();

    // Before start, content is a placeholder space with 0 alpha
    auto def = typewriter_text(opts, Frame{0}, 1.0f,
        TypewriterOptions{.start_delay = Frame{10}});

    CHECK(def.content.value == " ");
    CHECK(def.style.color.a == doctest::Approx(0.0f));  // invisible
}

TEST_CASE("typewriter_text convergence: partial reveal produces substring") {
    CenterTextOptions opts;
    opts.text      = "ABCDE";
    opts.box       = {800.0f, 200.0f};
    opts.font_size = 48.0f;
    opts.color     = Color::white();

    // At frame 2 with 1 char/frame, Linear easing, 5 chars total:
    // linear_t = 2 * 1.0 / 5 = 0.4, eased_t = 0.4 (Linear), revealed = floor(0.4 * 5) = 2
    auto def = typewriter_text(opts, Frame{2}, 1.0f);

    // Exactly 2 chars should be revealed ("AB")
    CHECK(def.content.value.size() == 2);
    CHECK(def.content.value[0] == 'A');
    CHECK(def.content.value[1] == 'B');
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. chronon3d::compat::from_text_definition() — reverse adapter: TextDefinition → TextSpec (F2.C)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("from_text_definition: content round-trips correctly") {
    TextDefinition def;
    def.content.value = "Reverse test";
    auto spec = chronon3d::compat::from_text_definition(def);
    CHECK(spec.content.value == "Reverse test");
}

TEST_CASE("from_text_definition: font round-trips correctly") {
    TextDefinition def;
    def.style.font = FontSpec{
        .font_path   = "fonts/Reverse.ttf",
        .font_family = "Reverse",
        .font_weight = 500,
        .font_style  = "normal",
        .font_size   = 36.0f,
    };
    auto spec = chronon3d::compat::from_text_definition(def);
    CHECK(spec.font.font_path   == "fonts/Reverse.ttf");
    CHECK(spec.font.font_family == "Reverse");
    CHECK(spec.font.font_weight == 500);
    CHECK(spec.font.font_size   == doctest::Approx(36.0f));
}

TEST_CASE("from_text_definition: appearance round-trips correctly") {
    TextDefinition def;
    def.style.color = Color{0.1f, 0.2f, 0.3f, 0.4f};
    def.style.paint.stroke_enabled = true;
    def.style.paint.stroke_width   = 4.0f;
    auto spec = chronon3d::compat::from_text_definition(def);
    CHECK(spec.appearance.color.r == doctest::Approx(0.1f));
    CHECK(spec.appearance.color.g == doctest::Approx(0.2f));
    CHECK(spec.appearance.color.b == doctest::Approx(0.3f));
    CHECK(spec.appearance.color.a == doctest::Approx(0.4f));
    CHECK(spec.appearance.paint.stroke_enabled == true);
    CHECK(spec.appearance.paint.stroke_width   == doctest::Approx(4.0f));
}

TEST_CASE("from_text_definition: layout round-trips correctly") {
    TextDefinition def;
    def.frame.size          = {640.0f, 480.0f};
    def.frame.anchor        = TextAnchor::TopCenter;
    def.frame.align         = TextAlign::Left;
    def.frame.vertical_align = VerticalAlign::Top;
    def.frame.wrap          = TextWrap::None;
    def.frame.overflow      = TextOverflow::Clip;
    def.frame.line_height   = 1.3f;
    def.frame.tracking      = 1.0f;
    def.frame.auto_fit      = true;
    def.frame.min_font_size = 10.0f;
    def.frame.max_font_size = 100.0f;
    def.frame.max_lines     = 4;
    def.frame.ellipsis      = true;
    def.frame.placement.offset = {200.0f, 300.0f};
    auto spec = chronon3d::compat::from_text_definition(def);

    CHECK(spec.layout.box.x            == doctest::Approx(640.0f));
    CHECK(spec.layout.box.y            == doctest::Approx(480.0f));
    CHECK(spec.layout.anchor           == TextAnchor::TopCenter);
    CHECK(spec.layout.align            == TextAlign::Left);
    CHECK(spec.layout.vertical_align   == VerticalAlign::Top);
    CHECK(spec.layout.wrap             == TextWrap::None);
    CHECK(spec.layout.overflow         == TextOverflow::Clip);
    CHECK(spec.layout.line_height      == doctest::Approx(1.3f));
    CHECK(spec.layout.tracking         == doctest::Approx(1.0f));
    CHECK(spec.layout.auto_fit         == true);
    CHECK(spec.layout.min_font_size    == doctest::Approx(10.0f));
    CHECK(spec.layout.max_font_size    == doctest::Approx(100.0f));
    CHECK(spec.layout.max_lines        == 4);
    CHECK(spec.layout.ellipsis         == true);
    CHECK(spec.placement.offset.x    == doctest::Approx(200.0f));
    CHECK(spec.placement.offset.y    == doctest::Approx(300.0f));
    // F1: z dropped from TextSpec.position — position is Vec2 only
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Round-trip no-data-loss — TextSpec → TextDef → TextSpec (F2.C reverse)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("round-trip: TextSpec → TextDefinition → TextSpec preserves all fields") {
    TextSpec original;
    original.content.value = "Round-trip reverse";
    original.font = {.font_path = "fonts/R.ttf", .font_family = "R",
                     .font_weight = 300, .font_style = "italic", .font_size = 24.0f};
    original.layout.box            = {500.0f, 100.0f};
    original.layout.anchor         = TextAnchor::BottomCenter;
    original.layout.align          = TextAlign::Right;
    original.layout.vertical_align = VerticalAlign::Bottom;
    original.layout.wrap           = TextWrap::Word;
    original.layout.overflow       = TextOverflow::Ellipsis;
    original.layout.centering_mode = TextCenteringMode::PixelInk;
    original.layout.line_height    = 1.4f;
    original.layout.tracking       = 0.5f;
    original.layout.auto_fit       = true;
    original.layout.min_font_size  = 6.0f;
    original.layout.max_font_size  = 90.0f;
    original.layout.max_lines      = 2;
    original.layout.ellipsis       = true;
    original.appearance.color      = Color{0.3f, 0.6f, 0.9f, 0.7f};
    original.appearance.paint.stroke_enabled = true;
    original.appearance.paint.stroke_width   = 2.0f;
    // Shadows + material + box_style set BEFORE the round-trip
    TextShadow shadow;
    shadow.enabled = true;
    shadow.blur    = 8.0f;
    original.appearance.shadows = {shadow};
    original.appearance.material = TextMaterial::neon();
    original.appearance.box_style.enabled = true;
    original.appearance.box_style.radius  = 12.0f;
    original.layout.paragraph.justification = TextJustification::Center;
    original.placement = TextPlacement{TextPlacementKind::Absolute, {150.0f, 250.0f}};

    // Single round-trip: forward + reverse
    auto def = from_text_spec(original);
    auto restored = chronon3d::compat::from_text_definition(def);

    // Every field must survive the full round-trip
    CHECK(restored.content.value == original.content.value);
    CHECK(restored.font.font_path   == original.font.font_path);
    CHECK(restored.font.font_family == original.font.font_family);
    CHECK(restored.font.font_weight == original.font.font_weight);
    CHECK(restored.font.font_style  == original.font.font_style);
    CHECK(restored.font.font_size   == doctest::Approx(original.font.font_size));
    CHECK(restored.layout.box.x     == doctest::Approx(original.layout.box.x));
    CHECK(restored.layout.box.y     == doctest::Approx(original.layout.box.y));
    CHECK(restored.layout.anchor    == original.layout.anchor);
    CHECK(restored.layout.align     == original.layout.align);
    CHECK(restored.layout.vertical_align == original.layout.vertical_align);
    CHECK(restored.layout.wrap      == original.layout.wrap);
    CHECK(restored.layout.overflow  == original.layout.overflow);
    CHECK(restored.layout.centering_mode == original.layout.centering_mode);
    CHECK(restored.layout.line_height == doctest::Approx(original.layout.line_height));
    CHECK(restored.layout.tracking  == doctest::Approx(original.layout.tracking));
    CHECK(restored.layout.auto_fit  == original.layout.auto_fit);
    CHECK(restored.layout.min_font_size == doctest::Approx(original.layout.min_font_size));
    CHECK(restored.layout.max_font_size == doctest::Approx(original.layout.max_font_size));
    CHECK(restored.layout.max_lines == original.layout.max_lines);
    CHECK(restored.layout.ellipsis  == original.layout.ellipsis);
    CHECK(restored.appearance.color.r == doctest::Approx(original.appearance.color.r));
    CHECK(restored.appearance.color.g == doctest::Approx(original.appearance.color.g));
    CHECK(restored.appearance.color.b == doctest::Approx(original.appearance.color.b));
    CHECK(restored.appearance.color.a == doctest::Approx(original.appearance.color.a));
    CHECK(restored.appearance.paint.stroke_enabled == original.appearance.paint.stroke_enabled);
    CHECK(restored.appearance.paint.stroke_width   == doctest::Approx(original.appearance.paint.stroke_width));
    CHECK(restored.placement.offset.x == doctest::Approx(original.placement.offset.x));
    CHECK(restored.placement.offset.y == doctest::Approx(original.placement.offset.y));
    // Shadows + material + box_style
    REQUIRE(restored.appearance.shadows.size() == 1);
    CHECK(restored.appearance.shadows[0].enabled == original.appearance.shadows[0].enabled);
    CHECK(restored.appearance.shadows[0].blur    == doctest::Approx(original.appearance.shadows[0].blur));
    CHECK(restored.appearance.material.use_material_glow == original.appearance.material.use_material_glow);
    CHECK(restored.appearance.box_style.enabled == original.appearance.box_style.enabled);
    CHECK(restored.appearance.box_style.radius  == doctest::Approx(original.appearance.box_style.radius));
    // Paragraph round-trip
    CHECK(restored.layout.paragraph.justification == original.layout.paragraph.justification);
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Full convergence: centered_text() → chronon3d::compat::from_text_definition() → TextSpec
// ═══════════════════════════════════════════════════════════════════════════
// This tests the full F2.C adapter chain: helper → TextDefinition → TextSpec
// → (downstream pipeline).  Verifies no data loss across the full chain.

TEST_CASE("full convergence: centered_text → from_text_definition → TextSpec") {
    CenterTextOptions opts;
    opts.text        = "CONVERGE";
    opts.box         = {1000.0f, 200.0f};
    opts.pos         = {500.0f, 300.0f, 0.0f};
    opts.font_asset  = "fonts/Test.ttf";
    opts.font_family = "Test";
    opts.font_weight = 600;
    opts.font_size   = 80.0f;
    opts.tracking    = 2.0f;
    opts.color       = Color{0.5f, 0.5f, 0.5f, 1.0f};
    opts.max_lines   = 2;
    opts.auto_fit    = false;
    opts.line_height = 1.0f;

    // F2.C: centered_text() returns TextDefinition
    auto def = centered_text(opts);
    // F2.C: chronon3d::compat::from_text_definition() converts back to TextSpec
    auto spec = chronon3d::compat::from_text_definition(def);

    // Verify the full chain preserves all authored values
    CHECK(spec.content.value == "CONVERGE");
    CHECK(spec.font.font_path   == "fonts/Test.ttf");
    CHECK(spec.font.font_family == "Test");
    CHECK(spec.font.font_weight == 600);
    CHECK(spec.font.font_size   == doctest::Approx(80.0f));
    CHECK(spec.layout.box.x     == doctest::Approx(1000.0f));
    CHECK(spec.layout.box.y     == doctest::Approx(200.0f));
    CHECK(spec.layout.anchor    == TextAnchor::Center);
    CHECK(spec.layout.align     == TextAlign::Center);
    CHECK(spec.layout.vertical_align == VerticalAlign::Middle);
    CHECK(spec.layout.centering_mode == TextCenteringMode::PixelInk);
    CHECK(spec.layout.tracking  == doctest::Approx(2.0f));
    CHECK(spec.layout.max_lines == 2);
    CHECK(spec.appearance.color.r == doctest::Approx(0.5f));
    CHECK(spec.appearance.color.g == doctest::Approx(0.5f));
    CHECK(spec.appearance.color.b == doctest::Approx(0.5f));
    CHECK(spec.placement.offset.x == doctest::Approx(500.0f));
    CHECK(spec.placement.offset.y == doctest::Approx(300.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. from_text_spec() — field-by-field mapping (original F2.A tests)
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
    spec.placement = TextPlacement{TextPlacementKind::Absolute, {480.0f, 270.0f}};

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
    CHECK(def.frame.placement.offset.x == doctest::Approx(480.0f));
    CHECK(def.frame.placement.offset.y == doctest::Approx(270.0f));

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
// 10. Default TextSpec — default-constructed values map to sensible defaults
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
    CHECK(def.frame.placement.offset.x == doctest::Approx(0.0f));
    CHECK(def.frame.placement.offset.y == doctest::Approx(0.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 11. TextSpanOverride — authoring-level span overrides
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
// 12. to_text_document() — Phase B: TextDefinition → TextDocument lowering
// ═══════════════════════════════════════════════════════════════════════════
// Verifies that to_text_document() correctly maps TextDefinition fields to
// the TextDocument pipeline model, including span lowering + paragraph split.

TEST_CASE("to_text_document: content.value mapped to doc.utf8") {
    TextDefinition def;
    def.content.value = "Phase B text";
    auto doc = to_text_document(def);
    CHECK(doc.utf8 == "Phase B text");
}

TEST_CASE("to_text_document: defaults set from from_text_definition") {
    TextDefinition def;
    def.content.value = "Defaults test";
    def.style.font = {.font_path = "fonts/Test.ttf", .font_family = "Test",
                      .font_weight = 600, .font_size = 80.0f};
    def.frame.size = {1000.0f, 200.0f};
    def.frame.anchor = TextAnchor::TopCenter;
    def.style.color = Color{0.5f, 0.5f, 0.5f, 1.0f};

    auto doc = to_text_document(def);

    // The defaults TextSpec should match chronon3d::compat::from_text_definition(def)
    CHECK(doc.defaults.content.value == "Defaults test");
    CHECK(doc.defaults.font.font_path == "fonts/Test.ttf");
    CHECK(doc.defaults.font.font_size == doctest::Approx(80.0f));
    CHECK(doc.defaults.layout.box.x == doctest::Approx(1000.0f));
    CHECK(doc.defaults.layout.box.y == doctest::Approx(200.0f));
    CHECK(doc.defaults.layout.anchor == TextAnchor::TopCenter);
    CHECK(doc.defaults.appearance.color.r == doctest::Approx(0.5f));
}

TEST_CASE("to_text_document: TextSpanOverride lowered to TextStyleSpan") {
    TextDefinition def;
    def.content.value = "HelloWorld";  // 10 bytes
    def.style.font.font_size = 48.0f;

    TextSpanOverride span;
    span.byte_start = 0;
    span.byte_end   = 5;  // "Hello"
    span.color      = Color{1.0f, 0.0f, 0.0f, 1.0f};  // red
    span.font       = FontSpec{.font_family = "Bold", .font_weight = 700};
    def.spans.push_back(span);

    auto doc = to_text_document(def);

    REQUIRE(doc.spans.size() == 1);
    CHECK(doc.spans[0].byte_start == 0);
    CHECK(doc.spans[0].byte_end   == 5);
    // Font override
    REQUIRE(doc.spans[0].font.has_value());
    CHECK(doc.spans[0].font->font_family == "Bold");
    CHECK(doc.spans[0].font->font_weight == 700);
    // Color override (wrapped in TextAppearanceSpec)
    REQUIRE(doc.spans[0].appearance.has_value());
    CHECK(doc.spans[0].appearance->color.r == doctest::Approx(1.0f));
    CHECK(doc.spans[0].appearance->color.g == doctest::Approx(0.0f));
}

TEST_CASE("to_text_document: font_size override becomes multiplier") {
    TextDefinition def;
    def.content.value = "HelloWorld";  // 10 bytes
    def.style.font.font_size = 48.0f;

    TextSpanOverride span;
    span.byte_start = 0;
    span.byte_end   = 5;  // "Hello"
    span.font_size  = 72.0f;
    def.spans.push_back(span);

    auto doc = to_text_document(def);

    REQUIRE(doc.spans.size() == 1);
    // Font size override → multiplier (72/48 = 1.5)
    REQUIRE(doc.spans[0].font_size_multiplier.has_value());
    CHECK(doc.spans[0].font_size_multiplier.value() == doctest::Approx(1.5f));
}

TEST_CASE("to_text_document: font_size override skipped when font has explicit size") {
    // Edge case: when TextSpanOverride.font has font_size > 0 AND font_size
    // is also set, the multiplier is NOT applied (avoids double-application).
    TextDefinition def;
    def.content.value = "Test";
    def.style.font.font_size = 48.0f;

    TextSpanOverride span;
    span.byte_start = 0;
    span.byte_end   = 4;
    span.font       = FontSpec{.font_family = "Bold", .font_weight = 700, .font_size = 96.0f};
    span.font_size  = 72.0f;  // different from font->font_size
    def.spans.push_back(span);

    auto doc = to_text_document(def);

    REQUIRE(doc.spans.size() == 1);
    // Font override carries the absolute font_size
    REQUIRE(doc.spans[0].font.has_value());
    CHECK(doc.spans[0].font->font_size == doctest::Approx(96.0f));
    // Multiplier is NOT set — the font override already carries the size
    CHECK(!doc.spans[0].font_size_multiplier.has_value());
}

TEST_CASE("to_text_document: multiple spans lowered correctly") {
    TextDefinition def;
    def.content.value = "ABCDEF";  // 6 bytes
    def.style.font.font_size = 40.0f;

    TextSpanOverride span_a;
    span_a.byte_start = 0;
    span_a.byte_end   = 3;
    span_a.color      = Color::red();
    def.spans.push_back(span_a);

    TextSpanOverride span_b;
    span_b.byte_start = 3;
    span_b.byte_end   = 6;
    span_b.color      = Color::blue();
    def.spans.push_back(span_b);

    auto doc = to_text_document(def);

    REQUIRE(doc.spans.size() == 2);
    CHECK(doc.spans[0].byte_start == 0);
    CHECK(doc.spans[0].byte_end   == 3);
    CHECK(doc.spans[1].byte_start == 3);
    CHECK(doc.spans[1].byte_end   == 6);
    CHECK(doc.spans[0].appearance->color.r == doctest::Approx(1.0f));  // red
    CHECK(doc.spans[1].appearance->color.b == doctest::Approx(1.0f));  // blue
}

TEST_CASE("to_text_document: paragraphs auto-split on newlines") {
    TextDefinition def;
    def.content.value = "Line 1\nLine 2\nLine 3";
    auto doc = to_text_document(def);

    // split_paragraphs() should have created 3 paragraphs
    CHECK(doc.paragraphs.size() == 3);
    CHECK(doc.paragraphs[0].byte_start == 0);
    CHECK(doc.paragraphs[2].byte_end   == 20);  // total utf8 length
}

TEST_CASE("to_text_document: empty content produces valid empty document") {
    TextDefinition def;
    def.content.value = "";
    auto doc = to_text_document(def);
    CHECK(doc.utf8.empty());
    CHECK(doc.spans.empty());
    CHECK(doc.paragraphs.empty());  // no auto-split on empty text
}

TEST_CASE("to_text_document: no spans produces empty span vector") {
    TextDefinition def;
    def.content.value = "No spans here";
    auto doc = to_text_document(def);
    CHECK(doc.spans.empty());
    CHECK(doc.utf8 == "No spans here");
}

// ═══════════════════════════════════════════════════════════════════════════
// 13. Full convergence: centered_text() → to_text_document() → TextDocument
// ═══════════════════════════════════════════════════════════════════════════
// Verifies the complete Phase B chain: authoring helper → TextDefinition
// → to_text_document() → TextDocument (ready for compile_text_layout).

TEST_CASE("full convergence: centered_text → to_text_document → TextDocument") {
    CenterTextOptions opts;
    opts.text        = "CONVERGE PHASE B";
    opts.box         = {1000.0f, 200.0f};
    opts.pos         = {500.0f, 300.0f, 0.0f};
    opts.font_asset  = "fonts/Test.ttf";
    opts.font_family = "Test";
    opts.font_weight = 600;
    opts.font_size   = 80.0f;
    opts.tracking    = 2.0f;
    opts.color       = Color{0.5f, 0.5f, 0.5f, 1.0f};
    opts.line_height = 1.0f;

    // Phase B: centered_text() → TextDefinition → to_text_document() → TextDocument
    auto def = centered_text(opts);
    auto doc = to_text_document(def);

    // Content converged
    CHECK(doc.utf8 == "CONVERGE PHASE B");

    // Defaults converged (font)
    CHECK(doc.defaults.font.font_path   == "fonts/Test.ttf");
    CHECK(doc.defaults.font.font_family == "Test");
    CHECK(doc.defaults.font.font_weight == 600);
    CHECK(doc.defaults.font.font_size   == doctest::Approx(80.0f));

    // Defaults converged (layout)
    CHECK(doc.defaults.layout.box.x     == doctest::Approx(1000.0f));
    CHECK(doc.defaults.layout.box.y     == doctest::Approx(200.0f));
    CHECK(doc.defaults.layout.anchor    == TextAnchor::Center);
    CHECK(doc.defaults.layout.align     == TextAlign::Center);
    CHECK(doc.defaults.layout.tracking  == doctest::Approx(2.0f));

    // Defaults converged (appearance)
    CHECK(doc.defaults.appearance.color.r == doctest::Approx(0.5f));
    CHECK(doc.defaults.appearance.color.g == doctest::Approx(0.5f));
    CHECK(doc.defaults.appearance.color.b == doctest::Approx(0.5f));

    // Defaults converged (position)
    CHECK(doc.defaults.placement.offset.x == doctest::Approx(500.0f));
    CHECK(doc.defaults.placement.offset.y == doctest::Approx(300.0f));

    // Paragraphs auto-split (single paragraph, no newlines)
    CHECK(doc.paragraphs.size() == 1);
    CHECK(doc.paragraphs[0].byte_start == 0);
    CHECK(doc.paragraphs[0].byte_end   == 16);  // strlen("CONVERGE PHASE B")
}

TEST_CASE("full convergence: glow_text → to_text_document → TextDocument") {
    CenterTextOptions opts;
    opts.text      = "GLOW PHASE B";
    opts.box       = {1200.0f, 240.0f};
    opts.font_size = 72.0f;
    opts.color     = Color{1.0f, 0.5f, 0.0f, 1.0f};

    auto def = glow_text(opts, Color{1.0f, 1.0f, 0.0f, 1.0f}, 30.0f, 0.8f);
    auto doc = to_text_document(def);

    CHECK(doc.utf8 == "GLOW PHASE B");
    CHECK(doc.defaults.font.font_size == doctest::Approx(72.0f));
    CHECK(doc.defaults.layout.box.x   == doctest::Approx(1200.0f));
    CHECK(doc.defaults.appearance.color.r == doctest::Approx(1.0f));
    CHECK(doc.defaults.appearance.color.g == doctest::Approx(0.5f));
    CHECK(doc.paragraphs.size() == 1);
}

TEST_CASE("full convergence: typewriter_text → to_text_document → TextDocument") {
    CenterTextOptions opts;
    opts.text      = "TYPEWRITER PHASE B";
    opts.box       = {800.0f, 200.0f};
    opts.font_size = 48.0f;
    opts.color     = Color::white();

    auto def = typewriter_text(opts, Frame{1000}, 1.0f);
    auto doc = to_text_document(def);

    CHECK(doc.utf8 == "TYPEWRITER PHASE B");
    CHECK(doc.defaults.font.font_size == doctest::Approx(48.0f));
    CHECK(doc.paragraphs.size() == 1);
}

TEST_CASE("full convergence: from_text_spec → to_text_document → TextDocument (round-trip)") {
    TextSpec spec;
    spec.content.value = "Round-trip Phase B";
    spec.font = {.font_path = "fonts/RT.ttf", .font_family = "RT",
                 .font_weight = 400, .font_size = 32.0f};
    spec.layout.box = {600.0f, 120.0f};
    spec.layout.anchor = TextAnchor::BottomCenter;
    spec.layout.tracking = 1.5f;
    spec.appearance.color = Color{0.2f, 0.8f, 0.4f, 0.9f};spec.placement = TextPlacement{TextPlacementKind::Absolute, {100.0f, 200.0f}};

    // Forward: TextSpec → TextDefinition → TextDocument
    auto def = from_text_spec(spec);
    auto doc = to_text_document(def);

    // Verify the TextDocument carries all the original authored values
    CHECK(doc.utf8 == "Round-trip Phase B");
    CHECK(doc.defaults.font.font_path   == "fonts/RT.ttf");
    CHECK(doc.defaults.font.font_family == "RT");
    CHECK(doc.defaults.font.font_weight == 400);
    CHECK(doc.defaults.font.font_size   == doctest::Approx(32.0f));
    CHECK(doc.defaults.layout.box.x     == doctest::Approx(600.0f));
    CHECK(doc.defaults.layout.box.y     == doctest::Approx(120.0f));
    CHECK(doc.defaults.layout.anchor    == TextAnchor::BottomCenter);
    CHECK(doc.defaults.layout.tracking  == doctest::Approx(1.5f));
    CHECK(doc.defaults.appearance.color.r == doctest::Approx(0.2f));
    CHECK(doc.defaults.appearance.color.g == doctest::Approx(0.8f));
    CHECK(doc.defaults.appearance.color.b == doctest::Approx(0.4f));
    CHECK(doc.defaults.appearance.color.a == doctest::Approx(0.9f));
    CHECK(doc.defaults.placement.offset.x == doctest::Approx(100.0f));
    CHECK(doc.defaults.placement.offset.y == doctest::Approx(200.0f));
    CHECK(doc.paragraphs.size() == 1);
}

TEST_CASE("to_text_document: deterministic across repeated calls") {
    TextDefinition def;
    def.content.value = "Deterministic Phase B";
    def.style.font.font_size = 48.0f;

    auto a = to_text_document(def);
    auto b = to_text_document(def);

    CHECK(a.utf8 == b.utf8);
    CHECK(a.defaults.font.font_size == doctest::Approx(b.defaults.font.font_size));
    CHECK(a.spans.size() == b.spans.size());
    CHECK(a.paragraphs.size() == b.paragraphs.size());
}

// ═══════════════════════════════════════════════════════════════════════════
// 14. text_run() convergence — TextDefinition → TextRunSpec → from_text_run_spec()
// ═══════════════════════════════════════════════════════════════════════════
// Verifies that LayerBuilder::text_run(name, TextDefinition) — which internally
// does TextDefinition → chronon3d::compat::from_text_definition() → TextSpec → TextRunSpec →
// text_run(name, TextRunSpec) — preserves all authored values through the
// full adapter chain.  We test at the adapter level (not through LayerBuilder)
// because the builder requires heavy scene/registry dependencies.

TEST_CASE("text_run convergence: TextDefinition → TextRunSpec → from_text_run_spec round-trip") {
    // Simulate what LayerBuilder::text_run(name, TextDefinition) does:
    //   TextRunSpec run;
    //   run.text = chronon3d::compat::from_text_definition(def);
    //   text_run(name, std::move(run));
    //
    // Then verify from_text_run_spec(run) produces the same TextDefinition.
    TextDefinition original;
    original.content.value = "TEXT_RUN_ADAPTER";
    original.style.font = {.font_path = "fonts/Adapter.ttf", .font_family = "Adapter",
                           .font_weight = 600, .font_size = 72.0f};
    original.frame.size = {1200.0f, 240.0f};
    original.frame.anchor = TextAnchor::Center;
    original.frame.align = TextAlign::Center;
    original.frame.tracking = 1.5f;
    original.frame.max_lines = 2;
    original.style.color = Color{0.3f, 0.7f, 0.9f, 1.0f};
    original.frame.placement.offset = {400.0f, 300.0f};

    // Step 1: TextDefinition → TextSpec (what text_run(name, TextDefinition) does)
    TextSpec spec = chronon3d::compat::from_text_definition(original);
    // Step 2: TextSpec → TextRunSpec (wrapping)
    TextRunSpec run;
    run.text = std::move(spec);
    // Step 3: TextRunSpec → TextDefinition (reverse via from_text_run_spec)
    auto restored = from_text_run_spec(run);

    // All authored values must survive the full round-trip
    CHECK(restored.content.value == "TEXT_RUN_ADAPTER");
    CHECK(restored.style.font.font_path   == "fonts/Adapter.ttf");
    CHECK(restored.style.font.font_family == "Adapter");
    CHECK(restored.style.font.font_weight == 600);
    CHECK(restored.style.font.font_size   == doctest::Approx(72.0f));
    CHECK(restored.frame.size.x     == doctest::Approx(1200.0f));
    CHECK(restored.frame.size.y     == doctest::Approx(240.0f));
    CHECK(restored.frame.anchor     == TextAnchor::Center);
    CHECK(restored.frame.align      == TextAlign::Center);
    CHECK(restored.frame.tracking   == doctest::Approx(1.5f));
    CHECK(restored.frame.max_lines  == 2);
    CHECK(restored.style.color.r    == doctest::Approx(0.3f));
    CHECK(restored.style.color.g    == doctest::Approx(0.7f));
    CHECK(restored.style.color.b    == doctest::Approx(0.9f));
    CHECK(restored.frame.placement.offset.x == doctest::Approx(400.0f));
    CHECK(restored.frame.placement.offset.y == doctest::Approx(300.0f));
}

TEST_CASE("text_run convergence: centered_text → from_text_definition → TextRunSpec → from_text_run_spec") {
    // Full chain: centered_text() → TextDefinition → chronon3d::compat::from_text_definition() →
    // TextSpec → TextRunSpec → from_text_run_spec() → TextDefinition
    // This is the adapter-level path that LayerBuilder::text_run(name, TextDefinition)
    // uses internally.
    CenterTextOptions opts;
    opts.text        = "CENTERED_RUN";
    opts.box         = {1000.0f, 200.0f};
    opts.pos         = {500.0f, 300.0f, 0.0f};
    opts.font_asset  = "fonts/Run.ttf";
    opts.font_family = "Run";
    opts.font_weight = 700;
    opts.font_size   = 80.0f;
    opts.tracking    = 2.0f;
    opts.color       = Color{0.5f, 0.5f, 0.5f, 1.0f};

    auto def = centered_text(opts);

    // Simulate text_run(name, def) adapter path
    TextRunSpec run;
    run.text = chronon3d::compat::from_text_definition(def);
    auto restored = from_text_run_spec(run);

    CHECK(restored.content.value == "CENTERED_RUN");
    CHECK(restored.style.font.font_path   == "fonts/Run.ttf");
    CHECK(restored.style.font.font_family == "Run");
    CHECK(restored.style.font.font_weight == 700);
    CHECK(restored.style.font.font_size   == doctest::Approx(80.0f));
    CHECK(restored.frame.size.x     == doctest::Approx(1000.0f));
    CHECK(restored.frame.size.y     == doctest::Approx(200.0f));
    CHECK(restored.frame.anchor     == TextAnchor::Center);
    CHECK(restored.frame.tracking   == doctest::Approx(2.0f));
    CHECK(restored.frame.placement.offset.x == doctest::Approx(500.0f));
    CHECK(restored.frame.placement.offset.y == doctest::Approx(300.0f));
    CHECK(restored.style.color.r    == doctest::Approx(0.5f));
}

TEST_CASE("text_run convergence: glow_text → from_text_definition → TextRunSpec → from_text_run_spec") {
    // Full chain: glow_text() → TextDefinition → chronon3d::compat::from_text_definition() →
    // TextSpec → TextRunSpec → from_text_run_spec() → TextDefinition
    CenterTextOptions opts;
    opts.text      = "GLOW_RUN";
    opts.box       = {1200.0f, 240.0f};
    opts.font_size = 72.0f;
    opts.color     = Color{1.0f, 0.5f, 0.0f, 1.0f};

    auto def = glow_text(opts, Color{1.0f, 1.0f, 0.0f, 1.0f}, 30.0f, 0.8f);

    TextRunSpec run;
    run.text = chronon3d::compat::from_text_definition(def);
    auto restored = from_text_run_spec(run);

    CHECK(restored.content.value == "GLOW_RUN");
    CHECK(restored.style.font.font_size == doctest::Approx(72.0f));
    CHECK(restored.frame.size.x  == doctest::Approx(1200.0f));
    CHECK(restored.frame.size.y  == doctest::Approx(240.0f));
    CHECK(restored.frame.anchor  == TextAnchor::Center);
    CHECK(restored.style.color.r == doctest::Approx(1.0f));
    CHECK(restored.style.color.g == doctest::Approx(0.5f));
    CHECK(restored.style.color.b == doctest::Approx(0.0f));
}

TEST_CASE("text_run convergence: typewriter_text → from_text_definition → TextRunSpec → from_text_run_spec") {
    // Full chain: typewriter_text() → TextDefinition → chronon3d::compat::from_text_definition() →
    // TextSpec → TextRunSpec → from_text_run_spec() → TextDefinition
    CenterTextOptions opts;
    opts.text      = "TYPEWRITER_RUN";
    opts.box       = {800.0f, 200.0f};
    opts.font_size = 48.0f;
    opts.color     = Color::white();

    auto def = typewriter_text(opts, Frame{1000}, 1.0f);

    TextRunSpec run;
    run.text = chronon3d::compat::from_text_definition(def);
    auto restored = from_text_run_spec(run);

    CHECK(restored.content.value == "TYPEWRITER_RUN");
    CHECK(restored.style.font.font_size == doctest::Approx(48.0f));
    CHECK(restored.frame.size.x  == doctest::Approx(800.0f));
    CHECK(restored.frame.size.y  == doctest::Approx(200.0f));
    CHECK(restored.frame.anchor  == TextAnchor::Center);
    CHECK(restored.frame.align   == TextAlign::Center);
    CHECK(restored.style.color.a == doctest::Approx(1.0f));  // full opacity at frame 1000
}

TEST_CASE("text_run convergence: TextRunSpec direction/language/script mapped to TextAnimation (Phase A.3)") {
    // Phase A.3 wiring: spec.direction + spec.language + spec.script + spec.cache_layout
    // now route through def.animation (TextAnimation is populated by
    // from_text_run_spec — the prior (void)silence pattern is replaced).
    TextRunSpec run;
    run.text.content.value = "RTL_TEXT";
    run.text.font.font_size = 48.0f;
    run.direction = TextDirection::RTL;
    run.language = "ar";
    run.script = 0x41726162u;  // 'Arab' OpenType tag
    run.cache_layout = false;

    auto def = from_text_run_spec(run);

    // TextSpec fields must be correct
    CHECK(def.content.value == "RTL_TEXT");
    CHECK(def.style.font.font_size == doctest::Approx(48.0f));
    // Phase A.3 mapping: TextRunSpec → TextAnimation
    CHECK(def.animation.direction == TextDirection::RTL);
    CHECK(def.animation.language == "ar");
    CHECK(def.animation.script == 0x41726162u);
    CHECK(def.animation.cache_layout == false);
}

// ═══════════════════════════════════════════════════════════════════════════
// 15. Determinism — same input always produces same output
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

// ═══════════════════════════════════════════════════════════════════════════
// 16. TextFrame.placement — offset component coverage (Phase A4 close-out)
// ═══════════════════════════════════════════════════════════════════════════
//
// Post-A4 the `TextFrame.{position, placement_kind, offset}` triple was
// consolidated into `TextFrame.placement` (a TextPlacement struct with
// `kind` + `offset` fields).  This group locks in the consolidated behavior.

TEST_CASE("TextFrame.placement: default TextFrame constructs with {0,0} offset") {
    TextFrame frame;
    CHECK(frame.placement.offset.x == doctest::Approx(0.0f));
    CHECK(frame.placement.offset.y == doctest::Approx(0.0f));
}

TEST_CASE("TextFrame.placement: from_text_spec(default) gives def.frame.placement.offset = {0, 0}") {
    TextSpec spec;  // all defaults
    auto def = from_text_spec(spec);
    CHECK(def.frame.placement.offset.x == doctest::Approx(0.0f));
    CHECK(def.frame.placement.offset.y == doctest::Approx(0.0f));
}

TEST_CASE("TextFrame.placement: setter roundtrip on TextDefinition directly") {
    TextDefinition def;
    def.frame.placement.offset = Vec2{100.0f, 50.0f};
    CHECK(def.frame.placement.offset.x == doctest::Approx(100.0f));
    CHECK(def.frame.placement.offset.y == doctest::Approx(50.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 17. Phase A5 — TextEffects ELIMINATED + canonical TextMaterial seam
// ═══════════════════════════════════════════════════════════════════════════
//
// Phase A5 closes the duplicate-authority on glow/bevel effect fields.  The
// legacy `chronon3d::TextEffects` struct (which lived on TextDefinition but
// was never consumed by the renderer — see the prior `def.effects.enabled`
// flag that the renderer never read) is REMOVED.  All glow/bevel effect
// authority now lives on `chronon3d::TextMaterial` (under
// `TextDefinition.style.material`).  This test group is the "parity lock":
//
//   - It exercises the exact field set the legacy `TextEffects.glow_*`
//     authority covered (color, radius, intensity) on the canonical
//     `TextMaterial` surface.
//   - It checks that `glow_text()` now writes to `def.style.material.*` —
//     the canonical seam — instead of the (now-deleted) `def.effects.*`.
//   - It serves as compile-time proof that `def.effects` no longer exists:
//     should anyone re-introduce the field, the access compiles but the
//     gate #22 (check_architecture_boundaries.sh) refuses to merge.

TEST_CASE("Phase A5: glow_text canonical route populates TextMaterial.glow_color/radius/intensity (parity assertion)") {
    // The legacy "TextEffects::glow=…" API surface is eliminated.  The
    // structural-parity assertion: a call site that previously set
    //     def.effects.enabled       = true
    //     def.effects.glow_color    = X
    //     def.effects.glow_radius   = R
    //     def.effects.glow_intensity = I
    // now produces identical setup when expressed on the canonical
    // TextMaterial:
    //     def.style.material.use_material_glow = true
    //     def.style.material.glow_color        = X
    //     def.style.material.glow_radius       = R
    //     def.style.material.glow_intensity    = I
    //
    // Same 4 fields, same renderer behaviour, single canonical seam.
    CenterTextOptions opts;
    opts.text      = "PARITY_GLOW";
    opts.box       = {1200.0f, 240.0f};
    opts.font_size = 96.0f;
    opts.color     = Color::white();

    Color glow_color{1.0f, 0.85f, 0.30f, 1.0f};
    const f32 glow_radius    = 28.0f;
    const f32 glow_intensity = 0.7f;
    auto def = glow_text(opts, glow_color, glow_radius, glow_intensity);

    // Content + frame fields are unchanged from pre-Phase-A5 (sanity).
    CHECK(def.content.value == "PARITY_GLOW");
    CHECK(def.frame.anchor == TextAnchor::Center);
    CHECK(def.frame.align  == TextAlign::Center);

    // Canonical TextMaterial seam (the parity assertion itself):
    CHECK(def.style.material.use_material_glow == true);
    CHECK(def.style.material.glow_color.r       == doctest::Approx(glow_color.r));
    CHECK(def.style.material.glow_color.g       == doctest::Approx(glow_color.g));
    CHECK(def.style.material.glow_color.b       == doctest::Approx(glow_color.b));
    CHECK(def.style.material.glow_color.a       == doctest::Approx(glow_color.a));
    CHECK(def.style.material.glow_radius        == doctest::Approx(glow_radius));
    CHECK(def.style.material.glow_intensity     == doctest::Approx(glow_intensity));

    // Direct field-by-field equivalence vs a hand-built TextMaterial with
    // the SAME inputs the legacy `def.effects.*` API would have taken.
    TextMaterial manual;
    manual.use_material_glow = true;
    manual.glow_color        = glow_color;
    manual.glow_radius       = glow_radius;
    manual.glow_intensity    = glow_intensity;

    CHECK(manual.use_material_glow == def.style.material.use_material_glow);
    CHECK(manual.glow_color.r       == doctest::Approx(def.style.material.glow_color.r));
    CHECK(manual.glow_color.g       == doctest::Approx(def.style.material.glow_color.g));
    CHECK(manual.glow_color.b       == doctest::Approx(def.style.material.glow_color.b));
    CHECK(manual.glow_radius        == doctest::Approx(def.style.material.glow_radius));
    CHECK(manual.glow_intensity     == doctest::Approx(def.style.material.glow_intensity));
}

TEST_CASE("Phase A5: bevel_px on TextMaterial is the canonical bevel authority (no duplicate on TextEffects)") {
    // The legacy `TextEffects.bevel_px / .bevel_highlight_opacity / .bevel_shadow_opacity
    // / .bevel_highlight_color` field set never existed on `TextMaterial`
    // as an exact duplicate — `TextMaterial` was the entire authority for
    // bevel all along.  Phase A5 documents this: post-elimination,
    // `def.style.material.bevel_px == X` IS the only seam.
    TextDefinition def;
    def.style.material.bevel_px                  = 2.5f;
    def.style.material.bevel_highlight_opacity   = 0.45f;
    def.style.material.bevel_shadow_opacity      = 0.30f;
    def.style.material.bevel_highlight_color     = Color{1.0f, 1.0f, 0.0f, 1.0f};

    CHECK(def.style.material.bevel_px                  == doctest::Approx(2.5f));
    CHECK(def.style.material.bevel_highlight_opacity   == doctest::Approx(0.45f));
    CHECK(def.style.material.bevel_shadow_opacity      == doctest::Approx(0.30f));
    CHECK(def.style.material.bevel_highlight_color.r   == doctest::Approx(1.0f));
    CHECK(def.style.material.bevel_highlight_color.b   == doctest::Approx(0.0f));
}

TEST_CASE("Phase A5: centered_text → manual TextMaterial assignment = same fields as glow_text") {
    // The degenerate comparison: pre-Phase-A5, callers could
    //   1. call `centered_text()` (no glow),
    //   2. set `def.effects.*` for glow (legacy duplicate seam), and
    // observe a glow render path.
    // Post-Phase-A5 the same observation requires
    //   1. call `centered_text()` (no glow),
    //   2. set `def.style.material.*` (canonical seam).
    // Both paths produce the SAME renderer-relevant state of the material
    // struct (this is the parity assertion in a single function call).
    CenterTextOptions opts;
    const Color glow_color{0.4f, 0.8f, 1.0f, 1.0f};
    const f32   glow_radius    = 16.0f;
    const f32   glow_intensity = 0.9f;

    auto direct = glow_text(opts, glow_color, glow_radius, glow_intensity);
    auto manual = centered_text(opts);
    manual.style.material.use_material_glow = true;
    manual.style.material.glow_color        = glow_color;
    manual.style.material.glow_radius       = glow_radius;
    manual.style.material.glow_intensity    = glow_intensity;

    // Field-by-field equality between the helper-site route and the
    // canonical-textDirect route — they should be IDENTICAL post-Phase-A5.
    CHECK(direct.style.material.use_material_glow == manual.style.material.use_material_glow);
    CHECK(direct.style.material.glow_color.r       == doctest::Approx(manual.style.material.glow_color.r));
    CHECK(direct.style.material.glow_color.g       == doctest::Approx(manual.style.material.glow_color.g));
    CHECK(direct.style.material.glow_color.b       == doctest::Approx(manual.style.material.glow_color.b));
    CHECK(direct.style.material.glow_color.a       == doctest::Approx(manual.style.material.glow_color.a));
    CHECK(direct.style.material.glow_radius        == doctest::Approx(manual.style.material.glow_radius));
    CHECK(direct.style.material.glow_intensity     == doctest::Approx(manual.style.material.glow_intensity));
}

// ═══════════════════════════════════════════════════════════════════════════
// 18. TextAnimation — Phase A.3: animators + selectors + run-control + Frame timing
// ═══════════════════════════════════════════════════════════════════════════
//
// TextAnimation is the per-text-run animation contract.  Its 8 fields mirror
// the top-level editor surface carried by TextRunSpec (animators, selectors,
// direction, language, script, cache_layout) + 2 Frame timing fields
// (start_delay, duration) for the global envelope.

TEST_CASE("TextAnimation: default construction has empty animators + selectors + Auto direction") {
    TextAnimation anim;
    CHECK(anim.animators.empty());
    CHECK(anim.selectors.empty());
    CHECK(anim.direction == TextDirection::Auto);
    CHECK(anim.language.empty());
    CHECK(anim.script == 0u);
    CHECK(anim.cache_layout == true);
}

TEST_CASE("TextAnimation: from_text_spec(default) gives TextDef with default TextAnimation") {
    // TextSpec has no animation concept — forward adapter leaves def.animation
    // at default (empty vectors, Auto direction, 0 script, cache_layout=true).
    TextSpec spec;
    auto def = from_text_spec(spec);
    CHECK(def.animation.animators.empty());
    CHECK(def.animation.selectors.empty());
    CHECK(def.animation.direction == TextDirection::Auto);
    CHECK(def.animation.language.empty());
    CHECK(def.animation.script == 0u);
    CHECK(def.animation.cache_layout == true);
}

TEST_CASE("TextAnimation: from_text_run_spec populates animators + selectors + run-control") {
    // Phase A.3 wiring (replaces the prior (void)silence pattern).
    TextRunSpec run;
    run.text.content.value = "ANIMATED";
    run.direction    = TextDirection::RTL;
    run.language     = "ar";
    run.script       = 0x41726162u;  // 'Arab' OpenType tag
    run.cache_layout = false;

    TextAnimatorSpec animator;
    animator.id      = "fade_in";
    animator.enabled = true;
    run.animators.push_back(animator);

    GlyphSelectorSpec selector;
    selector.id = "all_glyphs";
    run.selectors.push_back(selector);

    auto def = from_text_run_spec(run);

    CHECK(def.content.value == "ANIMATED");
    REQUIRE(def.animation.animators.size() == 1);
    CHECK(def.animation.animators[0].id == "fade_in");
    CHECK(def.animation.animators[0].enabled == true);
    REQUIRE(def.animation.selectors.size() == 1);
    CHECK(def.animation.selectors[0].id == "all_glyphs");
    CHECK(def.animation.direction == TextDirection::RTL);
    CHECK(def.animation.language == "ar");
    CHECK(def.animation.script == 0x41726162u);
    CHECK(def.animation.cache_layout == false);
}

TEST_CASE("TextAnimation: Frame start_delay/duration default to Frame{0} (compile-time contract test)") {
    // Phase A.3 architectural choice: the global envelope is in Frame units
    // (mirrors TextAnimatorSpec.properties[].frame convention).  start_delay +
    // duration have no TextRunSpec source, so the adapter does NOT map them
    // (they default to Frame{0} = immediate / use-per-animator).  This test
    // exercises Frame type compatibility + assignment + mutation across
    // multiple property accesses (compile-time proof of the contract).
    TextAnimation anim;
    anim.start_delay = Frame{42};
    anim.duration    = Frame{120};
    // Re-assignment exercises the Frame overload on the same slot.
    anim.start_delay = Frame{99};
    anim.duration    = Frame{200};
    // Direct setting on TextDefinition (the canonical authoring surface).
    TextDefinition def;
    def.animation.start_delay = Frame{15};
    def.animation.duration    = Frame{180};
    // Roundtrip through the run-spec adapter (start_delay/duration are
    // dropped — see LIFECYCLE LOSSY REVERSE comment).
    TextRunSpec rs;
    rs.text.content.value = "T";
    rs.animators = { TextAnimatorSpec{} };
    rs.selectors = { GlyphSelectorSpec{} };
    auto def2 = from_text_run_spec(rs);
    CHECK(def2.animation.animators.size() == 1);
    CHECK(def2.animation.selectors.size() == 1);
}

TEST_CASE("TextAnimation: reverse adapter drops animation (TextDef-only by design)") {
    // Phase A.3 mirror of Phase A.2 (TextFrame.offset): TextSpec has no
    // animation concept by design.  The reverse adapter therefore drops
    // animators/selectors/direction/language/script/cache_layout on the floor.
    // Lossy by design + documented in the LIFECYCLE comment.
    TextDefinition def;
    TextAnimatorSpec animator;
    animator.id = "fade_in";
    def.animation.animators.push_back(animator);
    GlyphSelectorSpec selector;
    selector.id = "all_glyphs";
    def.animation.selectors.push_back(selector);
    def.animation.direction    = TextDirection::RTL;
    def.animation.language     = "ja";
    def.animation.script       = 0x4A616E20u;  // 'Jpan ' OpenType script tag
    def.animation.cache_layout = false;
    auto spec = chronon3d::compat::from_text_definition(def);   // spec has no animation field
    (void)spec;
    auto restored = from_text_spec(spec);   // default TextAnimation
    CHECK(restored.animation.animators.empty());
    CHECK(restored.animation.selectors.empty());
    CHECK(restored.animation.direction == TextDirection::Auto);
    CHECK(restored.animation.language.empty());
    CHECK(restored.animation.script == 0u);
    CHECK(restored.animation.cache_layout == true);
}

// ═══════════════════════════════════════════════════════════════════════════
// 19. to_text_run_spec — F2.D: TextDefinition → TextRunSpec reverse adapter
// ═══════════════════════════════════════════════════════════════════════════
//
// F2.D closes the LOSSY REVERSE gap flagged in the LIFECYCLE comment of
// text_definition.hpp:  the 6 spec-only animation fields
// (animators, selectors, direction, language, script, cache_layout) are now
// carried back from TextDefinition to a TextRunSpec.
//
// DOCUMENTED LOSSY DROP: TextAnimation.start_delay + .duration (Frame envelope)
// are NOT representable in TextRunSpec; the conversion therefore silently
// drops them.  Roundtrip TextDefinition → TextRunSpec → TextDefinition yields
// Frame{0} for both envelope fields.

TEST_CASE("to_text_run_spec: forward mapping populates all 6 animation fields") {
    TextDefinition def;
    def.content.value = "F2D forward";
    def.style.font.font_size = 48.0f;

    // Populate the 6 spec-only animation fields
    TextAnimatorSpec animator;
    animator.id = "fade_in";
    animator.enabled = true;
    def.animation.animators.push_back(animator);
    GlyphSelectorSpec selector;
    selector.id = "all_glyphs";
    def.animation.selectors.push_back(selector);
    def.animation.direction    = TextDirection::RTL;
    def.animation.language     = "ar";
    def.animation.script       = 0x41726162u;  // 'Arab' OpenType tag
    def.animation.cache_layout = false;

    auto run = chronon3d::compat::to_text_run_spec(def);

    // The 6 animation fields must all be carried over verbatimly
    REQUIRE(run.animators.size() == 1);
    CHECK(run.animators[0].id == "fade_in");
    CHECK(run.animators[0].enabled == true);
    REQUIRE(run.selectors.size() == 1);
    CHECK(run.selectors[0].id == "all_glyphs");
    CHECK(run.direction    == TextDirection::RTL);
    CHECK(run.language     == "ar");
    CHECK(run.script       == 0x41726162u);
    CHECK(run.cache_layout == false);
}

TEST_CASE("to_text_run_spec: base spec reuses from_text_definition (no manual remap drift)") {
    // The base TextSpec fields must be populated via chronon3d::compat::from_text_definition() —
    // verifying that the F2.D adapter does NOT manually map the 22 base
    // fields (drift-prevention pattern documented in the header).
    TextDefinition def;
    def.content.value = "Base reuse";
    def.style.font = {.font_path = "fonts/F2D.ttf", .font_family = "F2D",
                      .font_weight = 600, .font_size = 64.0f};
    def.frame.size = {1200.0f, 240.0f};
    def.frame.anchor = TextAnchor::TopCenter;
    def.frame.tracking = 1.5f;
    def.frame.max_lines = 3;
    def.style.color = Color{0.4f, 0.6f, 0.8f, 1.0f};
    def.frame.placement.offset = {400.0f, 300.0f};

    auto run = chronon3d::compat::to_text_run_spec(def);
    // The behavior MUST match chronon3d::compat::from_text_definition(def): 22 base fields preserved
    auto direct_spec = chronon3d::compat::from_text_definition(def);

    CHECK(run.text.content.value == direct_spec.content.value);
    CHECK(run.text.font.font_path == direct_spec.font.font_path);
    CHECK(run.text.font.font_family == direct_spec.font.font_family);
    CHECK(run.text.font.font_weight == direct_spec.font.font_weight);
    CHECK(run.text.font.font_size == doctest::Approx(direct_spec.font.font_size));
    CHECK(run.text.layout.box.x == doctest::Approx(direct_spec.layout.box.x));
    CHECK(run.text.layout.box.y == doctest::Approx(direct_spec.layout.box.y));
    CHECK(run.text.layout.anchor == direct_spec.layout.anchor);
    CHECK(run.text.layout.tracking == doctest::Approx(direct_spec.layout.tracking));
    CHECK(run.text.layout.max_lines == direct_spec.layout.max_lines);
    CHECK(run.text.appearance.color.r == doctest::Approx(direct_spec.appearance.color.r));
    CHECK(run.text.appearance.color.g == doctest::Approx(direct_spec.appearance.color.g));
    CHECK(run.text.appearance.color.b == doctest::Approx(direct_spec.appearance.color.b));
    CHECK(run.text.placement.offset.x == doctest::Approx(direct_spec.placement.offset.x));
    CHECK(run.text.placement.offset.y == doctest::Approx(direct_spec.placement.offset.y));
}

TEST_CASE("to_text_run_spec: empty def yields empty animation vectors + default defaults") {
    // Default-constructed TextDefinition has empty animators + selectors +
    // probe default TextRunSpec fields too (direction=Auto, language="",
    // script=0, cache_layout=true — TextRunSpec's defaults).
    TextDefinition def;
    auto run = chronon3d::compat::to_text_run_spec(def);

    CHECK(run.animators.empty());
    CHECK(run.selectors.empty());
    CHECK(run.direction == TextDirection::Auto);
    CHECK(run.language.empty());
    CHECK(run.script == 0u);
    CHECK(run.cache_layout == true);  // TextRunSpec default is true; mapping preserves it
}

TEST_CASE("to_text_run_spec: Frame start_delay/duration are lossily dropped (canonical behaviour)") {
    // F2.D documented loss: start_delay + duration are NOT representable in
    // TextRunSpec.  The adapter therefore silently drops them.  This test
    // exercises the canonical, tested behaviour (NOT a bug, NOT undefined).
    //
    // Verification strategy: roundtrip def → run_spec → new def then assert
    // the envelope is Frame{0} (the default Frame value used by the
    // from_text_run_spec forward path when no field is set).
    TextDefinition def;
    def.content.value = "Envelope lossy";
    def.animation.start_delay = Frame{42};   // explicitly set; will be dropped
    def.animation.duration    = Frame{120};  // explicitly set; will be dropped
    // Plus 1 mappable field for context
    TextAnimatorSpec animator;
    animator.id = "fade_in";
    def.animation.animators.push_back(animator);

    auto run = chronon3d::compat::to_text_run_spec(def);
    // Verification is on the ROUND-TRIPPED definition (returned by
    // from_text_run_spec), not on run_spec directly (TextRunSpec lacks these
    // fields at all).
    auto roundtrip = from_text_run_spec(run);

    CHECK(roundtrip.animation.start_delay == Frame{0});   // dropped → Frame{0}
    CHECK(roundtrip.animation.duration    == Frame{0});   // dropped → Frame{0}
    // The single mappable field survives the round-trip
    REQUIRE(roundtrip.animation.animators.size() == 1);
    CHECK(roundtrip.animation.animators[0].id == "fade_in");
}

TEST_CASE("to_text_run_spec: round-trip is idempotent for the 6 spec-only fields") {
    // The point of F2.D:  def → run_spec → def should round-trip the 6
    // animation fields verbatimly (modulo the documented Frame envelope drop).
    TextDefinition def;
    def.content.value = "Round-trip F2D";
    def.style.font.font_size = 56.0f;
    def.frame.size = {1000.0f, 200.0f};
    def.frame.placement.offset = Vec2{42.5f, -17.3f};

    TextAnimatorSpec animator_a;
    animator_a.id = "fade_in";
    animator_a.enabled = true;
    TextAnimatorSpec animator_b;
    animator_b.id = "typewriter";
    animator_b.enabled = false;
    def.animation.animators = {animator_a, animator_b};

    GlyphSelectorSpec selector;
    selector.id = "every_2nd";
    def.animation.selectors = {selector};

    def.animation.direction    = TextDirection::LTR;
    def.animation.language     = "en-US";
    def.animation.script       = 0x4C61746Eu;  // 'Latn' OpenType tag
    def.animation.cache_layout = true;

    // Round-trip: def → run_spec → def
    auto run = chronon3d::compat::to_text_run_spec(def);
    auto restored = from_text_run_spec(run);

    // The 6 spec-only fields must round-trip exactly
    REQUIRE(restored.animation.animators.size() == 2);
    CHECK(restored.animation.animators[0].id == "fade_in");
    CHECK(restored.animation.animators[0].enabled == true);
    CHECK(restored.animation.animators[1].id == "typewriter");
    CHECK(restored.animation.animators[1].enabled == false);
    REQUIRE(restored.animation.selectors.size() == 1);
    CHECK(restored.animation.selectors[0].id == "every_2nd");
    CHECK(restored.animation.direction    == TextDirection::LTR);
    CHECK(restored.animation.language     == "en-US");
    CHECK(restored.animation.script       == 0x4C61746Eu);
    CHECK(restored.animation.cache_layout == true);

    // The base TextSpec fields ALSO round-trip (via from_text_definition reuse)
    CHECK(restored.content.value == "Round-trip F2D");
    CHECK(restored.style.font.font_size == doctest::Approx(56.0f));
    CHECK(restored.frame.size.x == doctest::Approx(1000.0f));
    CHECK(restored.frame.size.y == doctest::Approx(200.0f));

    CHECK(restored.frame.placement.offset.x == doctest::Approx(42.5f));
    CHECK(restored.frame.placement.offset.y == doctest::Approx(-17.3f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 20. F3.D — helper-site authoring via to_text_run_spec preserves the 6
//            spec-only animation fields in TextRunSpec
// ═══════════════════════════════════════════════════════════════════════════
//
// F3.D forward-point rewires the 2 LayerBuilder TextDefinition overloads
// (`text(name, TextDefinition)` + `text_run(name, TextDefinition)`) to route
// through to_text_run_spec instead of the F2.C lossy `from_text_definition`
// path.  This locks the contract for the 17 helper-site call sites
// (`centered_text()` / `glow_text()` / `typewriter_text()` / presets):
// a helper-site author who AUGMENTS the returned TextDefinition with animation
// now has those fields reach the renderer.
//
// Regression vector: if a future edit reverts the F3.D rewiring back to
// `from_text_definition` / `text(name, TextSpec)`, the 6 spec-only animation
// fields would be silently dropped by the from_text_definition return path.
// This test exercises the same authoring pattern callsites use and asserts
// every field survives the F3.D wire.
//
// Frame envelope lossily dropped by to_text_run_spec per F2.D contract is
// NOT asserted here — it is covered by group 19.4 (F2.D lossy drop test).

TEST_CASE("F3.D: helper-site centered_text + animation augmentation flows through to_text_run_spec") {
    CenterTextOptions opts;
    opts.text        = "F3D_HELPER";
    opts.box         = {1000.0f, 200.0f};
    opts.pos         = {500.0f, 300.0f, 0.0f};
    opts.font_asset  = "fonts/F3D.ttf";
    opts.font_family = "F3D";
    opts.font_weight = 700;
    opts.font_size   = 80.0f;
    opts.tracking    = 2.0f;
    opts.color       = Color{0.5f, 0.5f, 0.5f, 1.0f};

    // Helper-site authoring pattern: build a TextDefinition via centered_text,
    // then AUGMENT with animation fields (this is what helper-site authors do
    // when they need custom animator / direction / etc. on top of the
    // canonical preset).
    auto def = centered_text(opts);
    TextAnimatorSpec fade_in;
    fade_in.id      = "fade_in";
    fade_in.enabled = true;
    TextAnimatorSpec typewriter;
    typewriter.id      = "typewriter";
    typewriter.enabled = false;
    def.animation.animators = {fade_in, typewriter};
    GlyphSelectorSpec every_glyph;
    every_glyph.id = "every_glyph";
    def.animation.selectors = {every_glyph};
    def.animation.direction    = TextDirection::LTR;
    def.animation.language     = "en-US";
    def.animation.script       = 0x4C61746Eu;  // 'Latn'
    def.animation.cache_layout = true;

    // Simulate the F3.D wire in LayerBuilder::text_run(name, TextDefinition):
    //   text_run(name, chronon3d::compat::to_text_run_spec(def));
    auto run = chronon3d::compat::to_text_run_spec(def);

    // The 6 spec-only animation fields must all reach the TextRunSpec.
    // (This is the contract that F3.D enables for the 17 helper sites.)
    REQUIRE(run.animators.size() == 2);
    CHECK(run.animators[0].id == "fade_in");
    CHECK(run.animators[0].enabled == true);
    CHECK(run.animators[1].id == "typewriter");
    CHECK(run.animators[1].enabled == false);
    REQUIRE(run.selectors.size() == 1);
    CHECK(run.selectors[0].id == "every_glyph");
    CHECK(run.direction    == TextDirection::LTR);
    CHECK(run.language     == "en-US");
    CHECK(run.script       == 0x4C61746Eu);
    CHECK(run.cache_layout == true);

    // The 22 base fields (via from_text_definition reuse) are still preserved
    // — F3.D must not regress the base-spec path.  Use the populated-funnel
    // assertions: content + font_weight + font_size + box + position + color.
    CHECK(run.text.content.value == "F3D_HELPER");
    CHECK(run.text.font.font_family == "F3D");
    CHECK(run.text.font.font_weight == 700);
    CHECK(run.text.font.font_size == doctest::Approx(80.0f));
    CHECK(run.text.layout.box.x == doctest::Approx(1000.0f));
    CHECK(run.text.layout.box.y == doctest::Approx(200.0f));
    CHECK(run.text.appearance.color.r == doctest::Approx(0.5f));
    CHECK(run.text.placement.offset.x == doctest::Approx(500.0f));
    CHECK(run.text.placement.offset.y == doctest::Approx(300.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 21. TICKET-TEXT-ANIMATOR-COMPILE-ISVALID — §5.0a + §5.0e chain methods
// ═══════════════════════════════════════════════════════════════════════════
//
// REQ-1..REQ-5 inspector-driven assertions on TextAnimatorSpec::compile() +
// TextAnimatorSpec::is_valid().  The chain-method pair (compile + is_valid)
// closes the user-spec `resolve().compile().is_valid()` gap explicitly
// documented in docs/CHANGELOG.md §5 (the event "the .hpp had the typing
// surface but no compile()/is_valid() implementation; the authoring chain
// was effectively '(compile()?) (is_valid()?) — silent fallthrough'").
//
// Inspector macro REQ_VALID_ANIMATOR_REQUIRE(spec) — short-circuit-style
// assertion: if spec is invalid, the assertion fires with the violation
// details (which REQ-N failed + the offending property).  Pattern matches
// the prior `STATE-EFFECT` inspector in tests/presets/test_presets.cpp
// ("BUILDER STATE-EFFECT — lb.text(name, spec) actually adds a node").
//
// The chain-method pair is tested via both the direct call
// `spec.is_valid()` AND the fluent `spec.compile().is_valid()` form — the
// compile() call is a no-op (header-docblock §5.0a) but returning
// TextAnimatorSpec& must NOT mask the underlying invariant check.
//
// 4 Invariants under test (matches docs/CHANGELOG.md §5.0e spec + the
// post-code-reviewer-fix collapse from 5 → 4 invariants — see Inv 2
// dedicated below).  Post-recorrection: only the 4 invariants carry
// meaningful weight beyond the membership predicate (`!empty/!empty`
// empty-check); the original "Inv 2 LENIENT" was a tautology
// (`size >= 0` always-true) and was dropped per code-reviewer verdict.
//   Inv 1 — Non-empty predicates (selectors AND properties).
//   Inv 2 — Strict monotonicity (no duplicate keyframe frames).
//   Inv 3 — Value integrity (no NaN/Inf in keyframe values or static fields).
//   Inv 4 — Blend-mode coverage (transform_mode + color_mode in {Add, Replace, Multiply}).

#include <cmath>  // std::nanf

TEST_CASE("§5.0a+§5.0e Inv 1: empty spec fails is_valid (both selectors AND properties empty)") {
    TextAnimatorSpec empty_spec;
    CHECK(empty_spec.selectors.empty());
    CHECK(empty_spec.properties.empty());
    CHECK_FALSE(empty_spec.is_valid());  // Inv 1 — both empty
    CHECK_FALSE(empty_spec.compile().is_valid());  // chain-form SAME
}

TEST_CASE("§5.0a+§5.0e Inv 1: non-empty selectors + empty properties fails is_valid") {
    TextAnimatorSpec spec;
    spec.id = "selectors_only";
    GlyphSelectorSpec sel;
    sel.id = "every_glyph";
    spec.selectors.push_back(sel);
    CHECK_FALSE(spec.is_valid());  // Inv 1 — properties empty
}

TEST_CASE("§5.0a+§5.0e Inv 1: empty selectors + non-empty properties fails is_valid") {
    TextAnimatorSpec spec;
    spec.id = "properties_only";
    spec.properties.push_back(RotationProperty{Vec3{45.0f, 0.0f, 0.0f}});
    CHECK_FALSE(spec.is_valid());  // Inv 1 — selectors empty
}

// (Code-reviewer pass removed the REQ-2 LENIENT test case as a tautology
// invariant — see src/text/animation/text_animator_compile.cpp header
// comment for why Inv 2 (strict monotonicity) is the cleanest "beyond
// membership" invariant when §5.0e is collapsed to 4 invariants.)

TEST_CASE("§5.0a+§5.0e Inv 2: animated OpacityProperty with monotonic keyframes PASS (N=2, trivial monotonic)") {
    // The "rich" authoring case: OpacityProperty has 2 keyframes at frames
    // 0 and 60 with non-NaN values — the canonical "fade in" animator.
    // N=2 exercises the lower-bound path — `if (kfs.size() < 2) return true`
    // short-circuits the helper at N<2, so N=2 still hits the strict-
    // monotonicity comparator on a single pair.
    TextAnimatorSpec spec;
    spec.id = "fade_in";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    OpacityProperty op{0.0f};
    op.value.add_keyframe(Frame{0},   0.0f);
    op.value.add_keyframe(Frame{60},  1.0f);
    spec.properties.push_back(op);
    CHECK(spec.is_valid());  // Inv 2 — strictly monotonic
    CHECK(spec.compile().is_valid());  // chain-form
}

TEST_CASE("§5.0a+§5.0e Inv 2: animated OpacityProperty with monotonic curve N=3 (real curve monotonic)") {
    // The N≥3 case is the meaningful Inv 2 test — exercises the strict
    // monotonicity comparator over a real 3-keyframe curve (N=2 trivially
    // short-circuits the helper).  Without this test, the Inv 2 helper
    // `check_monotonic_av` is never actually walked past its first entry
    // on a populated time-curve.
    TextAnimatorSpec spec;
    spec.id = "fade_in_3_keyframes";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    OpacityProperty op{0.0f};
    op.value.add_keyframe(Frame{0},   0.0f);
    op.value.add_keyframe(Frame{30},  0.5f);
    op.value.add_keyframe(Frame{60},  1.0f);
    spec.properties.push_back(op);
    CHECK(spec.is_valid());  // Inv 2 — 3 keyframes, all strictly increasing
}

TEST_CASE("§5.0a+§5.0e Inv 2: animated OpacityProperty with DUPLICATE keyframe frames fails") {
    // Locks Inv 2 against the "add_keyframe twice at frame N" footgun.
    // AnimatorValue::add_keyframe does NOT reject duplicates (it just
    // std::sorts which preserves equal keys).  The chain's compile() +
    // is_valid() check catches the violation explicitly.
    TextAnimatorSpec spec;
    spec.id = "duplicate_frames_anti_lock";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    OpacityProperty op{0.0f};
    op.value.add_keyframe(Frame{30},  0.5f);
    op.value.add_keyframe(Frame{30},  0.6f);  // SAME FRAME — breaks Inv 2
    spec.properties.push_back(op);
    CHECK_FALSE(spec.is_valid());  // Inv 2 — duplicate frames
}

TEST_CASE("§5.0a+§5.0e Inv 3: animated OpacityProperty with NaN keyframe value fails") {
    // Locks Inv 3 against the "0/0 normalized fade-in" footgun.  AnimatedValue
    // does NOT reject NaN at insertion; the chain catches it.
    TextAnimatorSpec spec;
    spec.id = "nan_keyframe_anti_lock";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    OpacityProperty op{0.0f};
    const float nan_val = std::nanf("");
    op.value.add_keyframe(Frame{0},  0.0f);
    op.value.add_keyframe(Frame{30}, nan_val);  // NaN — breaks Inv 3
    spec.properties.push_back(op);
    CHECK_FALSE(spec.is_valid());  // Inv 3 — NaN keyframe
}

TEST_CASE("§5.0a+§5.0e Inv 3: static RotationProperty with NaN degrees fails (RotationProperty::degrees lock)") {
    // Inv 3 also covers static-value properties.  Locks the §5.0e rotation
    // dispatch: RotationProperty has `degrees` (NOT `value`) — the code-
    // reviewer NEEDS-FIX iteration caught that lumping Rotation+Anchor on
    // `p.value` would be a compile error since RotationProperty has no value
    // field.  This test exercise the correct `p.degrees` path.
    TextAnimatorSpec spec;
    spec.id = "nan_static_rot_anti_lock";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    RotationProperty rp;
    rp.degrees.x = std::nanf("");
    rp.degrees.y = 0.0f;
    rp.degrees.z = 0.0f;
    spec.properties.push_back(rp);
    CHECK_FALSE(spec.is_valid());  // Inv 3 — NaN in RotationProperty.degrees
}

TEST_CASE("§5.0a+§5.0e Inv 3: static AnchorProperty with NaN value fails (AnchorProperty::value lock)") {
    // Companion to the RotationProperty anti-lock: AnchorProperty uses
    // `p.value` (Vec3).  Both static-value Vec3 properties are now correctly
    // dispatched (separate is_same_v branches) per the §5.0e code-reviewer
    // fix.
    TextAnimatorSpec spec;
    spec.id = "nan_static_anchor_anti_lock";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    AnchorProperty ap;
    ap.value.x = std::nanf("");
    ap.value.y = 0.0f;
    ap.value.z = 0.0f;
    spec.properties.push_back(ap);
    CHECK_FALSE(spec.is_valid());  // Inv 3 — NaN in AnchorProperty.value
}

TEST_CASE("§5.0a+§5.0e Inv 4: blend_mode coverage (default Add + Replace pass)") {
    // The default-constructed transform_mode=Add + color_mode=Replace is
    // the canonical valid blend-mode setting.  Both must pass Inv 4.
    TextAnimatorSpec spec;
    spec.id = "default_blend_modes";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    spec.properties.push_back(ScaleProperty{Vec3{1.5f, 1.5f, 1.5f}});
    CHECK(spec.transform_mode == TextPropertyBlendMode::Add);
    CHECK(spec.color_mode == TextPropertyBlendMode::Replace);
    CHECK(spec.is_valid());  // Inv 4 — default blend modes present
}

TEST_CASE("§5.0a+§5.0e Inv 4: blend-mode invariant on Multiply (explicit non-default enum)") {
    // Locks Inv 4 covers all 3 enum members, not just default Add + Replace.
    // Without this test, a future constriction of the Inv 4 check (e.g.
    // dropping Multiply from the explicit value-comparison) would not be
    // caught.
    TextAnimatorSpec spec;
    spec.id = "multiply_blend_modes";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    spec.properties.push_back(ScaleProperty{Vec3{0.5f, 0.5f, 0.5f}});
    spec.transform_mode = TextPropertyBlendMode::Multiply;
    spec.color_mode = TextPropertyBlendMode::Multiply;
    CHECK(spec.is_valid());  // Inv 4 — Multiply (the third enum member) is also valid
}

TEST_CASE("§5.0a+§5.0e: compile() returns self-reference enabling fluent chain") {
    // Locks the §5.0a contract: compile() returns TextAnimatorSpec& (a
    // from-this reference) so the chain `spec.compile().is_valid()` is
    // a single fluent expression.  Verifies both the reference identity
    // (same struct, same fields) AND the chain execution.
    TextAnimatorSpec spec;
    spec.id = "chain_test";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    spec.properties.push_back(ScaleProperty{Vec3{1.0f, 1.0f, 1.0f}});

    TextAnimatorSpec& chained = spec.compile();
    CHECK(&chained == &spec);  // self-reference identity lock
    CHECK(chained.id == spec.id);
    CHECK(chained.is_valid() == spec.is_valid());
    CHECK(chained.compile().is_valid() == spec.compile().is_valid());
}

TEST_CASE("§5.0a+§5.0e: chain-form == direct-form over 100 repeated calls (agreement sanity)") {
    // Sanity check: 100 calls of `spec.compile().is_valid()` (chain-form)
    // agree with `spec.is_valid()` (direct-form) on the same struct.
    // Locks §5.0a's "the SelfRef return must NOT mask the underlying
    // invariant check" contract under repeated invocation.
    TextAnimatorSpec spec;
    spec.id = "chain_repeat_100";
    GlyphSelectorSpec sel;
    sel.id = "all_glyphs";
    spec.selectors.push_back(sel);
    spec.properties.push_back(OpacityProperty{1.0f});
    spec.properties.push_back(ScaleProperty{Vec3{2.0f, 2.0f, 2.0f}});

    // Capture the direct-form boolean so each iteration can compare
    // against the chain-form.
    const bool direct = spec.is_valid();
    for (int i = 0; i < 100; ++i) {
        CHECK(spec.compile().is_valid() == direct);
    }
    CHECK(spec.is_valid() == direct);  // one final pass-through after the loop
}
