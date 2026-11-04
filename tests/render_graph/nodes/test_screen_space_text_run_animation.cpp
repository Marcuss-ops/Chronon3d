// SPDX-License-Identifier: MIT
//
// Screen-space TextRun animation regression.
//
// A screen-space text layer is commonly marked `cache_static(true)` because
// its layer transform never changes.  That flag says nothing about the glyph
// driver: a TextRun with an animator stack rewrites `shape.glyphs` every
// frame.  Before the fix the source pass keyed the TextRun node at Frame{0}
// whenever the *layer* was static, so the node cache replayed the first glyph
// state forever and the overlay looked like a still PNG.
//
// This test renders the same TextRun at frames 0 and 30:
//   - word animator: ink pixels in the animated bbox MUST differ;
//   - static control: pixels in its bbox MUST be identical.

#include <doctest/doctest.h>

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

constexpr int kWidth = 960;
constexpr int kHeight = 540;
constexpr int kFps = 24;

struct InkBounds {
    int x0{std::numeric_limits<int>::max()};
    int y0{std::numeric_limits<int>::max()};
    int x1{-1};
    int y1{-1};

    [[nodiscard]] bool valid() const { return x1 >= x0 && y1 >= y0; }
};

bool is_ink(const Color& c) {
    return (c.r + c.g + c.b) / 3.0f > 0.02f;
}

InkBounds scan_ink(const Framebuffer& fb, int y_begin, int y_end) {
    InkBounds b;
    for (int y = std::max(0, y_begin); y < std::min(fb.height(), y_end); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (!is_ink(fb.get_pixel(x, y))) continue;
            b.x0 = std::min(b.x0, x);
            b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x);
            b.y1 = std::max(b.y1, y);
        }
    }
    return b;
}

InkBounds merge(InkBounds a, InkBounds b) {
    if (!a.valid()) return b;
    if (!b.valid()) return a;
    return InkBounds{
        std::min(a.x0, b.x0),
        std::min(a.y0, b.y0),
        std::max(a.x1, b.x1),
        std::max(a.y1, b.y1),
    };
}

InkBounds expand(InkBounds b, int px) {
    return InkBounds{b.x0 - px, b.y0 - px, b.x1 + px, b.y1 + px};
}

bool differs(const Color& a, const Color& b) {
    return std::abs(a.r - b.r) > 1e-5f ||
           std::abs(a.g - b.g) > 1e-5f ||
           std::abs(a.b - b.b) > 1e-5f ||
           std::abs(a.a - b.a) > 1e-5f;
}

int differing_pixels(const Framebuffer& a, const Framebuffer& b, InkBounds region) {
    if (!region.valid()) return 0;
    int changed = 0;
    for (int y = std::max(0, region.y0); y <= std::min(a.height() - 1, region.y1); ++y) {
        for (int x = std::max(0, region.x0); x <= std::min(a.width() - 1, region.x1); ++x) {
            if (differs(a.get_pixel(x, y), b.get_pixel(x, y))) ++changed;
        }
    }
    return changed;
}

PreparedText make_text(const char* utf8, Vec2 pin, float font_size) {
    PreparedText text;
    text.document.utf8 = utf8;
    text.style.font = FontSpec{
        .font_path = "assets/fonts/Inter-Bold.ttf",
        .font_family = "Inter",
        .font_weight = 700,
        .font_size = font_size,
    };
    text.style.color = Color::white();
    text.style.paint.fill = text.style.color;
    text.frame.size = {900.0f, 150.0f};
    text.frame.placement = TextPlacement{TextPlacementKind::Absolute, pin};
    text.frame.anchor = TextAnchor::Center;
    text.frame.align = TextAlign::Center;
    text.frame.vertical_align = VerticalAlign::Middle;
    return text;
}

Composition make_composition(SoftwareRenderer& renderer) {
    return composition(
        CompositionSpec{
            .name = "ScreenSpaceTextRunAnimation",
            .width = kWidth,
            .height = kHeight,
            .frame_rate = FrameRate{kFps, 1},
            .duration = Frame{60},
        },
        [&renderer](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());

            s.layer("bg", [](LayerBuilder& l) {
                l.fill(Color::black());
            });

            s.screen_layer("animated_text_layer", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                PreparedText text = make_text("ANIMATED WORD", Vec2{480.0f, 170.0f}, 84.0f);

                TextAnimatorSpec lift;
                lift.id = "screen_space_word_lift";
                lift.enabled = true;
                lift.transform_mode = TextPropertyBlendMode::Add;

                GlyphSelectorSpec words;
                words.id = "screen_space_words";
                words.unit = TextSelectorUnit::Word;
                words.shape = TextSelectorShape::Square;
                words.order = TextSelectorOrder::Forward;
                words.start = {0.0f};
                words.end = {100.0f};
                words.amount = {100.0f};
                words.exclude_spaces = false;
                lift.selectors.push_back(std::move(words));

                PositionProperty position{AnimatedValue<Vec3>{Vec3{0.0f, 0.0f, 0.0f}}};
                position.value.add_keyframe(Frame{0}, Vec3{0.0f, 0.0f, 0.0f});
                position.value.add_keyframe(Frame{30}, Vec3{0.0f, -40.0f, 0.0f});
                lift.properties.push_back(std::move(position));
                text.animation.animators.push_back(std::move(lift));

                l.text_run("animated_text", std::move(text)).commit();
                // Reproduce the overlay layer contract: the layer transform is
                // static even though the TextRun's glyph driver is animated.
                l.cache_static(true);
            });

            s.screen_layer("static_text_layer", [&renderer](LayerBuilder& l) {
                l.font_engine(&renderer.font_engine());
                PreparedText text = make_text("STATIC CONTROL", Vec2{480.0f, 410.0f}, 64.0f);
                l.text_run("static_text", std::move(text)).commit();
                l.cache_static(true);
            });

            return s.build();
        });
}

} // namespace

TEST_CASE("Screen-space TextRun word animator changes pixels while static control does not") {
    auto renderer = test::make_renderer();
    auto comp = make_composition(renderer);

    auto fb0 = renderer.render(comp, Frame{0});
    auto fb30 = renderer.render(comp, Frame{30});
    REQUIRE(fb0 != nullptr);
    REQUIRE(fb30 != nullptr);
    REQUIRE(fb0->width() == kWidth);
    REQUIRE(fb0->height() == kHeight);

    // The animated run lives in the upper half; the static control in the
    // lower half.  Splitting by Y keeps the two bboxes independent.
    const InkBounds animated0 = scan_ink(*fb0, 0, 300);
    const InkBounds animated30 = scan_ink(*fb30, 0, 300);
    const InkBounds static0 = scan_ink(*fb0, 300, kHeight);
    const InkBounds static30 = scan_ink(*fb30, 300, kHeight);

    REQUIRE(animated0.valid());
    REQUIRE(animated30.valid());
    REQUIRE(static0.valid());
    REQUIRE(static30.valid());

    const InkBounds animated_region = expand(merge(animated0, animated30), 2);
    const InkBounds static_region = expand(static0, 2);

    INFO("animated frame0 bbox=", animated0.x0, ",", animated0.y0, "..",
         animated0.x1, ",", animated0.y1);
    INFO("animated frame30 bbox=", animated30.x0, ",", animated30.y0, "..",
         animated30.x1, ",", animated30.y1);

    CHECK(differing_pixels(*fb0, *fb30, animated_region) > 200);
    CHECK(differing_pixels(*fb0, *fb30, static_region) == 0);
}
