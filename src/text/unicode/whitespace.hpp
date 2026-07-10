#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// whitespace.hpp — FASE 2 TICKET-080 canonical Unicode whitespace classifier
//
// Internal header — NOT part of the public API.  Lives in `src/text/unicode/`
// per the FASE 2 unicode-extraction plan.
//
// Anti-duplication invariants:
//   • Replaces 3 private duplicate impls:
//       (1) `chronon3d::detail::is_unicode_whitespace`     in include/.../glyph_selector.hpp  // drift-allow: stale-ref
//       (2) `composer_internal::is_whitespace_codepoint`   in src/text/internal/composer_helpers.hpp  // drift-allow: stale-ref
//       (3) anonymous `is_unicode_whitespace_cp`           in src/text/text_unit_map.cpp  // drift-allow: stale-ref
//   • The canonical list is the SUPERSET of all three lists:
//       ASCII (`' '`, `\t`, `\r`, `\n`) +
//       U+00A0 NBSP, U+1680 ogham space +
//       U+2000-U+200A en/em/thin/hair etc. +
//       U+2028 LINE SEPARATOR, U+2029 PARAGRAPH SEPARATOR +
//       U+202F NARROW NO-BREAK SPACE, U+205F MEDIUM MATH SPACE +
//       U+3000 IDEOGRAPHIC SPACE +
//       U+FEFF ZERO WIDTH NO-BREAK SPACE (from the selector list).
//     Note: U+FEFF was missing in (2)+(3); adding it here matches the
//     selector behaviour, which is the canonical decision (U+FEFF is a
//     zero-width whitespace historically used as BOM, treated as such by
//     Unicode line/word boundary rules).
//   • The new superset is what the SHAPER sees; the BOM is correctly
//     re-classified as whitespace so it never reaches the cluster count.
//     Composer's call sites pick up the superset transparently — see
//     the FASE 2 commit message for the per-callsite behaviour diff table.
//
// Determinism: pure, no time, no PRNG, no threads.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace chronon3d {
namespace text {
namespace unicode {

/// Returns true if `cp` is a Unicode whitespace or separator character.
/// See file header for the full cover list.  True == the codepoint is
/// eligible for UAX#29 WB5a (break after whitespace) treatment.  Does NOT
/// encode mandatory-break semantics — for that, callers must also check
/// `cp == '\n' || cp == '\r'`.
[[nodiscard]] bool is_unicode_whitespace(char32_t cp) noexcept;

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
