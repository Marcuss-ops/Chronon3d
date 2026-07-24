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
