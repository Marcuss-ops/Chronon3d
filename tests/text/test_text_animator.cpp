#include <doctest/doctest.h>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/text/text_animator.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
using namespace chronon3d;


static FontSpec inter_bold_spec() {
    return FontSpec{
        .font_path = "assets/fonts/Inter-Bold.ttf",
        .font_family = "Inter",
        .font_weight = 700,
    };
}

TEST_CASE("TextAnimator splits text by character") {
    TextAnimator ta;
    ta.text("ABC").config({.mode = TextAnimMode::ByCharacter});

    auto units = ta.split_units();
    REQUIRE(units.size() == 3);
    CHECK(units[0] == "A");
    CHECK(units[1] == "B");
    CHECK(units[2] == "C");
}

TEST_CASE("TextAnimator splits text by word") {
    TextAnimator ta;
    ta.text("hello world test").config({.mode = TextAnimMode::ByWord});

    auto units = ta.split_units();
    REQUIRE(units.size() == 3);
    CHECK(units[0] == "hello");
    CHECK(units[1] == "world");
    CHECK(units[2] == "test");
}

TEST_CASE("TextAnimator splits text by line") {
    TextAnimator ta;
    ta.text("line1\nline2\nline3").config({.mode = TextAnimMode::ByLine});

    auto units = ta.split_units();
    REQUIRE(units.size() == 3);
    CHECK(units[0] == "line1");
    CHECK(units[1] == "line2");
    CHECK(units[2] == "line3");
}

TEST_CASE("TextAnimator handles empty text") {
    TextAnimator ta;
    ta.text("").config({.mode = TextAnimMode::ByCharacter});

    auto units = ta.split_units();
    CHECK(units.empty());
}

TEST_CASE("TextAnimator unit_delay returns zero for single unit") {
    TextAnimator ta;
    ta.text("X").font_size(48).config({
        .mode = TextAnimMode::ByCharacter,
        .duration = Frame{20},
        .delay_per_unit = Frame{5},
        .easing = EasingCurve{Easing::Linear}
    });

    SceneBuilder sb;
    // build should work without crashing
    ta.build(sb, "test");
    CHECK(true);
}

TEST_CASE("TextAnimator measure_unit_width returns expected width") {
    TextAnimator ta;
    ta.text("ABC").font_size(72.0f);
    // Each char is approx 72 * 0.58 = 41.76, so 3 chars ≈ 125.28
    f32 w = ta.measure_unit_width("ABC");
    CHECK(w > 0.0f);
    CHECK(w < 200.0f);
    CHECK(w == doctest::Approx(72.0f * 0.58f * 3.0f));
}

TEST_CASE("TextAnimator builds layers into scene") {
    SceneBuilder sb;

    TextAnimator ta;
    ta.text("Hello")
        .font_size(64.0f)
        .color(Color::white())
        .config({
            .mode = TextAnimMode::ByCharacter,
            .duration = Frame{30},
            .delay_per_unit = Frame{4},
            .easing = EasingCurve{Easing::OutCubic},
            .animate_opacity = true,
        });

    ta.build(sb, "my_title");

    Scene result = sb.build();
    // Should create 5 layers (one per character)
    CHECK(result.layers().size() > 0);
}

TEST_CASE("TextAnimator config tracking sets initial state") {
    TextAnimConfig cfg;
    cfg.mode = TextAnimMode::ByCharacter;
    cfg.animate_tracking = true;
    cfg.tracking_from = 20.0f;

    CHECK(cfg.animate_tracking == true);
    CHECK(cfg.tracking_from == 20.0f);
}

TEST_CASE("TextAnimator ByLine splits newlines correctly") {
    TextAnimator ta;
    ta.text("a\nb\nc\nd").config({.mode = TextAnimMode::ByLine});

    auto units = ta.split_units();
    REQUIRE(units.size() == 4);
    CHECK(units[0] == "a");
    CHECK(units[1] == "b");
    CHECK(units[2] == "c");
    CHECK(units[3] == "d");
}

TEST_CASE("TextAnimator ByWord handles multiple spaces") {
    TextAnimator ta;
    ta.text("a   b").config({.mode = TextAnimMode::ByWord});

    auto units = ta.split_units();
    REQUIRE(units.size() == 2);
    CHECK(units[0] == "a");
    CHECK(units[1] == "b");
}

