#include "src/text/unicode/utf8_decoder.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>

using chronon3d::text::unicode::decode_codepoint;
using chronon3d::text::unicode::decode_codepoint_at;
using chronon3d::text::unicode::utf8_seq_length;

TEST_CASE("UnicodeDecoder: ICU decodes ASCII and multi-byte scalars") {
    const std::string text = "A\xE2\x82\xAC\xF0\x9F\x98\x80";
    std::size_t pos = 0;

    CHECK(decode_codepoint(text, pos) == U'A');
    CHECK(pos == 1);
    CHECK(decode_codepoint(text, pos) == U'\u20AC');
    CHECK(pos == 4);
    CHECK(decode_codepoint(text, pos) == U'\U0001F600');
    CHECK(pos == text.size());
}

TEST_CASE("UnicodeDecoder: ICU substitutes malformed and truncated input") {
    const std::string malformed = "\xFFx";
    std::size_t pos = 0;
    CHECK(decode_codepoint(malformed, pos) == 0xFFFD);
    CHECK(pos == 1);
    CHECK(decode_codepoint(malformed, pos) == U'x');

    const std::string truncated = "\xE2\x82";
    pos = 0;
    CHECK(decode_codepoint(truncated, pos) == 0xFFFD);
    CHECK(pos > 0);
    CHECK(pos <= truncated.size());
}

TEST_CASE("UnicodeDecoder: random-access decoding and lead classification use ICU") {
    const std::string text = "x\xC2\xADy";
    CHECK(decode_codepoint_at(text, 1) == U'\u00AD');
    CHECK(utf8_seq_length(static_cast<unsigned char>('x')) == 1);
    CHECK(utf8_seq_length(0xC2) == 2);
    CHECK(utf8_seq_length(0xE2) == 3);
    CHECK(utf8_seq_length(0xF0) == 4);
    CHECK(utf8_seq_length(0x80) == 1);
}
