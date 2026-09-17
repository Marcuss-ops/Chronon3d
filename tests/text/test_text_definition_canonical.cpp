// SPDX-License-Identifier: MIT
// Canonical TextDefinition authoring contract.

#include <doctest/doctest.h>

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_document_builder.hpp>

using namespace chronon3d;

namespace {

TextDefinition make_definition() {
    return TextDefinition{
        .content = {.value = "Chronon3D"},
        .style = {
            .font = {.font_path = "assets/fonts/Poppins-Bold.ttf",
                     .font_family = "Poppins", .font_weight = 700,
                     .font_size = 72.0f},
            .color = Color::white(),
            .paint = {.stroke_enabled = true,
                      .stroke_color = Color::black(), .stroke_width = 3.0f},
        },
        .frame = {.size = {900.0f, 180.0f},
                  .anchor = TextAnchor::Center,
                  .align = TextAlign::Center,
                  .vertical_align = VerticalAlign::Middle,
                  .wrap = TextWrap::Word,
                  .overflow = TextOverflow::Clip,
                  .centering_mode = TextCenteringMode::PixelInk},
    };
}

} // namespace

TEST_CASE("TextDefinition canonical authoring preserves all authored fields") {
    const auto def = make_definition();

    CHECK(def.content.value == "Chronon3D");
    CHECK(def.style.font.font_family == "Poppins");
    CHECK(def.style.font.font_weight == 700);
    CHECK(def.style.font.font_size == doctest::Approx(72.0f));
    CHECK(def.style.paint.stroke_enabled);
    CHECK(def.style.paint.stroke_width == doctest::Approx(3.0f));
    CHECK(def.frame.size.x == doctest::Approx(900.0f));
    CHECK(def.frame.align == TextAlign::Center);
}

TEST_CASE("TextDefinition lowers to a usable TextDocument") {
    const auto def = make_definition();
    const auto document = to_text_document(def);

    CHECK(document.validate());
    REQUIRE(document.paragraphs.size() == 1);
    CHECK(document.utf8 == "Chronon3D");
}

TEST_CASE("TextDefinition lowering is deterministic") {
    const auto a = to_text_document(make_definition());
    const auto b = to_text_document(make_definition());

    CHECK(a.validate());
    CHECK(b.validate());
    CHECK(a.utf8 == b.utf8);
    CHECK(a.paragraphs.size() == b.paragraphs.size());
}

TEST_CASE("TextDefinition keeps fill, stroke and shadow as distinct semantics") {
    auto def = make_definition();
    def.style.paint.fill = Color{0.25f, 0.5f, 0.75f, 1.0f};
    def.style.paint.stroke_enabled = true;
    def.style.paint.stroke_color = Color{0.9f, 0.1f, 0.2f, 0.8f};
    def.style.paint.stroke_width = 4.0f;
    def.style.shadows.push_back(TextShadow{
        .enabled = true,
        .offset = {3.0f, 5.0f},
        .blur = 7.0f,
        .opacity = 0.6f,
        .color = Color{0.02f, 0.03f, 0.04f, 0.9f},
    });

    CHECK(def.style.paint.fill.r == doctest::Approx(0.25f));
    CHECK(def.style.paint.stroke_color.r == doctest::Approx(0.9f));
    CHECK(def.style.paint.stroke_width == doctest::Approx(4.0f));
    REQUIRE(def.style.shadows.size() == 1);
    CHECK(def.style.shadows.front().color.r == doctest::Approx(0.02f));
    CHECK(def.style.shadows.front().blur == doctest::Approx(7.0f));
    CHECK(def.style.shadows.front().opacity == doctest::Approx(0.6f));
    CHECK(def.style.paint.fill.r != def.style.paint.stroke_color.r);
    CHECK(def.style.paint.stroke_color.r != def.style.shadows.front().color.r);
}
