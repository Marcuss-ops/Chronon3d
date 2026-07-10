// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_unicode.cpp
//
// P0-5/6: Unicode Support + Font Fallback — verifies that the text
// engine handles various Unicode scripts and missing fonts gracefully.
//
// Cases:
//   1. European accented (Caffè déjà vu) → visible ink with Inter-Bold
//   2. Cyrillic (Привет мир)             → visible ink with Inter-Bold
//   3. Arabic (مرحبا بالعالم)             → visible ink with NotoNaskhArabic
//   4. CJK without CJK font              → no crash, may produce no ink
//   5. Non-existent font path             → no crash, may produce no ink
//   6. Emoji without emoji font           → no crash, may produce no ink
//   7. Mixed scripts                      → no crash, at least Latin visible
//
// Principle: the engine MUST NOT crash on any Unicode input.  Missing
// glyphs are acceptable (tofu squares or empty space); crashes are not.
//
// NOTE: We use reinterpret_cast<const char*>(u8"...") because C++20
// u8"" literals produce char8_t* which doesn't convert to string_view.
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

// Helper: u8"" → std::string (C++20 u8 literals produce char8_t*, not char*)
template <std::size_t N>
std::string u8str(const char8_t (&s)[N]) {
    return std::string(reinterpret_cast<const char*>(s), N - 1);
}

Composition build_unicode_composition(
    SoftwareRenderer& renderer,
    std::string_view text,
    std::string_view font_path = "assets/fonts/Inter-Bold.ttf",
    float font_size = 72.0f
) {
    return composition(
        {.name = "TextUnicode/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text, font_path, font_size](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("unicode_layer", [&renderer, text, font_path, font_size](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("unicode_test", TextRunSpec{
                    .text = TextSpec{
                        .content = {.value = std::string{text}},
                        .font = {
                            .font_path = std::string{font_path},
                            .font_family = "default",
                            .font_weight = 700,
                            .font_size = font_size
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

// ═══ Test 1 — European accented characters ════════════════════════════════
TEST_CASE("TextUnicode 01: European accented characters render ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_unicode_composition(renderer,
            u8str(u8"Caffè déjà vu Olá ação coração")),
        Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("visible_pixels=", visible);
    CHECK(visible > 500);
}

// ═══ Test 2 — Cyrillic script ═════════════════════════════════════════════
TEST_CASE("TextUnicode 02: Cyrillic script renders ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_unicode_composition(renderer, u8str(u8"Привет мир")),
        Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("visible_pixels=", visible);
    CHECK(visible > 200);
}

// ═══ Test 3 — Arabic script with Arabic font ══════════════════════════════
TEST_CASE("TextUnicode 03: Arabic script with Arabic font renders ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_unicode_composition(renderer,
            u8str(u8"مرحبا بالعالم"),
            "assets/fonts/NotoNaskhArabic-Bold.ttf"),
        Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("visible_pixels=", visible);
    CHECK(visible > 200);
}

// ═══ Test 4 — CJK without CJK font ═══════════════════════════════════════
TEST_CASE("TextUnicode 04: CJK without CJK font does not crash") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_unicode_composition(renderer, u8str(u8"你好世界 こんにちは世界")),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1920);
    REQUIRE(fb->height() == 1080);

    const int visible = count_visible_pixels(*fb);
    INFO("visible_pixels=", visible, " (may be 0 if CJK glyphs missing)");
}

// ═══ Test 5 — Non-existent font path ══════════════════════════════════════
// The pipeline may throw when the font file doesn't exist (text_run_shape
// is null).  This is acceptable — we verify the engine doesn't crash with
// a segfault or infinite loop.  A thrown exception is a graceful failure.
TEST_CASE("TextUnicode 05: non-existent font path does not crash") {
    auto renderer = test::make_renderer();
    // The pipeline may throw — catch it and verify it's a graceful error.
    try {
        auto fb = renderer.render(
            build_unicode_composition(renderer,
                "Hello World",
                "assets/fonts/DOES_NOT_EXIST.ttf"),
            Frame{0});
        if (fb != nullptr) {
            const int visible = count_visible_pixels(*fb);
            INFO("visible_pixels=", visible);
            // Font not found may produce no ink — that's OK.
        }
    } catch (const std::exception& e) {
        INFO("Exception caught (graceful): ", e.what());
        // Throwing is acceptable — the engine didn't crash.
    } catch (...) {
        INFO("Unknown exception caught (graceful)");
    }
    // Test passes as long as we didn't segfault or hang.
}

// ═══ Test 6 — Emoji without emoji font ════════════════════════════════════
TEST_CASE("TextUnicode 06: emoji without emoji font does not crash") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_unicode_composition(renderer, u8str(u8"👋🔥🚀🎯✨")),
        Frame{0});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == 1920);
    REQUIRE(fb->height() == 1080);

    const int visible = count_visible_pixels(*fb);
    INFO("visible_pixels=", visible, " (may be 0 if emoji glyphs missing)");
}

// ═══ Test 7 — Mixed scripts ═══════════════════════════════════════════════
TEST_CASE("TextUnicode 07: mixed scripts with Latin renders some ink") {
    auto renderer = test::make_renderer();
    auto fb = renderer.render(
        build_unicode_composition(renderer,
            u8str(u8"Hello Привет 你好 Caffè")),
        Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("visible_pixels=", visible);
    CHECK(visible > 100);
}
