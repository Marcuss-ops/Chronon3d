#pragma once

#include <cstddef>
#include <string_view>

namespace chronon3d::text::unicode {

/// Return the nominal UTF-8 sequence length for a lead byte using ICU's
/// canonical UTF-8 classification. Invalid lead/continuation bytes return 1.
[[nodiscard]] std::size_t utf8_seq_length(unsigned char lead) noexcept;

/// Decode one UTF-8 scalar value through ICU and advance `pos` by the number
/// of bytes consumed. Malformed or truncated input produces U+FFFD. When
/// `pos >= sv.size()`, returns U+FFFD without advancing.
[[nodiscard]] char32_t decode_codepoint(
    std::string_view sv, std::size_t& pos) noexcept;

/// Decode the scalar value beginning at `offset` without exposing iterator
/// state to the caller. ICU owns UTF-8 validation and substitution semantics.
[[nodiscard]] char32_t decode_codepoint_at(
    std::string_view sv, std::size_t offset) noexcept;

} // namespace chronon3d::text::unicode
