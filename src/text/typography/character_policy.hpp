#pragma once

#include "../unicode/utf8_decoder.hpp"
#include "../unicode/whitespace.hpp"

#include <string_view>

namespace chronon3d::composer_internal {

[[nodiscard]] inline char32_t decode_codepoint_at(
    std::string_view sv, std::size_t byte_start) noexcept {
    return text::unicode::decode_codepoint_at(sv, byte_start);
}

[[nodiscard]] inline bool is_whitespace_codepoint(char32_t cp) noexcept {
    return text::unicode::is_unicode_whitespace(cp);
}

[[nodiscard]] inline bool is_mandatory_break_codepoint(char32_t cp) noexcept {
    return cp == U'\n' || cp == U'\r';
}

[[nodiscard]] inline bool is_punctuation_codepoint(char32_t cp) noexcept {
    switch (cp) {
    case U'.': case U',': case U';': case U':': case U'!': case U'?':
    case U'"': case U'\'': case U'`':
    case U'\u201C': case U'\u201D': case U'\u2018': case U'\u2019':
    case U'-': case U'\u2013': case U'\u2014':
    case U'\u2026':
    case U'\u00AB': case U'\u00BB':
    case U'\u2039': case U'\u203A':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool is_soft_hyphen_at(
    std::string_view sv, std::size_t byte_start) noexcept {
    return byte_start < sv.size() &&
           decode_codepoint_at(sv, byte_start) == U'\u00AD';
}

} // namespace chronon3d::composer_internal