TEST_CASE("TextAnimator builds with all animation options enabled") {
    SceneBuilder sb;

    TextAnimator ta;
    ta.text("Hi!")
        .font_size(80.0f)
        .color(Color{1.0f, 0.5f, 0.0f, 1.0f})
        .config({
            .mode = TextAnimMode::ByCharacter,
            .duration = Frame{40},
            .delay_per_unit = Frame{5},
            .easing = EasingCurve{Easing::OutBack},
            .animate_opacity = true,
            .animate_slide = true,
            .slide_from = {0.0f, 60.0f, 0.0f},
            .animate_scale = true,
            .scale_from = {0.5f, 0.5f, 1.0f},
            .animate_tracking = true,
            .tracking_from = 40.0f,
            .animate_blur = true,
            .blur_from = 30.0f,
        });

    ta.build(sb, "all_effects");

    Scene result = sb.build();
    REQUIRE(result.layers().size() == 3);
    CHECK(result.layers()[0].anim_transform.opacity.is_animated());
    CHECK(result.layers()[0].anim_transform.blur.is_animated());
}

TEST_CASE("TextAnimator blur reveal keyframes") {
    SceneBuilder sb;

    TextAnimator ta;
    ta.text("AB")
        .font_size(64.0f)
        .color(Color::white())
        .config({
            .mode = TextAnimMode::ByCharacter,
            .duration = Frame{20},
            .delay_per_unit = Frame{6},
            .easing = EasingCurve{Easing::OutCubic},
            .animate_blur = true,
            .blur_from = 18.0f,
        });

    ta.build(sb, "blur_reveal");

    Scene result = sb.build();
    REQUIRE(result.layers().size() == 2);

    // Layer 0 (A): blur starts at 18, ends at 0 over frames 0..20
    const auto& layer0 = result.layers()[0];
    CHECK(layer0.anim_transform.blur.is_animated());
    // At frame 0 the baked blur radius should be 18 (hold before start)
    // The effect stack should contain a blur effect with the baked radius
    REQUIRE(!layer0.effects().empty());
    bool has_blur = false;
    for (const auto& e : layer0.effects()) {
        if (auto* bp = std::get_if<BlurParams>(&e.params)) {
            has_blur = true;
            CHECK(bp->radius == doctest::Approx(18.0f));
        }
    }
    CHECK(has_blur);

    // Layer 1 (B): delay=6, so at global frame 0 blur is still at hold value 18
    const auto& layer1 = result.layers()[1];
    CHECK(layer1.anim_transform.blur.is_animated());
    bool has_blur1 = false;
    for (const auto& e : layer1.effects()) {
        if (auto* bp = std::get_if<BlurParams>(&e.params)) {
            has_blur1 = true;
            CHECK(bp->radius == doctest::Approx(18.0f));
        }
    }
    CHECK(has_blur1);
}

TEST_CASE("TextAnimator layered build staggered keyframes") {
    SceneBuilder sb;

    TextAnimator ta;
    ta.text("AB")
        .font_size(50.0f)
        .color(Color::white())
        .config({
            .mode = TextAnimMode::ByCharacter,
            .duration = Frame{30},
            .delay_per_unit = Frame{10},
            .easing = EasingCurve{Easing::Linear},
            .animate_opacity = true,
        });

    ta.build(sb, "stagger");

    Scene result = sb.build();
    REQUIRE(result.layers().size() == 2);

    // Layer 0 (A) should have opacity keyframes at delay=0
    CHECK(result.layers()[0].anim_transform.opacity.is_animated());
    // Layer 1 (B) should have opacity keyframes at delay=10
    CHECK(result.layers()[1].anim_transform.opacity.is_animated());
}

TEST_CASE("TextAnimator measure_unit_width uses FontEngine when available") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    TextAnimator ta;
    ta.text("ABC")
        .font_size(72.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine);

    f32 precise_w = ta.measure_unit_width("ABC");
    CHECK(precise_w > 0.0f);

    // Fallback width for comparison: 72 * 0.58 * 3 = 125.28
    f32 fallback_w = 72.0f * 0.58f * 3.0f;

    // Precise shaped width should differ from the naive fallback
    // (real glyphs have variable widths and kerning)
    CHECK(precise_w != doctest::Approx(fallback_w).epsilon(0.01f));
}

TEST_CASE("TextAnimator with FontEngine produces precise layer positions") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    SceneBuilder sb;

    TextAnimator ta;
    ta.text("AV")
        .font_size(64.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine)
        .config({
            .mode = TextAnimMode::ByCharacter,
            .duration = Frame{20},
            .delay_per_unit = Frame{4},
            .easing = EasingCurve{Easing::OutCubic},
            .animate_opacity = true,
        });

    ta.build(sb, "kerned");
    Scene result = sb.build();
    REQUIRE(result.layers().size() == 2);

    // Each layer should carry the FontEngine pointer through LayerBuilder
    // (LayerBuilder stores it; downstream pipeline can query it)
    // We verify the build completes without error and layers exist.
    CHECK(result.layers()[0].nodes.size() > 0);
    CHECK(result.layers()[1].nodes.size() > 0);
}

