#include <doctest/doctest.h>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>

using namespace chronon3d;

TEST_CASE("TextRasterizerCache cache key hashing specifications") {
    TextShape base_text;
    base_text.text = "Hello World";
    base_text.style.font_path = "/fonts/Inter.ttf";
    base_text.style.font_family = "Inter";
    base_text.style.size = 24.0f;
    base_text.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
    base_text.style.align = TextAlign::Left;
    base_text.style.line_height = 1.2f;
    base_text.style.tracking = 0.0f;
    base_text.style.max_lines = 0;
    base_text.style.auto_scale = false;
    base_text.style.min_size = 8.0f;
    base_text.style.max_size = 72.0f;
    base_text.style.auto_fit = false;
    base_text.style.ellipsis = false;
    base_text.style.overflow = TextOverflow::Clip;
    base_text.style.wrap = TextWrap::Word;
    base_text.box.enabled = true;
    base_text.box.size = {300.0f, 100.0f};

    float effective_size = 24.0f;
    int padding = 4;

    uint64_t base_hash = hash_text_style(base_text, effective_size, padding);

    SUBCASE("Same text same style causes cache hit (identical hashes)") {
        uint64_t same_hash = hash_text_style(base_text, effective_size, padding);
        CHECK(base_hash == same_hash);
    }

    SUBCASE("Changing wrap causes cache miss") {
        TextShape other = base_text;
        other.style.wrap = TextWrap::Character;
        uint64_t other_hash = hash_text_style(other, effective_size, padding);
        CHECK(base_hash != other_hash);
    }

    SUBCASE("Changing overflow causes cache miss") {
        TextShape other = base_text;
        other.style.overflow = TextOverflow::Ellipsis;
        uint64_t other_hash = hash_text_style(other, effective_size, padding);
        CHECK(base_hash != other_hash);
    }

    SUBCASE("Changing ellipsis causes cache miss") {
        TextShape other = base_text;
        other.style.ellipsis = true;
        uint64_t other_hash = hash_text_style(other, effective_size, padding);
        CHECK(base_hash != other_hash);
    }

    SUBCASE("Changing max_lines causes cache miss") {
        TextShape other = base_text;
        other.style.max_lines = 3;
        uint64_t other_hash = hash_text_style(other, effective_size, padding);
        CHECK(base_hash != other_hash);
    }

    SUBCASE("Changing auto_fit causes cache miss") {
        TextShape other = base_text;
        other.style.auto_fit = true;
        uint64_t other_hash = hash_text_style(other, effective_size, padding);
        CHECK(base_hash != other_hash);
    }

    SUBCASE("Changing box size causes cache miss") {
        TextShape other = base_text;
        other.box.size = {400.0f, 100.0f};
        uint64_t other_hash = hash_text_style(other, effective_size, padding);
        CHECK(base_hash != other_hash);
    }

    SUBCASE("Changing box enabled causes cache miss") {
        TextShape other = base_text;
        other.box.enabled = false;
        uint64_t other_hash = hash_text_style(other, effective_size, padding);
        CHECK(base_hash != other_hash);
    }
}
