#pragma once

// ICU is kept behind this text/unicode boundary. Callers use opaque numeric
// script ids and do not depend on ICU headers or types.
#include <unicode/utf8.h>
#include <unicode/uscript.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::text::unicode {

using Script = std::int32_t;
inline constexpr Script unknown_script = -1;

[[nodiscard]] inline bool is_strong_script(Script script) noexcept {
    if (script == unknown_script) return false;
    const auto value = static_cast<UScriptCode>(script);
    return value != USCRIPT_COMMON && value != USCRIPT_INHERITED &&
           value != USCRIPT_UNKNOWN;
}

[[nodiscard]] inline bool is_right_to_left_script(Script script) noexcept {
    return script != unknown_script &&
        uscript_isRightToLeft(static_cast<UScriptCode>(script));
}

[[nodiscard]] inline const char* short_name(Script script) noexcept {
    return script == unknown_script ? nullptr :
        uscript_getShortName(static_cast<UScriptCode>(script));
}

[[nodiscard]] inline Script first_strong_script(std::string_view text) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    const auto length = static_cast<int32_t>(text.size());
    int32_t cursor = 0;
    while (cursor < length) {
        UChar32 codepoint = 0;
        U8_NEXT(bytes, cursor, length, codepoint);
        if (codepoint < 0) continue;
        UErrorCode error = U_ZERO_ERROR;
        const auto script = uscript_getScript(codepoint, &error);
        if (U_SUCCESS(error) && script != USCRIPT_COMMON &&
            script != USCRIPT_INHERITED && script != USCRIPT_UNKNOWN)
            return static_cast<Script>(script);
    }
    return unknown_script;
}

[[nodiscard]] inline std::vector<std::pair<std::size_t, std::size_t>>
strong_script_segments(std::string_view text) {
    std::vector<std::pair<std::size_t, std::size_t>> segments;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    const auto length = static_cast<int32_t>(text.size());
    int32_t cursor = 0;
    int32_t start = 0;
    UScriptCode current = USCRIPT_UNKNOWN;
    while (cursor < length) {
        const int32_t char_start = cursor;
        UChar32 codepoint = 0;
        U8_NEXT(bytes, cursor, length, codepoint);
        if (codepoint < 0) continue;
        UErrorCode error = U_ZERO_ERROR;
        const auto script = uscript_getScript(codepoint, &error);
        if (U_FAILURE(error) || script == USCRIPT_COMMON ||
            script == USCRIPT_INHERITED || script == USCRIPT_UNKNOWN) continue;
        if (current == USCRIPT_UNKNOWN || current == USCRIPT_COMMON ||
            current == USCRIPT_INHERITED) {
            current = script;
            continue;
        }
        if (script == current) continue;
        segments.emplace_back(static_cast<std::size_t>(start),
                              static_cast<std::size_t>(char_start));
        start = char_start;
        current = script;
    }
    if (start < length)
        segments.emplace_back(static_cast<std::size_t>(start), text.size());
    if (segments.empty() && !text.empty()) segments.emplace_back(0, text.size());
    return segments;
}

} // namespace chronon3d::text::unicode
