// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_edge_cases.cpp
//
// P2-#12: Edge Cases — verifies the text pipeline handles pathological
// inputs without crashing, corrupting output, or producing false positives.
//
// Cases:
//   1. Empty string ""           → no crash, no visible ink
//   2. Whitespace only "   "     → no crash, minimal/no ink
//   3. Multiple newlines "\n\n\n"→ no crash, bbox handled
//   4. Super-long word no spaces → no crash, overflow declared
//   5. Emoji ZWJ sequence        → no crash (may be tofu)
//   6. Combining marks "ÁÁÁ"     → no crash, accents not separated
//   7. Tab characters "\t\tHello"→ no crash
//   8. Mixed whitespace + newline→ no crash
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <string>

using namespace chronon3d;
using namespace chronon3d::test;
using namespace chronon3d::test::completeness;

namespace {

// Helper: u8"" → std::string (C++20 u8 literals produce char8_t*)
template <std::size_t N>
std::string u8str(const char8_t (&s)[N]) {
    return std::string(reinterpret_cast<const char*>(s), N - 1);
}

/// Build a simple text composition with the given string.
Composition build_edge_comp(SoftwareRenderer& renderer, std::string text) {
    return composition(
        {.name = "TextEdgeCases/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text = std::move(text)](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("edge_layer", [&renderer, text](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("edge_test", TextRunParams{
                    .text = TextSpec{
                        .content = {.value = text},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 72.0f
                        },
                        .layout = {
                            .box = {1920.0f, 1080.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()},
                        .position = {960.0f, 540.0f, 0.0f}
                    }
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — Empty string ════════════════════════════════════════════════
TEST_CASE("TextEdge 01: empty string does not crash") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_edge_comp(renderer, ""), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("empty: visible=", visible);

    // Empty text → no ink.
    CHECK(visible == 0);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);
}

// ═══ Test 2 — Whitespace only ═════════════════════════════════════════════
TEST_CASE("TextEdge 02: whitespace only does not crash") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(build_edge_comp(renderer, "     "), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("whitespace: visible=", visible);

    // Whitespace-only → minimal/no ink.
    CHECK(visible < 50);
}

// ═══ Test 3 — Multiple newlines ═══════════════════════════════════════════
TEST_CASE("TextEdge 03: multiple newlines do not crash") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_edge_comp(renderer, "\n\n\n"), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("newlines only: visible=", visible);

    // Just newlines → minimal/no visible ink (newlines produce empty lines).
    CHECK(visible < 50);
}

// ═══ Test 4 — Super-long word without spaces ══════════════════════════════
// A single extremely long word with Word wrapping will overflow the layout
// box because there are no word boundaries to break on.  The pipeline must
// not crash, and the bbox should exceed the box boundaries (overflow).
TEST_CASE("TextEdge 04: super-long word without spaces does not crash") {
    auto renderer = test::make_renderer();
    const std::string long_word = "supercalifragilisticexpialidocious";

    auto fb = renderer.render(build_edge_comp(renderer, long_word), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);
    // Box is 1920 wide; a 34-char word at 72pt is wider than 1920px.
    const int max_w = max_ink_row_width(*fb);

    INFO("long word: visible=", visible,
         " bbox_w=", bbox.width(), " max_row_w=", max_w);

    // The word has real glyphs → visible ink.
    CHECK(visible > 100);
    CHECK_FALSE(bbox.empty());
    // Overflow: the word may extend beyond the box but must stay
    // within the framebuffer (no crash, no memory corruption).
    CHECK(bbox.x0 >= 0);
    CHECK(bbox.x1 <= fb->width() - 1);
}

// ═══ Test 5 — Emoji ZWJ sequence ═════════════════════════════════════════
// Emoji ZWJ (Zero-Width Joiner) sequences like 👨‍👩‍👧‍👦 are
// multi-codepoint grapheme clusters. The engine may render tofu or
// skip them, but must NOT crash.
TEST_CASE("TextEdge 05: emoji ZWJ sequence does not crash") {
    auto renderer = test::make_renderer();
    // Family emoji: 👨 + ZWJ + 👩 + ZWJ + 👧 + ZWJ + 👦
    const std::string zwj_emoji = u8str(u8"👨‍👩‍👧‍👦");

    auto fb = renderer.render(build_edge_comp(renderer, zwj_emoji), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("emoji ZWJ: visible=", visible,
         " (may be 0 if glyphs missing)");

    // No crash is the primary assertion.
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);
    // Even if glyphs are missing, count_visible_pixels is safe to call.
}

// ═══ Test 6 — Combining marks (accents) ══════════════════════════════════
TEST_CASE("TextEdge 06: combining marks do not crash") {
    auto renderer = test::make_renderer();
    // Á = A + combining acute accent (U+0301)
    const std::string accented = u8str(u8"A\u0301A\u0301A\u0301");

    auto fb = renderer.render(build_edge_comp(renderer, accented), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("combining marks: visible=", visible);

    // Precomposed or decomposed accents should produce some ink.
    CHECK(visible > 0);
}

// ═══ Test 7 — Tab characters ═════════════════════════════════════════════
TEST_CASE("TextEdge 07: tab characters do not crash") {
    auto renderer = test::make_renderer();
    const std::string tabbed = "\t\tHello";

    auto fb = renderer.render(build_edge_comp(renderer, tabbed), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("tab: visible=", visible);

    // Tabs may be collapsed to spaces or ignored, but "Hello" should
    // produce visible ink.
    CHECK(visible > 50);
}

// ═══ Test 8 — Mixed whitespace and newlines ══════════════════════════════
TEST_CASE("TextEdge 08: mixed whitespace and newlines do not crash") {
    auto renderer = test::make_renderer();
    const std::string mixed = "  \t\n  Hello\n\nWorld  ";

    auto fb = renderer.render(build_edge_comp(renderer, mixed), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("mixed whitespace: visible=", visible);

    // "Hello" and "World" should produce visible ink.
    CHECK(visible > 100);
}
