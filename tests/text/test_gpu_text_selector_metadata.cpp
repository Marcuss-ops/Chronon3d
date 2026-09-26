#include <chronon3d/runtime/gpu_text_selector_metadata.hpp>

#include <doctest/doctest.h>

#include <array>

using namespace chronon3d;
using namespace chronon3d::runtime;

TEST_CASE("GPU text selector flags preserve ABI fields and pin bit positions") {
    constexpr std::uint32_t base = 0x0fu;
    constexpr auto packed = pack_text_selector_metadata(
        base, static_cast<std::uint32_t>(TextSelectorUnit::Word),
        static_cast<std::uint32_t>(TextSelectorOrder::FromCenter));
    CHECK(packed == (base | (static_cast<std::uint32_t>(TextSelectorUnit::Word) << 4u) |
                     (static_cast<std::uint32_t>(TextSelectorOrder::FromCenter) << 7u)));
    CHECK((packed & 0x0fu) == base);
    CHECK(((packed >> 4u) & 0x7u) == static_cast<std::uint32_t>(TextSelectorUnit::Word));
    CHECK(((packed >> 7u) & 0x7u) == static_cast<std::uint32_t>(TextSelectorOrder::FromCenter));

    constexpr auto no_wiggly_gpu_shape = pack_text_selector_metadata(
        base, static_cast<std::uint32_t>(TextSelectorUnit::Glyph),
        static_cast<std::uint32_t>(TextSelectorOrder::Forward),
        TextSelectorShape::Wiggly);
    CHECK(unpack_text_selector_shape(no_wiggly_gpu_shape) == TextSelectorShape::Square);

    constexpr auto glyph = pack_mtsdf_glyph_unit_flags(1u, 0x1234u);
    CHECK(glyph == static_cast<std::uint16_t>(1u | (0x1234u << 1u)));
    CHECK(unpack_mtsdf_glyph_unit_index(static_cast<std::uint16_t>(glyph)) == 0x1234u);
    CHECK((pack_mtsdf_glyph_unit_flags(0u, 0x9000u) & 1u) == 0u);
    CHECK(unpack_mtsdf_glyph_unit_index(static_cast<std::uint16_t>(
              pack_mtsdf_glyph_unit_flags(1u, 0x9000u))) == kMtsdfGlyphWordIndexMask);
}

TEST_CASE("GPU CPU selector weight supports word, line, and center-out order") {
    TextRunDynamic run{};
    run.selector_start = 0.0f;
    run.selector_end = 34.0f;
    run.selector_amount = 100.0f;
    run.selector_unit_count = 3;

    run.flags = pack_text_selector_metadata(2u, static_cast<std::uint32_t>(TextSelectorUnit::Word),
                                            static_cast<std::uint32_t>(TextSelectorOrder::Forward),
                                            TextSelectorShape::Square);
    CHECK(gpu_text_selector_weight(run, 0u) > 0.99f);
    CHECK(gpu_text_selector_weight(run, 1u) < 0.01f);

    run.flags = pack_text_selector_metadata(2u, static_cast<std::uint32_t>(TextSelectorUnit::Line),
                                            static_cast<std::uint32_t>(TextSelectorOrder::Reverse),
                                            TextSelectorShape::Smooth);
    CHECK(gpu_text_selector_weight(run, 2u) > 0.99f);
    CHECK(gpu_text_selector_weight(run, 0u) < 0.01f);

    run.selector_unit_count = 5;
    run.selector_end = 25.0f;
    run.flags = pack_text_selector_metadata(2u, static_cast<std::uint32_t>(TextSelectorUnit::Word),
                                            static_cast<std::uint32_t>(TextSelectorOrder::FromCenter),
                                            TextSelectorShape::Square);
    CHECK(gpu_text_selector_weight(run, 2u) > 0.99f);
    CHECK(gpu_text_selector_weight(run, 0u) < 0.01f);

    constexpr std::array<TextSelectorShape, 6> shapes{
        TextSelectorShape::Square, TextSelectorShape::RampUp,
        TextSelectorShape::RampDown, TextSelectorShape::Triangle,
        TextSelectorShape::Round, TextSelectorShape::Smooth};
    for (const auto shape : shapes) {
        run.selector_start = 0.0f;
        run.selector_end = 80.0f;
        run.selector_amount = 73.0f;
        run.selector_unit_count = 5u;
        run.flags = pack_text_selector_metadata(
            2u, static_cast<std::uint32_t>(TextSelectorUnit::Glyph),
            static_cast<std::uint32_t>(TextSelectorOrder::Forward), shape);
        const SelectorWeightParams reference{
            shape, 0.0f, 80.0f, 0.0f, 73.0f, 5u, 0u, true, true, false};
        for (std::uint32_t glyph = 0; glyph < 5u; ++glyph) {
            auto glyph_reference = reference;
            glyph_reference.ordered_index = glyph;
            CHECK(gpu_text_selector_weight(run, glyph) ==
                  doctest::Approx(selector_weight(glyph_reference, glyph)));
        }
    }
}