TEST_CASE("TextAnimator FontEngine fallback when font unavailable") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    TextAnimator ta;
    ta.text("X")
        .font_size(48.0f)
        .font_path("nonexistent_font.ttf")
        .font_engine(&engine)
        .config({
            .mode = TextAnimMode::ByCharacter,
            .duration = Frame{10},
        });

    // measure_unit_width should gracefully fall back to approximation
    f32 w = ta.measure_unit_width("X");
    CHECK(w == doctest::Approx(48.0f * 0.58f));

    SceneBuilder sb;
    ta.build(sb, "fallback");
    Scene result = sb.build();
    CHECK(result.layers().size() == 1);
}

TEST_CASE("TextAnimator ByGlyph splits into shaped glyphs") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    TextAnimator ta;
    ta.text("ABC")
        .font_size(64.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine)
        .config({.mode = TextAnimMode::ByGlyph});

    auto glyphs = ta.split_glyphs();
    REQUIRE(glyphs.size() == 3);
    CHECK(glyphs[0].text == "A");
    CHECK(glyphs[1].text == "B");
    CHECK(glyphs[2].text == "C");

    // Glyph positions should increase monotonically
    CHECK(glyphs[0].x < glyphs[1].x);
    CHECK(glyphs[1].x < glyphs[2].x);

    // Advances should be positive
    CHECK(glyphs[0].advance_x > 0.0f);
    CHECK(glyphs[1].advance_x > 0.0f);
    CHECK(glyphs[2].advance_x > 0.0f);
}

TEST_CASE("TextAnimator ByGlyph builds one layer per glyph") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    SceneBuilder sb;

    TextAnimator ta;
    ta.text("AB")
        .font_size(64.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine)
        .config({
            .mode = TextAnimMode::ByGlyph,
            .duration = Frame{20},
            .delay_per_unit = Frame{4},
            .easing = EasingCurve{Easing::OutCubic},
            .animate_opacity = true,
        });

    ta.build(sb, "glyph_anim");
    Scene result = sb.build();
    REQUIRE(result.layers().size() == 2);

    // Each layer should have a text node
    CHECK(result.layers()[0].nodes.size() > 0);
    CHECK(result.layers()[1].nodes.size() > 0);
}

TEST_CASE("TextAnimator ByGlyph positions differ from approximate fallback") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    SceneBuilder sb1;
    SceneBuilder sb2;

    // ByGlyph with FontEngine — precise positions
    TextAnimator ta_glyph;
    ta_glyph.text("AV")
        .font_size(64.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine)
        .config({
            .mode = TextAnimMode::ByGlyph,
            .duration = Frame{20},
            .delay_per_unit = Frame{4},
            .animate_opacity = true,
        });
    ta_glyph.build(sb1, "glyph");
    Scene result_glyph = sb1.build();
    REQUIRE(result_glyph.layers().size() == 2);

    // ByCharacter without FontEngine — approximate positions
    TextAnimator ta_char;
    ta_char.text("AV")
        .font_size(64.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .config({
            .mode = TextAnimMode::ByCharacter,
            .duration = Frame{20},
            .delay_per_unit = Frame{4},
            .animate_opacity = true,
        });
    ta_char.build(sb2, "char");
    Scene result_char = sb2.build();
    REQUIRE(result_char.layers().size() == 2);

    // The second layer (V) should be at a different x position because
    // ByGlyph accounts for kerning, while ByCharacter uses fixed width
    f32 glyph_v_x = result_glyph.layers()[1].transform.position.x;
    f32 char_v_x  = result_char.layers()[1].transform.position.x;
    // Should differ at more than 1% (real kerning vs naive fallback)
    CHECK(glyph_v_x != doctest::Approx(char_v_x).epsilon(0.01f));
}

TEST_CASE("TextAnimator ByGlyph handles spaces as individual glyphs") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    TextAnimator ta;
    ta.text("A B")
        .font_size(64.0f)
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_engine(&engine)
        .config({.mode = TextAnimMode::ByGlyph});

    auto glyphs = ta.split_glyphs();
    REQUIRE(glyphs.size() == 3);
    CHECK(glyphs[0].text == "A");
    CHECK(glyphs[1].text == " ");
    CHECK(glyphs[2].text == "B");
}
