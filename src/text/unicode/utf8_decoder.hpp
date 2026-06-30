#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// utf8_decoder.hpp — FASE 2 TICKET-080 canonical UTF-8 decoder (internal)
//
// Internal header — NOT part of the public API.  Lives in `src/text/unicode/`
// per the FASE 2 unicode-extraction plan, included by sibling internal files
// under `src/text/`.
//
// Anti-duplication invariants:
//   • Does NOT pull in `<msdfgen>`, `<libtess2>`, `<unicode[/...]>` —
//     see AGENTS.md Gate 5 deny-list.  This is a hand-rolled decoder
//     covering ASCII + 2/3/4-byte UTF-8 only (no library header prefix
//     `unicode/` allowed, even if not an actual ICU/Boost import).
//   • Replaces 4 private duplicate impls:
//       (1) `chronon3d::detail::utf8_decode`         in include/.../text_unicode_utils.hpp
//       (2) `chronon3d::detail::decode_utf8_codepoint_from` in include/.../glyph_selector.hpp
//       (3) `composer_internal::decode_codepoint_at` in src/text/internal/composer_helpers.hpp
//       (4) anonymous `decode_utf8(sv, idx)` in src/text/text_unit_map.cpp
//     The public-header variants (1)+(2) remain defined for back-compat
//     with all current callers (inside include/chronon3d/), but new code
//     under src/ should prefer this canonical header.
//
// Determinism: pure, no time, no PRNG, no threads.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace chronon3d {
namespace text {
namespace unicode {

/// Return the byte length of the UTF-8 sequence starting with the given
/// LEAD byte.  Returns 1 for ASCII (high bit 0), 2-4 for valid multi-byte
/// lead bytes (0xC2/0xE0/0xF0 mask families), and 1 for invalid lead bytes
/// (e.g. 0xC0, 0xC1, 0xF5-0xFF) — invalid leads are advanced-by-1 so the
/// caller can still make forward progress on malformed input.
/// Behaviour matches the pre-existing `chronon3d::detail::utf8_seq_len`
/// and `decode_utf8_codepoint_from`-style len derivation exactly.
[[nodiscard]] std::size_t utf8_seq_length(unsigned char lead) noexcept;

/// Decode a single UTF-8 codepoint starting at offset `pos` in `sv`.
/// Advances `pos` by 1-4 bytes (1 on invalid / truncated / OOB).
/// Returns U+FFFD REPLACEMENT CHARACTER on:
///   - `pos >= sv.size()`
///   - truncated multi-byte sequence (`pos + len > sv.size()`)
///   - invalid lead byte
///   - invalid continuation byte (high-2 bits != 10xxxxxx)
[[nodiscard]] char32_t decode_codepoint(std::string_view sv, std::size_t& pos) noexcept;

/// Decode the codepoint at byte `offset` in `sv` WITHOUT advancing.
/// Returns U+FFFD if `offset >= sv.size()` or the sequence is invalid /
/// truncated.  Equivalent to `decode_codepoint` with a scratch pos var,
/// but doesn't require the caller to manage the offset.
[[nodiscard]] char32_t decode_codepoint_at(std::string_view sv, std::size_t offset) noexcept;

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
