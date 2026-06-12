#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/font_engine.hpp>

using namespace chronon3d;

static float mock_char_width(const void* /*ctx*/, char /*c*/, float font_size) {
    return font_size * 0.6f;
}

static TextLayoutInput make_input(const char* text, float size = 20.0f) {
    TextLayoutInput input;
    input.text = text;
    input.style.size = size;
    input.char_width_fn = mock_char_width;
    return input;
}

// ═════════════════════════════════════════════════════════════════════════════
// 1. Basic Bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: basic width and height") {
    SUBCASE("Single char returns width ≈ 0.6 * font_size") {
        auto input = make_input("A", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(12.0f));           // 1 * 20 * 0.6
        CHECK(res.size.y == doctest::Approx(20.0f * 1.2f));    // 1 line * line_height * size
    }

    SUBCASE("Multiple chars sum width correctly") {
        auto input = make_input("ABC", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(36.0f));           // 3 * 20 * 0.6
        CHECK(res.size.y == doctest::Approx(24.0f));           // 1 * 20 * 1.2
    }

    SUBCASE("Empty text has zero width, one empty line") {
        auto input = make_input("", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(0.0f));
        REQUIRE(res.lines.size() == 1);
        CHECK(res.lines[0].text == "");
    }

    SUBCASE("Newlines multiply height by line count") {
        auto input = make_input("Line1\nLine2\nLine3", 10.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.y == doctest::Approx(3.0f * 10.0f * 1.2f));  // 3 lines
        REQUIRE(res.lines.size() == 3);
    }

    SUBCASE("Long text produces proportional width") {
        auto input = make_input("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(26.0f * 10.0f * 0.6f));
        CHECK(res.lines.size() == 1);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. TextAnchor Horizontal Alignment — Bounds & Line Positions
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: TextAnchor horizontal alignment") {
    SUBCASE("Anchor Left: all lines at x=0") {
        auto input = make_input("A\nLongLine", 10.0f);
        input.style.align = TextAlign::Left;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        CHECK(res.lines[0].position.x == doctest::Approx(0.0f));
        CHECK(res.lines[1].position.x == doctest::Approx(0.0f));
        // size.x should be the max line width (LongLine)
        CHECK(res.size.x > res.lines[0].width);
    }

    SUBCASE("Anchor Center: short lines offset inward") {
        auto input = make_input("A\nLongLine", 10.0f);
        input.style.align = TextAlign::Center;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        // max_width = width of "LongLine" = 8 * 10 * 0.6 = 48
        // line 0 width = 6, offset = (48 - 6) / 2 = 21
        CHECK(res.lines[0].position.x == doctest::Approx(21.0f));
        // line 1 width = 48, offset = 0
        CHECK(res.lines[1].position.x == doctest::Approx(0.0f));
        CHECK(res.size.x == doctest::Approx(48.0f));
    }

    SUBCASE("Anchor Right: short lines shifted right") {
        auto input = make_input("A\nLongLine", 10.0f);
        input.style.align = TextAlign::Right;

        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        // max_width = 48, line 0 width = 6, offset = 48 - 6 = 42
        CHECK(res.lines[0].position.x == doctest::Approx(42.0f));
        // line 1 width = 48, offset = 0
        CHECK(res.lines[1].position.x == doctest::Approx(0.0f));
    }

    SUBCASE("Anchor alignment works identically to legacy align field") {
        auto input_anchor = make_input("Hello\nWorld", 10.0f);
        input_anchor.style.anchor.align = TextAlign::Center;

        auto input_legacy = make_input("Hello\nWorld", 10.0f);
        input_legacy.style.align = TextAlign::Center;

        auto res_anchor = TextLayoutEngine::layout(input_anchor);
        auto res_legacy = TextLayoutEngine::layout(input_legacy);

        CHECK(res_anchor.size.x == doctest::Approx(res_legacy.size.x));
        REQUIRE(res_anchor.lines.size() == res_legacy.lines.size());
        for (size_t i = 0; i < res_anchor.lines.size(); ++i) {
            CHECK(res_anchor.lines[i].position.x ==
                  doctest::Approx(res_legacy.lines[i].position.x));
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Wrapping Bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: word wrap") {
    SUBCASE("Word wrap constrains total width to box width") {
        auto input = make_input("Hello World Wide Web", 10.0f);
        input.style.wrap = TextWrap::Word;
        input.box.enabled = true;
        input.box.size = {30.0f, 100.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() > 1);
        CHECK(res.size.x <= 30.0f);
    }

    SUBCASE("Character wrap produces more lines than word wrap for long words") {
        auto input_word = make_input("Supercalifragilisticexpialidocious", 10.0f);
        input_word.style.wrap = TextWrap::Word;
        input_word.box.enabled = true;
        input_word.box.size = {40.0f, 200.0f};

        auto input_char = make_input("Supercalifragilisticexpialidocious", 10.0f);
        input_char.style.wrap = TextWrap::Character;
        input_char.box.enabled = true;
        input_char.box.size = {40.0f, 200.0f};

        auto res_word = TextLayoutEngine::layout(input_word);
        auto res_char = TextLayoutEngine::layout(input_char);

        // Character wrap should produce at least as many lines
        CHECK(res_char.lines.size() >= res_word.lines.size());
    }

    SUBCASE("No wrap: all text on one line regardless of box width") {
        auto input = make_input("Hello World", 10.0f);
        input.style.wrap = TextWrap::None;
        input.box.enabled = true;
        input.box.size = {10.0f, 10.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 1);
        CHECK(res.size.x > 10.0f);  // overflows box
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Tracking Bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: tracking affects width") {
    SUBCASE("Positive tracking increases total width") {
        auto input = make_input("ABC", 10.0f);
        auto res_base = TextLayoutEngine::layout(input);

        input.style.tracking = 5.0f;
        auto res_tracked = TextLayoutEngine::layout(input);

        // Base width = 3 * 10 * 0.6 = 18
        // With tracking = 18 + 3 * 5 = 33
        CHECK(res_base.size.x == doctest::Approx(18.0f));
        CHECK(res_tracked.size.x == doctest::Approx(33.0f));
    }

    SUBCASE("Negative tracking reduces width") {
        auto input = make_input("ABC", 10.0f);
        input.style.tracking = -2.0f;
        auto res = TextLayoutEngine::layout(input);
        // 18 - 3 * 2 = 12
        CHECK(res.size.x == doctest::Approx(12.0f));
    }

    SUBCASE("Tracking is per code-point") {
        // Two ASCII chars + tracking per code-point
        // 2 * (10 * 0.6) + 2 * 10 = 12 + 20 = 32
        auto input = make_input("AB", 10.0f);
        input.style.tracking = 10.0f;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(32.0f));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Line Height Bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: line height scales vertical size") {
    SUBCASE("Default line_height gives height = font_size * 1.2") {
        auto input = make_input("A\nB", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.y == doctest::Approx(2.0f * 20.0f * 1.2f));  // 2 * 24 = 48
    }

    SUBCASE("line_height = 1.0 gives tighter height") {
        auto input = make_input("A\nB", 20.0f);
        input.style.line_height = 1.0f;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.y == doctest::Approx(40.0f));  // 2 * 20
    }

    SUBCASE("line_height = 2.0 gives double height") {
        auto input = make_input("A\nB", 20.0f);
        input.style.line_height = 2.0f;
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.y == doctest::Approx(80.0f));  // 2 * 40
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Auto-Fit Bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: auto-fit shrinks font to fit box") {
    SUBCASE("Long text auto-fits to narrow box") {
        auto input = make_input("ExtremelyLongSentenceThatDoesNotFit", 100.0f);
        input.style.auto_fit = true;
        input.style.min_size = 8.0f;
        input.box.enabled = true;
        input.box.size = {80.0f, 50.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x <= 80.0f);
        CHECK(res.font_size < 100.0f);  // font was shrunk
    }

    SUBCASE("Short text does not shrink with auto-fit") {
        auto input = make_input("Hi", 100.0f);
        input.style.auto_fit = true;
        input.style.min_size = 8.0f;
        input.box.enabled = true;
        input.box.size = {500.0f, 200.0f};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.font_size == doctest::Approx(100.0f));  // unchanged
        CHECK(res.size.x <= 500.0f);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Per-Line Bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: per-line metrics") {
    SUBCASE("Each line reports its exact width") {
        auto input = make_input("A\nBB\nCCC", 10.0f);
        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 3);
        CHECK(res.lines[0].width == doctest::Approx(6.0f));   // 1 * 10 * 0.6
        CHECK(res.lines[1].width == doctest::Approx(12.0f));  // 2 * 10 * 0.6
        CHECK(res.lines[2].width == doctest::Approx(18.0f));  // 3 * 10 * 0.6
    }

    SUBCASE("Each line has non-zero ascent and descent") {
        auto input = make_input("Hello\nWorld", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 2);
        for (const auto& line : res.lines) {
            CHECK(line.ascent > 0.0f);
            CHECK(line.descent > 0.0f);
            CHECK(line.baseline > 0.0f);
        }
    }

    SUBCASE("Line positions are stacked vertically") {
        auto input = make_input("A\nB\nC", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        REQUIRE(res.lines.size() == 3);
        CHECK(res.lines[0].position.y == doctest::Approx(0.0f));
        CHECK(res.lines[1].position.y == doctest::Approx(24.0f));  // 1 * 20 * 1.2
        CHECK(res.lines[2].position.y == doctest::Approx(48.0f));  // 2 * 20 * 1.2
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. FontEngine Bounds — Real Metrics
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: FontEngine real metrics") {
    FontEngine engine;
    if (!engine.can_load({"assets/fonts/Inter-Bold.ttf", "Inter", 700})) {
        MESSAGE("Skipping: Inter-Bold.ttf not available");
        return;
    }

    SUBCASE("Real text width differs from mock approximation") {
        TextLayoutInput input;
        input.text = "Hello";
        input.style.size = 32.0f;
        input.font_engine = &engine;
        input.font_spec = {"assets/fonts/Inter-Bold.ttf", "Inter", 700};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x > 0.0f);

        // Mock would give 5 * 32 * 0.6 = 96 px
        float mock_w = 5.0f * 32.0f * 0.6f;
        CHECK(res.size.x != doctest::Approx(mock_w).epsilon(0.01f));
    }

    SUBCASE("Total height scales with font size using real metrics") {
        TextLayoutInput input;
        input.text = "Line1\nLine2";
        input.style.size = 24.0f;
        input.font_engine = &engine;
        input.font_spec = {"assets/fonts/Inter-Bold.ttf", "Inter", 700};

        auto res = TextLayoutEngine::layout(input);
        CHECK(res.lines.size() == 2);
        // Height should be 2 lines * 24 * 1.2 = 57.6
        CHECK(res.size.y == doctest::Approx(57.6f).epsilon(0.1f));
    }

    SUBCASE("Tracking with real font metrics") {
        TextLayoutInput input;
        input.text = "ABC";
        input.style.size = 20.0f;
        input.font_engine = &engine;
        input.font_spec = {"assets/fonts/Inter-Bold.ttf", "Inter", 700};

        auto no_track = TextLayoutEngine::layout(input);

        input.style.tracking = 10.0f;
        auto with_track = TextLayoutEngine::layout(input);

        CHECK(with_track.size.x > no_track.size.x);
        // Tracking adds 3 * 10 = 30 px to the width
        CHECK(with_track.size.x == doctest::Approx(no_track.size.x + 30.0f).epsilon(0.5f));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. TextAnchor Padding (reserved for future use)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: TextAnchor padding is stored but inert") {
    // The padding field exists on TextAnchor but is not yet applied
    // by the layout engine or rasterizer.  These tests verify it does
    // not cause crashes or unexpected behavior when set.
    //
    // When padding is activated in a future change, these tests should
    // be updated to verify that bounds expand by the padding amount.

    SUBCASE("Setting padding does not crash or change layout") {
        auto input_no_pad = make_input("Hello", 20.0f);
        auto res_no_pad = TextLayoutEngine::layout(input_no_pad);

        auto input_pad = make_input("Hello", 20.0f);
        input_pad.style.anchor.padding = {10.0f, 5.0f};
        auto res_pad = TextLayoutEngine::layout(input_pad);

        // Currently padding is not applied — bounds should be identical
        CHECK(res_pad.size.x == doctest::Approx(res_no_pad.size.x));
        CHECK(res_pad.size.y == doctest::Approx(res_no_pad.size.y));
        REQUIRE(res_pad.lines.size() == res_no_pad.lines.size());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. Edge Cases
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TextLayoutEngine bounds: edge cases") {
    SUBCASE("Single space returns non-zero width") {
        auto input = make_input(" ", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(12.0f));  // 1 * 20 * 0.6
        CHECK(res.lines.size() == 1);
    }

    SUBCASE("Special characters (tabs, newlines) produce reasonable bounds") {
        auto input = make_input("\t\n\r", 20.0f);
        auto res = TextLayoutEngine::layout(input);
        // Should not crash; should return some lines
        CHECK(res.lines.size() >= 1);
        CHECK(res.size.y > 0.0f);
    }

    SUBCASE("Very small font size works") {
        auto input = make_input("Tiny", 1.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(4.0f * 1.0f * 0.6f));
        CHECK(res.size.y > 0.0f);
    }

    SUBCASE("Very large font size works") {
        auto input = make_input("Big", 1000.0f);
        auto res = TextLayoutEngine::layout(input);
        CHECK(res.size.x == doctest::Approx(3.0f * 1000.0f * 0.6f));
        CHECK(res.size.y == doctest::Approx(1000.0f * 1.2f));
    }

    SUBCASE("Mixed single and multi-byte characters (UTF-8 safety)") {
        // ASCII + 2-byte + 3-byte sequences
        auto input = make_input("A\xC3\xA9\xE2\x82\xAC", 20.0f);  // A, é, €
        auto res = TextLayoutEngine::layout(input);
        // Should not crash; 3 code-points
        CHECK(res.lines.size() == 1);
        CHECK(res.size.x > 0.0f);
    }

    SUBCASE("Very long single word with word wrap falls back to char wrap") {
        auto input = make_input("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 10.0f);
        input.style.wrap = TextWrap::Word;
        input.box.enabled = true;
        input.box.size = {30.0f, 200.0f};

        auto res = TextLayoutEngine::layout(input);
        // Word wrap falls back to character-level wrap for overlong tokens
        CHECK(res.lines.size() > 1);
        CHECK(res.size.x <= 30.0f);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 11. FontEngine Glyph Bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FontEngine glyph bounds: per-glyph bbox correctness") {
    FontEngine engine;
    if (!engine.can_load({"assets/fonts/Inter-Bold.ttf", "Inter", 700})) {
        MESSAGE("Skipping: Inter-Bold.ttf not available");
        return;
    }

    SUBCASE("Every glyph has a valid bounding box") {
        auto run = engine.shape_text("ABCDEFGHIJ", {"assets/fonts/Inter-Bold.ttf", "Inter", 700}, 32.0f);
        REQUIRE(run.has_value());

        for (const auto& g : run->glyphs) {
            CHECK(g.bbox_x0 < g.bbox_x1);  // positive width
            CHECK(g.bbox_y1 < g.bbox_y0);  // y grows downward in FT coords, so top < bottom
            CHECK(g.advance_x > 0.0f);
        }
    }

    SUBCASE("Glyph bbox dimensions scale with font size") {
        auto run_small = engine.shape_text("A", {"assets/fonts/Inter-Bold.ttf", "Inter", 700}, 16.0f);
        auto run_large = engine.shape_text("A", {"assets/fonts/Inter-Bold.ttf", "Inter", 700}, 64.0f);

        REQUIRE(run_small.has_value());
        REQUIRE(run_large.has_value());
        REQUIRE(!run_small->glyphs.empty());
        REQUIRE(!run_large->glyphs.empty());

        float small_w = run_small->glyphs[0].bbox_x1 - run_small->glyphs[0].bbox_x0;
        float large_w = run_large->glyphs[0].bbox_x1 - run_large->glyphs[0].bbox_x0;

        CHECK(large_w > small_w * 3.0f);  // 64/16 = 4, allow some tolerance
    }

    SUBCASE("Glyph run width equals sum of advances") {
        auto run = engine.shape_text("ABC", {"assets/fonts/Inter-Bold.ttf", "Inter", 700}, 32.0f);
        REQUIRE(run.has_value());

        float sum_adv = 0.0f;
        for (const auto& g : run->glyphs) {
            sum_adv += g.advance_x;
        }
        CHECK(run->width == doctest::Approx(sum_adv).epsilon(0.5f));
    }

    SUBCASE("Kerning reduces total width for AV pair") {
        auto run_av = engine.shape_text("AV", {"assets/fonts/Inter-Bold.ttf", "Inter", 700}, 32.0f);
        auto run_a = engine.shape_text("A", {"assets/fonts/Inter-Bold.ttf", "Inter", 700}, 32.0f);
        auto run_v = engine.shape_text("V", {"assets/fonts/Inter-Bold.ttf", "Inter", 700}, 32.0f);

        REQUIRE(run_av.has_value());
        REQUIRE(run_a.has_value());
        REQUIRE(run_v.has_value());

        float separate = run_a->width + run_v->width;
        float together = run_av->width;

        // With kerning, AV together should be <= separate
        CHECK(together <= separate);
    }
}

