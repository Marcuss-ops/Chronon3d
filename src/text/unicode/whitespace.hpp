#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// whitespace.hpp — FASE 2 TICKET-080 canonical Unicode whitespace classifier
//
// Internal header — NOT part of the public API.  Lives in `src/text/unicode/`
// per the FASE 2 unicode-extraction plan.
//
// Anti-duplication invariants:
//   • Replaces 3 private duplicate impls:
//       (1) `chronon3d::detail::is_unicode_whitespace`     in include/chronon3d/text/glyph_selector.hpp
//       (2) `composer_internal::is_whitespace_codepoint`   in src/text/boundary_resolver/composer_helpers.hpp
//       (3) anonymous `is_unicode_whitespace_cp`           in src/text/boundary_resolver/text_unit_map.cpp
//   • ICU owns the Unicode White_Space property table. Chronon explicitly
//     retains U+FEFF as whitespace for the historical BOM/text-unit contract.
//
// Determinism: pure, no time, no PRNG, no threads.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace chronon3d {
namespace text {
namespace unicode {

/// Returns true if `cp` is a Unicode whitespace or separator character.
/// ICU White_Space plus Chronon's explicit U+FEFF BOM policy. True == the codepoint is
/// eligible for UAX#29 WB5a (break after whitespace) treatment.  Does NOT
/// encode mandatory-break semantics — for that, callers must also check
/// `cp == '\n' || cp == '\r'`.
[[nodiscard]] bool is_unicode_whitespace(char32_t cp) noexcept;

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
