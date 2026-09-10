#include <doctest/doctest.h>

#include <chronon3d/backends/text/mtsdf_generator.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <limits>

namespace {

constexpr const char* kFont = "tests/fixtures/Inter-Bold.ttf";

struct FreeTypeFace {
    FT_Library library{nullptr};
    FT_Face face{nullptr};

    FreeTypeFace() {
        REQUIRE(FT_Init_FreeType(&library) == 0);
        REQUIRE(FT_New_Face(library, kFont, 0, &face) == 0);
        REQUIRE(FT_Set_Pixel_Sizes(face, 0, 64) == 0);
    }

    ~FreeTypeFace() {
        if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
    }
};

std::uint32_t texel_bits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}
std::uint8_t channel(std::uint32_t texel, unsigned shift) noexcept {
    return static_cast<std::uint8_t>((texel >> shift) & 0xFFu);
}

} // namespace

TEST_CASE("MTSDF generator emits a real signed multi-channel glyph") {
    REQUIRE(std::filesystem::exists(kFont));
    FreeTypeFace ft;
    const auto glyph_id = FT_Get_Char_Index(ft.face, static_cast<FT_ULong>('H'));
    REQUIRE(glyph_id != 0);

    const auto bitmap = chronon3d::generate_mtsdf_glyph(
        ft.face, glyph_id, 64, 8, 10);
    REQUIRE(bitmap.has_value());
    REQUIRE(bitmap->width > 0);
    REQUIRE(bitmap->height > 0);
    REQUIRE(bitmap->advance_x > 0.0f);
    REQUIRE(bitmap->rgba8.size() ==
            static_cast<std::size_t>(bitmap->width) * bitmap->height);

    bool has_inside = false;
    bool has_outside = false;
    bool has_edge_coloring = false;
    for (const float packed : bitmap->rgba8) {
        const auto texel = texel_bits(packed);
        const auto r = channel(texel, 0);
        const auto g = channel(texel, 8);
        const auto b = channel(texel, 16);
        const auto a = channel(texel, 24);
        has_inside |= a > 200;
        has_outside |= a < 55;
        has_edge_coloring |= r != g || g != b;
    }

    CHECK(has_inside);
    CHECK(has_outside);
    CHECK(has_edge_coloring);
}
