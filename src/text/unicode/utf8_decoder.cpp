// ═══════════════════════════════════════════════════════════════════════════
// utf8_decoder.cpp — FASE 2 TICKET-080 canonical UTF-8 decoder impl
//
// Internal implementation, matches all 4 prior duplicate inline impls
// byte-for-byte at canonical input/output semantics.  See utf8_decoder.hpp
// for the anti-duplication invariant list and migration notes.
// ═══════════════════════════════════════════════════════════════════════════

#include "utf8_decoder.hpp"

namespace chronon3d {
namespace text {
namespace unicode {

std::size_t utf8_seq_length(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;  // invalid lead byte — skip one to make forward progress.
}

char32_t decode_codepoint(std::string_view sv, std::size_t& pos) noexcept {
    if (pos >= sv.size()) return 0xFFFD;
    const unsigned char lead = static_cast<unsigned char>(sv[pos]);
    const std::size_t len = utf8_seq_length(lead);
    if (pos + len > sv.size()) { ++pos; return 0xFFFD; }
    char32_t cp;
    switch (len) {
        case 1: cp = lead; break;
        case 2: cp =
            ((lead & 0x1F) << 6) |
            (static_cast<unsigned char>(sv[pos + 1]) & 0x3F);
            break;
        case 3: cp =
            ((lead & 0x0F) << 12) |
            ((static_cast<unsigned char>(sv[pos + 1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(sv[pos + 2]) & 0x3F);
            break;
        case 4: cp =
            ((lead & 0x07) << 18) |
            ((static_cast<unsigned char>(sv[pos + 1]) & 0x3F) << 12) |
            ((static_cast<unsigned char>(sv[pos + 2]) & 0x3F) << 6) |
             (static_cast<unsigned char>(sv[pos + 3]) & 0x3F);
            break;
        default: cp = 0xFFFD; break;
    }
    pos += len;
    return cp;
}

char32_t decode_codepoint_at(std::string_view sv, std::size_t offset) noexcept {
    if (offset >= sv.size()) return 0xFFFD;
    const unsigned char lead = static_cast<unsigned char>(sv[offset]);
    const std::size_t len = utf8_seq_length(lead);
    if (offset + len > sv.size()) return 0xFFFD;
    switch (len) {
        case 1: return lead;
        case 2: return
            ((lead & 0x1F) << 6) |
            (static_cast<unsigned char>(sv[offset + 1]) & 0x3F);
        case 3: return
            ((lead & 0x0F) << 12) |
            ((static_cast<unsigned char>(sv[offset + 1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(sv[offset + 2]) & 0x3F);
        case 4: return
            ((lead & 0x07) << 18) |
            ((static_cast<unsigned char>(sv[offset + 1]) & 0x3F) << 12) |
            ((static_cast<unsigned char>(sv[offset + 2]) & 0x3F) << 6) |
             (static_cast<unsigned char>(sv[offset + 3]) & 0x3F);
        default: return 0xFFFD;
    }
}

}  // namespace unicode
}  // namespace text
}  // namespace chronon3d
