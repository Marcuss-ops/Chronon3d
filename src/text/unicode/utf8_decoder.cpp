#include "utf8_decoder.hpp"

#include <unicode/utf8.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace chronon3d::text::unicode {
namespace {
constexpr char32_t kReplacementCharacter = 0xFFFD;
}

std::size_t utf8_seq_length(unsigned char lead) noexcept {
    const auto byte = static_cast<std::uint8_t>(lead);
    if (U8_IS_SINGLE(byte)) return 1;
    if (U8_IS_LEAD(byte)) {
        return static_cast<std::size_t>(U8_COUNT_TRAIL_BYTES(byte) + 1);
    }
    // Invalid lead/continuation bytes consume one byte under the adapter
    // contract, matching ICU's substitution behaviour.
    return 1;
}

char32_t decode_codepoint(std::string_view sv, std::size_t& pos) noexcept {
    if (pos >= sv.size()) return kReplacementCharacter;

    // ICU's UTF-8 iteration macros use int32 offsets. Decode from a bounded
    // local window so Chronon's size_t string offsets remain correct even for
    // very large buffers while ICU owns all byte-sequence validation.
    std::array<std::uint8_t, 4> window{};
    const std::size_t remaining = sv.size() - pos;
    const auto length = static_cast<std::int32_t>(
        std::min<std::size_t>(window.size(), remaining));
    for (std::int32_t i = 0; i < length; ++i) {
        window[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(sv[pos + static_cast<std::size_t>(i)]);
    }

    std::int32_t index = 0;
    UChar32 codepoint = static_cast<UChar32>(kReplacementCharacter);
    U8_NEXT_OR_FFFD(window.data(), index, length, codepoint);
    if (index <= 0) index = 1;
    pos += static_cast<std::size_t>(index);
    return static_cast<char32_t>(codepoint);
}

char32_t decode_codepoint_at(std::string_view sv, std::size_t offset) noexcept {
    std::size_t pos = offset;
    return decode_codepoint(sv, pos);
}

} // namespace chronon3d::text::unicode
