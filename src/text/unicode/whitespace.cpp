// ═══════════════════════════════════════════════════════════════════════════
// whitespace.cpp — FASE 2 TICKET-080 canonical Unicode whitespace classifier
//
// Canonical superset impl.  See whitespace.hpp header for the cover-list
// provenance and per-call-site behaviour-diff discussion.
// ═══════════════════════════════════════════════════════════════════════════

#include "whitespace.hpp"

namespace chronon3d {
namespace text {
namespace unicode {

bool is_unicode_whitespace(char32_t cp) noexcept {
    // ── ASCII fast-path ───────────────────────────────────────────────
    if (cp <= 0x7Fu) {
        return cp == 0x20u  // SP
            || cp == 0x09u  // TAB
            || cp == 0x0Au  // LF
            || cp == 0x0Du; // CR
    }
    // ── Unicode whitespace / separator cover list (UAX#29 WB5a) ───────
    switch (cp) {
        case 0x00A0u:  // NO-BREAK SPACE
        case 0x1680u:  // OGHAM SPACE MARK
        case 0x2000u:  // EN QUAD
        case 0x2001u:  // EM QUAD
        case 0x2002u:  // EN SPACE
        case 0x2003u:  // EM SPACE
        case 0x2004u:  // THREE-PER-EM SPACE
        case 0x2005u:  // FOUR-PER-EM SPACE
        case 0x2006u:  // SIX-PER-EM SPACE
        case 0x2007u:  // FIGURE SPACE
        case 0x2008u:  // PUNCTUATION SPACE
        case 0x2009u:  // THIN SPACE
        case 0x200Au:  // HAIR SPACE
        case 0x2028u:  // LINE SEPARATOR
        case 0x2029u:  // PARAGRAPH SEPARATOR
        case 0x202Fu:  // NARROW NO-BREAK SPACE
        case 0x205Fu:  // MEDIUM MATHEMATICAL SPACE
        case 0x3000u:  // IDEOGRAPHIC SPACE
        case 0xFEFFu:  // ZERO WIDTH NO-BREAK SPACE (BOM)
            return true;
        default:
            return false;
    }
}

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
