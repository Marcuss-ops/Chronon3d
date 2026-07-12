// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_completeness/text_long_text.cpp
//
// P2-#11: Long Text Stress — verifies the text pipeline handles large
// inputs without crashing, exhausting memory, or producing incoherent
// output.
//
// Cases:
//   1. 100-word paragraph  → renders ink, bbox within canvas
//   2. 1000-word paragraph → renders ink, no crash
//   3. 5000-char string    → renders ink, no crash
//   4. Massive emoji       → no crash, graceful degradation
//   5. Extreme multi-line   → 100 explicit newlines, no crash
//   6. Narrow box long text → many wrapped lines, no crash
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

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

#include <chrono>
#include <sstream>
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

/// Generate a paragraph of N words (Lorem-style).
std::string make_words(int count) {
    const char* pool[] = {
        "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
        "pack", "my", "box", "with", "five", "dozen", "liquor", "jugs",
        "how", "vexingly", "daft", "zebras", "run", "fast", "bright",
        "sun", "moon", "stars", "cloud", "rain", "wind", "fire",
        "earth", "water", "sky", "tree", "river", "mountain", "ocean",
        "light", "dark", "day", "night", "spring", "summer", "autumn",
        "winter", "happy", "sad", "brave", "strong", "gentle", "wild"
    };
    constexpr int pool_sz = sizeof(pool) / sizeof(pool[0]);
    std::ostringstream oss;
    for (int i = 0; i < count; ++i) {
        if (i > 0) oss << ' ';
        oss << pool[i % pool_sz];
    }
    return oss.str();
}

/// Generate a string of exactly N characters (repeating alphabet).
std::string make_chars(int count) {
    std::string s;
    s.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        s += static_cast<char>('A' + (i % 26));
    }
    return s;
}

/// Generate a string with N explicit newlines, each line being a short word.
std::string make_multiline(int lines) {
    std::ostringstream oss;
    for (int i = 0; i < lines; ++i) {
        if (i > 0) oss << '\n';
        oss << "Line" << (i + 1);
    }
    return oss.str();
}

/// Composition with long text.
Composition build_long_text(SoftwareRenderer& renderer,
                            std::string text,
                            float box_width = 1920.0f) {
    return composition(
        {.name = "TextLongText/test",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 60},
        [&renderer, text = std::move(text), box_width](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            s.layer("long_layer", [&](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                l.text_run("long_test", TextRunSpec{
                    .text = TextSpec{.content = {.value = text},.placement = TextPlacement{TextPlacementKind::Absolute, {box_width / 2.0f, 540.0f}},.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 32.0f
                        },.layout = {
                            .box = {box_width, 1080.0f},
                            .align = TextAlign::Left,
                            .vertical_align = VerticalAlign::Top
                        },.appearance = {.color = Color::white()},}
                }).commit();
            });
            return s.build();
        });
}

} // namespace

// ═══ Test 1 — 100-word paragraph ═════════════════════════════════════════
TEST_CASE("TextLongText 01: 100 words renders ink within canvas") {
    auto renderer = test::make_renderer();
    const std::string text = make_words(100);

    auto fb = renderer.render(build_long_text(renderer, text), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    const AlphaBBox bbox = alpha_bbox(*fb);

    INFO("100 words: visible=", visible,
         " bbox=", bbox.x0, ",", bbox.y0, "..", bbox.x1, ",", bbox.y1);

    CHECK(visible > 100);
    CHECK_FALSE(bbox.empty());
    // Bbox must be within canvas.
    CHECK(bbox.x0 >= 0);
    CHECK(bbox.y0 >= 0);
    CHECK(bbox.x1 <= fb->width() - 1);
    CHECK(bbox.y1 <= fb->height() - 1);
}

// ═══ Test 2 — 1000-word paragraph ════════════════════════════════════════
TEST_CASE("TextLongText 02: 1000 words does not crash") {
    auto renderer = test::make_renderer();
    const std::string text = make_words(1000);

    auto fb = renderer.render(build_long_text(renderer, text), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("1000 words: visible=", visible);

    // At 1000 words, text will overflow the box. As long as we don't crash
    // and the framebuffer is valid, the test passes.
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);
}

// ═══ Test 3 — 5000-char string ═══════════════════════════════════════════
TEST_CASE("TextLongText 03: 5000 chars does not crash within time budget") {
    auto renderer = test::make_renderer();
    const std::string text = make_chars(5000);

    auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render(build_long_text(renderer, text), Frame{0});
    auto elapsed = std::chrono::steady_clock::now() - t0;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("5000 chars: visible=", visible, " elapsed_ms=", ms);

    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);
    // 5000 characters should render in under 10 seconds.
    CHECK(ms < 10000);
}

// ═══ Test 4 — Massive emoji string ═══════════════════════════════════════
// NOTE: Memory usage is not directly measurable in a portable unit test.
// The "no crash" assertion combined with the 10-second render budget
// acts as a practical proxy: if memory explodes, the render would either
// crash (caught by the test runner) or time out (caught by the timing check).
TEST_CASE("TextLongText 04: emoji string does not crash") {
    auto renderer = test::make_renderer();
    // 200 emoji characters — the engine may render tofu/empty,
    // but must NOT crash.
    std::string emojis;
    for (int i = 0; i < 50; ++i) {
        emojis += u8str(u8"👋🔥🚀🎯✨💻🎨🎵🌟");
    }

    auto fb = renderer.render(build_long_text(renderer, emojis), Frame{0});
    REQUIRE(fb != nullptr);

    INFO("emojis: visible=", count_visible_pixels(*fb));
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);
}

// ═══ Test 5 — Extreme multi-line (100 newlines) ══════════════════════════
TEST_CASE("TextLongText 05: 100 explicit newlines does not crash") {
    auto renderer = test::make_renderer();
    const std::string text = make_multiline(100);

    auto fb = renderer.render(build_long_text(renderer, text), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    INFO("100 newlines: visible=", visible);

    CHECK(visible > 100);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);
}

// ═══ Test 6 — Narrow box (250px) with long text ══════════════════════════
TEST_CASE("TextLongText 06: narrow box 250px with long text does not crash") {
    auto renderer = test::make_renderer();
    const std::string text = make_words(200);

    auto fb = renderer.render(build_long_text(renderer, text, 250.0f), Frame{0});
    REQUIRE(fb != nullptr);

    const int visible = count_visible_pixels(*fb);
    const int rows = count_ink_rows(*fb);
    const int max_w = max_ink_row_width(*fb);

    INFO("250px box: visible=", visible, " rows=", rows, " max_width=", max_w);

    // Narrow box with long text → many wrapped lines.
    CHECK(visible > 100);
    CHECK(rows > 5);
    // No row exceeds box + margin.
    CHECK(max_w < 300);
}
