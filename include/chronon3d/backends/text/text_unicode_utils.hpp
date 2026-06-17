#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace chronon3d::detail {

/// Return the byte length of the UTF-8 sequence starting at the given byte.
/// Returns 1 for ASCII, 2-4 for multi-byte sequences, 1 for invalid.
inline size_t utf8_seq_len(unsigned char lead) noexcept {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1; // invalid continuation byte — skip one
}

/// Decode a single UTF-8 code point at the given position.
/// Advances `pos` by the sequence length.  Returns U+FFFD on error.
inline char32_t utf8_decode(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return 0;
    const unsigned char c0 = static_cast<unsigned char>(s[pos]);
    const size_t len = utf8_seq_len(c0);
    if (pos + len > s.size()) { ++pos; return 0xFFFD; }
    char32_t cp;
    switch (len) {
        case 1: cp = c0; break;
        case 2: cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[pos+1]) & 0x3F); break;
        case 3: cp = ((c0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[pos+1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos+2]) & 0x3F); break;
        case 4: cp = ((c0 & 0x07) << 18) | ((static_cast<unsigned char>(s[pos+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[pos+2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[pos+3]) & 0x3F); break;
        default: cp = 0xFFFD; break;
    }
    pos += len;
    return cp;
}

/// Remove the last code point from a UTF-8 string (in-place).
/// Safe for multi-byte sequences — scans backwards from end.
inline void utf8_pop_back(std::string& s) {
    if (s.empty()) return;
    size_t pos = s.size() - 1;
    // Skip continuation bytes (10xxxxxx)
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    s.resize(pos);
}

/// Return the number of code points in a UTF-8 string (O(n)).
inline size_t utf8_length(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size();) {
        i += utf8_seq_len(static_cast<unsigned char>(s[i]));
        ++count;
    }
    return count;
}

/// Overload for std::string_view — avoids copying into std::string.
inline size_t utf8_length(std::string_view sv) {
    size_t count = 0;
    for (size_t i = 0; i < sv.size();) {
        i += utf8_seq_len(static_cast<unsigned char>(sv[i]));
        ++count;
    }
    return count;
}

// ── Grapheme cluster segmentation (UAX #29 simplified) ───────────
//
// These functions iterate by *extended grapheme clusters* —
// user-perceived characters — instead of raw code points.
// A grapheme cluster keeps combining marks, ZWJ emoji sequences,
// emoji skin-tone modifiers, and regional-indicator flag pairs
// together so they are never split mid-character during animations.
//
// Extended grapheme clusters are defined by Unicode Standard Annex #29.
// We implement the core rules GB1-GB5, GB9-GB11, and GB12-GB13
// using static range tables instead of ICU, keeping this header
// dependency-free while covering the most common real-world scripts.

/// Decode a single UTF-8 code point at the current offset.
/// Advances `offset` by the sequence length.  Returns U+FFFD on error.
/// `string_view` overload for use with `std::string_view` callers
/// (the existing `utf8_decode` takes `const std::string&`).
inline char32_t utf8_decode_cp(std::string_view sv, size_t& offset) {
    if (offset >= sv.size()) return 0;
    const unsigned char lead = static_cast<unsigned char>(sv[offset]);
    const size_t len = utf8_seq_len(lead);
    if (offset + len > sv.size()) { ++offset; return 0xFFFD; }

    char32_t cp;
    switch (len) {
        case 1: cp = lead; break;
        case 2: cp = ((lead & 0x1F) << 6) | (static_cast<unsigned char>(sv[offset+1]) & 0x3F); break;
        case 3: cp = ((lead & 0x0F) << 12) | ((static_cast<unsigned char>(sv[offset+1]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset+2]) & 0x3F); break;
        case 4: cp = ((lead & 0x07) << 18) | ((static_cast<unsigned char>(sv[offset+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(sv[offset+2]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset+3]) & 0x3F); break;
        default: cp = 0xFFFD; break;
    }
    offset += len;
    return cp;
}

/// True if `cp` is a Grapheme_Extend character (continues the current
/// cluster).  Covers combining marks (Mn/Mc/Me), ZWJ, variation
/// selectors, emoji modifiers, and other invisible extenders.
inline bool is_grapheme_extend(char32_t cp) noexcept {
    // Zero-width joiner — U+200D
    if (cp == 0x200D) return true;

    // ── Combining Diacritical Marks: U+0300-U+036F ─────────────
    if (cp >= 0x0300 && cp <= 0x036F) return true;

    // ── Combining Diacritical Marks Extended: U+1AB0-U+1AFF ────
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return true;

    // ── Combining Diacritical Marks Supplement: U+1DC0-U+1DFF ──
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return true;

    // ── Combining Diacritical Marks for Symbols: U+20D0-U+20FF ─
    if (cp >= 0x20D0 && cp <= 0x20FF) return true;

    // ── Combining Half Marks: U+FE20-U+FE2F ────────────────────
    if (cp >= 0xFE20 && cp <= 0xFE2F) return true;

    // ── Variation Selectors: U+FE00-U+FE0F ─────────────────────
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;
    // Variation Selectors Supplement: U+E0100-U+E01EF
    if (cp >= 0xE0100 && cp <= 0xE01EF) return true;

    // ── Emoji modifiers (skin tones): U+1F3FB-U+1F3FF ──────────
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true;

    // ── Arabic combining marks ──────────────────────────────────
    if (cp >= 0x0610 && cp <= 0x061A) return true;
    if (cp >= 0x064B && cp <= 0x065F) return true;
    if (cp == 0x0670) return true;
    if (cp >= 0x06D6 && cp <= 0x06DC) return true;
    if (cp >= 0x06DF && cp <= 0x06E4) return true;
    if (cp >= 0x06E7 && cp <= 0x06E8) return true;
    if (cp >= 0x06EA && cp <= 0x06ED) return true;
    if (cp >= 0x08D4 && cp <= 0x08E1) return true;
    if (cp >= 0x08E3 && cp <= 0x08FF) return true;

    // ── Cyrillic combining marks ────────────────────────────────
    if (cp >= 0x0483 && cp <= 0x0489) return true;

    // ── Hebrew combining marks ──────────────────────────────────
    if (cp >= 0x0591 && cp <= 0x05BD) return true;
    if (cp == 0x05BF) return true;
    if (cp >= 0x05C1 && cp <= 0x05C2) return true;
    if (cp >= 0x05C4 && cp <= 0x05C5) return true;
    if (cp == 0x05C7) return true;

    // ── Syriac combining marks ──────────────────────────────────
    if (cp == 0x0711) return true;
    if (cp >= 0x0730 && cp <= 0x074A) return true;

    // ── Thaana combining marks ──────────────────────────────────
    if (cp >= 0x07A6 && cp <= 0x07B0) return true;

    // ── N'Ko combining marks ────────────────────────────────────
    if (cp >= 0x07EB && cp <= 0x07F3) return true;
    if (cp >= 0x07FD && cp <= 0x07FD) return true;

    // ── Samaritan combining marks ───────────────────────────────
    if (cp >= 0x0816 && cp <= 0x0819) return true;
    if (cp >= 0x081B && cp <= 0x0823) return true;
    if (cp >= 0x0825 && cp <= 0x0827) return true;
    if (cp >= 0x0829 && cp <= 0x082D) return true;

    // ── Mandaic combining marks ─────────────────────────────────
    if (cp >= 0x0859 && cp <= 0x085B) return true;

    // ── Devanagari + other Indic combining marks ────────────────
    // Devanagari
    if (cp >= 0x0900 && cp <= 0x0903) return true;
    if (cp >= 0x093A && cp <= 0x093C) return true;
    if (cp >= 0x093E && cp <= 0x094F) return true;
    if (cp >= 0x0951 && cp <= 0x0957) return true;
    if (cp >= 0x0962 && cp <= 0x0963) return true;
    // Bengali
    if (cp >= 0x0981 && cp <= 0x0983) return true;
    if (cp >= 0x09BC && cp <= 0x09BC) return true;
    if (cp >= 0x09BE && cp <= 0x09C4) return true;
    if (cp >= 0x09C7 && cp <= 0x09C8) return true;
    if (cp >= 0x09CB && cp <= 0x09CD) return true;
    if (cp == 0x09D7) return true;
    if (cp >= 0x09E2 && cp <= 0x09E3) return true;
    if (cp == 0x09FE) return true;
    // Gurmukhi
    if (cp >= 0x0A01 && cp <= 0x0A03) return true;
    if (cp == 0x0A3C) return true;
    if (cp >= 0x0A3E && cp <= 0x0A42) return true;
    if (cp >= 0x0A47 && cp <= 0x0A48) return true;
    if (cp >= 0x0A4B && cp <= 0x0A4D) return true;
    if (cp == 0x0A51) return true;
    if (cp >= 0x0A70 && cp <= 0x0A71) return true;
    if (cp == 0x0A75) return true;
    // Gujarati
    if (cp >= 0x0A81 && cp <= 0x0A83) return true;
    if (cp == 0x0ABC) return true;
    if (cp >= 0x0ABE && cp <= 0x0AC5) return true;
    if (cp >= 0x0AC7 && cp <= 0x0AC9) return true;
    if (cp >= 0x0ACB && cp <= 0x0ACD) return true;
    if (cp >= 0x0AE2 && cp <= 0x0AE3) return true;
    // Oriya
    if (cp >= 0x0B01 && cp <= 0x0B03) return true;
    if (cp == 0x0B3C) return true;
    if (cp >= 0x0B3E && cp <= 0x0B44) return true;
    if (cp >= 0x0B47 && cp <= 0x0B48) return true;
    if (cp >= 0x0B4B && cp <= 0x0B4D) return true;
    if (cp >= 0x0B55 && cp <= 0x0B57) return true;
    if (cp >= 0x0B62 && cp <= 0x0B63) return true;
    // Tamil, Telugu, Kannada, Malayalam
    if (cp >= 0x0BBE && cp <= 0x0BC2) return true;
    if (cp >= 0x0BC6 && cp <= 0x0BC8) return true;
    if (cp >= 0x0BCA && cp <= 0x0BCD) return true;
    if (cp == 0x0BD7) return true;
    if (cp >= 0x0C00 && cp <= 0x0C04) return true;
    if (cp >= 0x0C3E && cp <= 0x0C44) return true;
    if (cp >= 0x0C46 && cp <= 0x0C48) return true;
    if (cp >= 0x0C4A && cp <= 0x0C4D) return true;
    if (cp >= 0x0C55 && cp <= 0x0C56) return true;
    if (cp >= 0x0C62 && cp <= 0x0C63) return true;
    if (cp >= 0x0C81 && cp <= 0x0C83) return true;
    if (cp == 0x0CBC) return true;
    if (cp >= 0x0CBE && cp <= 0x0CC4) return true;
    if (cp >= 0x0CC6 && cp <= 0x0CC8) return true;
    if (cp >= 0x0CCA && cp <= 0x0CCD) return true;
    if (cp >= 0x0CD5 && cp <= 0x0CD6) return true;
    if (cp >= 0x0CE2 && cp <= 0x0CE3) return true;
    if (cp >= 0x0D00 && cp <= 0x0D03) return true;
    if (cp >= 0x0D3B && cp <= 0x0D3C) return true;
    if (cp >= 0x0D3E && cp <= 0x0D44) return true;
    if (cp >= 0x0D46 && cp <= 0x0D48) return true;
    if (cp >= 0x0D4A && cp <= 0x0D4D) return true;
    if (cp == 0x0D57) return true;
    if (cp >= 0x0D62 && cp <= 0x0D63) return true;
    // Sinhala
    if (cp >= 0x0D82 && cp <= 0x0D83) return true;
    if (cp == 0x0DCA) return true;
    if (cp >= 0x0DCF && cp <= 0x0DD4) return true;
    if (cp >= 0x0DD6 && cp <= 0x0DD6) return true;
    if (cp >= 0x0DD8 && cp <= 0x0DDF) return true;

    // ── Thai + Lao combining marks ──────────────────────────────
    if (cp >= 0x0E31 && cp <= 0x0E31) return true;
    if (cp >= 0x0E34 && cp <= 0x0E3A) return true;
    if (cp >= 0x0E47 && cp <= 0x0E4E) return true;
    if (cp >= 0x0EB1 && cp <= 0x0EB1) return true;
    if (cp >= 0x0EB4 && cp <= 0x0EBC) return true;
    if (cp >= 0x0EC8 && cp <= 0x0ECD) return true;

    // ── Tibetan combining marks ─────────────────────────────────
    if (cp >= 0x0F18 && cp <= 0x0F19) return true;
    if (cp == 0x0F35) return true;
    if (cp == 0x0F37) return true;
    if (cp == 0x0F39) return true;
    if (cp >= 0x0F3E && cp <= 0x0F3F) return true;
    if (cp >= 0x0F71 && cp <= 0x0F84) return true;
    if (cp >= 0x0F86 && cp <= 0x0F87) return true;
    if (cp >= 0x0F8D && cp <= 0x0F97) return true;
    if (cp >= 0x0F99 && cp <= 0x0FBC) return true;
    if (cp == 0x0FC6) return true;

    // ── Myanmar combining marks ─────────────────────────────────
    if (cp >= 0x102B && cp <= 0x103E) return true;
    if (cp >= 0x1056 && cp <= 0x1059) return true;
    if (cp >= 0x105E && cp <= 0x1060) return true;
    if (cp >= 0x1062 && cp <= 0x1064) return true;
    if (cp >= 0x1067 && cp <= 0x106D) return true;
    if (cp >= 0x1071 && cp <= 0x1074) return true;
    if (cp >= 0x1082 && cp <= 0x108D) return true;
    if (cp == 0x108F) return true;
    if (cp >= 0x109A && cp <= 0x109D) return true;

    // ── Ethiopic combining marks ────────────────────────────────
    if (cp >= 0x135D && cp <= 0x135F) return true;

    // ── Mongolian combining marks ───────────────────────────────
    if (cp >= 0x1712 && cp <= 0x1714) return true;
    if (cp >= 0x1732 && cp <= 0x1734) return true;
    if (cp >= 0x1752 && cp <= 0x1753) return true;
    if (cp >= 0x1772 && cp <= 0x1773) return true;

    // ── Tagalog, Hanunoo, Buhid, Tagbanwa + Khmer combining ───
    if (cp >= 0x17B4 && cp <= 0x17D3) return true; // Khmer
    if (cp == 0x17DD) return true;

    // ── Canadian Syllabics combining ────────────────────────────
    if (cp >= 0x18A9 && cp <= 0x18A9) return true;

    // ── Limbu combining marks ───────────────────────────────────
    if (cp >= 0x1920 && cp <= 0x192B) return true;
    if (cp >= 0x1930 && cp <= 0x193B) return true;

    // ── Tai Le, New Tai Lue combining marks ─────────────────────
    if (cp == 0x1A17 || cp == 0x1A18) return true;
    if (cp >= 0x1A55 && cp <= 0x1A5E) return true;
    if (cp >= 0x1A60 && cp <= 0x1A7C) return true;
    if (cp == 0x1A7F) return true;
    if (cp >= 0x1AB0 && cp <= 0x1ACE) return true;
    if (cp >= 0x1B00 && cp <= 0x1B04) return true;
    if (cp >= 0x1B34 && cp <= 0x1B44) return true;
    if (cp >= 0x1B6B && cp <= 0x1B73) return true;
    if (cp >= 0x1B80 && cp <= 0x1B82) return true;
    if (cp >= 0x1BA1 && cp <= 0x1BAD) return true;
    if (cp >= 0x1BE6 && cp <= 0x1BF3) return true;

    // ── CJK-related combining / enclosing ───────────────────────
    if (cp >= 0x302A && cp <= 0x302F) return true;
    if (cp == 0x3099 || cp == 0x309A) return true;

    // ── Enclosing combining marks ───────────────────────────────
    if (cp >= 0x20E0 && cp <= 0x20E3) return true;
    if (cp == 0x20E4) return true;

    // ── Wide coverage: Mn/Mc/Me General_Category catch-all ──────
    if (cp >= 0xA8E0 && cp <= 0xA8FF) return true;
    if (cp >= 0xA926 && cp <= 0xA92D) return true;
    if (cp >= 0xA947 && cp <= 0xA953) return true;
    if (cp >= 0xA980 && cp <= 0xA983) return true;
    if (cp >= 0xA9B3 && cp <= 0xA9C0) return true;
    if (cp >= 0xAA29 && cp <= 0xAA36) return true;
    if (cp >= 0xAA43 && cp <= 0xAA43) return true;
    if (cp >= 0xAA4C && cp <= 0xAA4D) return true;
    if (cp >= 0xAA7B && cp <= 0xAA7D) return true;
    if (cp >= 0xAAB0 && cp <= 0xAAB0) return true;
    if (cp >= 0xAAB2 && cp <= 0xAAB4) return true;
    if (cp >= 0xAAB7 && cp <= 0xAAB8) return true;
    if (cp >= 0xAABE && cp <= 0xAABF) return true;
    if (cp == 0xAAC1) return true;
    if (cp >= 0xABE3 && cp <= 0xABEA) return true;
    if (cp >= 0xABEC && cp <= 0xABED) return true;

    return false;
}

/// True for Regional Indicator code points (U+1F1E6-U+1F1FF).
/// Two consecutive RIs form a single grapheme cluster (flag emoji).
inline bool is_regional_indicator(char32_t cp) noexcept {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

/// True if `cp` has the Unicode Extended_Pictographic property.
/// Approximation covering the most common emoji and pictographic ranges
/// (SMP emoji blocks, Misc Symbols, Dingbats, and common BMP pictographs).
/// Used by GB11 (ZWJ emoji sequence) grapheme segmentation.
inline bool is_extended_pictographic(char32_t cp) noexcept {
    // ── SMP emoji / pictographic blocks ───────────────────────────
    // Supplemental Symbols and Pictographs, Emoticons, Transport,
    // and other pictorial SMP ranges.
    if (cp >= 0x1F000 && cp <= 0x1F9FF) return true;
    if (cp >= 0x1FA00 && cp <= 0x1FAFF) return true;

    // ── Misc Symbols and Pictographs (BMP) ────────────────────────
    if (cp >= 0x2600 && cp <= 0x27BF) return true;

    // ── Dingbats (BMP) ────────────────────────────────────────────
    if (cp >= 0x2700 && cp <= 0x27BF) return true;  // overlap, kept for clarity

    // ── Ornamental and other pictorial BMP ────────────────────────
    if (cp >= 0x1F300 && cp <= 0x1F5FF) return true;  // already in 1F000-1F9FF

    // ── Common BMP pictographs used as emoji ──────────────────────
    if (cp == 0x00A9 || cp == 0x00AE) return true;  // (c) (r)
    if (cp == 0x203C || cp == 0x2049) return true;  // !! !?
    if (cp == 0x2122 || cp == 0x2139) return true;  // TM i
    if (cp >= 0x2194 && cp <= 0x2199) return true;  // arrows
    if (cp >= 0x21A9 && cp <= 0x21AA) return true;  // curved arrows
    if (cp >= 0x231A && cp <= 0x231B) return true;  // watch, hourglass
    if (cp == 0x2328 || cp == 0x23CF) return true;
    if (cp >= 0x23E9 && cp <= 0x23F3) return true;
    if (cp >= 0x23F8 && cp <= 0x23FA) return true;
    if (cp == 0x24C2) return true;
    if (cp >= 0x25AA && cp <= 0x25AB) return true;
    if (cp == 0x25B6 || cp == 0x25C0) return true;
    if (cp >= 0x25FB && cp <= 0x25FE) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;  // already covered
    if (cp >= 0x2934 && cp <= 0x2935) return true;
    if (cp >= 0x2B05 && cp <= 0x2B07) return true;
    if (cp >= 0x2B1B && cp <= 0x2B1C) return true;
    if (cp == 0x2B50 || cp == 0x2B55) return true;
    if (cp == 0x3030 || cp == 0x303D) return true;
    if (cp == 0x3297 || cp == 0x3299) return true;

    return false;
}

/// Count the number of extended grapheme clusters in a UTF-8 string.
/// Combining marks, ZWJ sequences, emoji modifiers, and regional-indicator
/// flag pairs are not counted as separate clusters — they stay attached
/// to their base character.
///
/// Implements UAX #29 GB11: Extended_Pictographic Extend* ZWJ x
/// Extended_Pictographic.  An emoji ZWJ sequence (e.g. family emoji) is
/// counted as a single grapheme cluster.
inline size_t grapheme_cluster_count(std::string_view sv) {
    size_t count = 0;
    size_t ri_toggle = 0;  // 0 = expecting first RI, 1 = expecting second

    enum class GB11State { Normal, ExtPict, ExtPictZWJ };
    GB11State gb11 = GB11State::Normal;

    for (size_t i = 0; i < sv.size();) {
        const char32_t cp = utf8_decode_cp(sv, i);

        const bool is_ext  = is_grapheme_extend(cp);
        const bool is_ri   = is_regional_indicator(cp);
        const bool is_ep   = is_extended_pictographic(cp);
        const bool is_zwj  = (cp == 0x200D);

        if (is_ri) {
            if (ri_toggle == 0) ++count;  // first RI of a flag
            ri_toggle ^= 1;               // toggle 0<->1
            gb11 = GB11State::Normal;     // RI breaks any ZWJ chain
            continue;
        }

        if (is_zwj && gb11 == GB11State::ExtPict) {
            gb11 = GB11State::ExtPictZWJ;
            ri_toggle = 0;
            continue;
        }

        if (is_ext) {
            ri_toggle = 0;
            continue;
        }

        if (is_ep && gb11 == GB11State::ExtPictZWJ) {
            gb11 = GB11State::ExtPict;  // back to ExtPict (ZWJ consumed)
            ri_toggle = 0;
            continue;  // no new cluster
        }

        ++count;
        ri_toggle = 0;
        gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
    }

    return count;
}

/// Decode a UTF-8 code point at the given byte offset without advancing.
/// Returns the code point, or U+FFFD if the sequence is invalid or
/// would run past the end of the string_view.
inline char32_t utf8_decode_at(std::string_view sv, size_t offset) {
    if (offset >= sv.size()) return 0xFFFD;
    const unsigned char lead = static_cast<unsigned char>(sv[offset]);
    const size_t len = utf8_seq_len(lead);
    if (offset + len > sv.size()) return 0xFFFD;
    switch (len) {
        case 1: return lead;
        case 2: return ((lead & 0x1F) << 6) | (static_cast<unsigned char>(sv[offset + 1]) & 0x3F);
        case 3: return ((lead & 0x0F) << 12) | ((static_cast<unsigned char>(sv[offset + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset + 2]) & 0x3F);
        case 4: return ((lead & 0x07) << 18) | ((static_cast<unsigned char>(sv[offset + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(sv[offset + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(sv[offset + 3]) & 0x3F);
        default: return 0xFFFD;
    }
}

/// Find the byte offset after exactly N complete grapheme clusters.
/// Returns sv.size() if N exceeds the string length.  Safe for
/// std::string::substr() — never splits a grapheme cluster.
/// Implements UAX #29 GB11 for emoji ZWJ sequences and GB12/GB13 for
/// Regional Indicator flag pairs.
inline size_t grapheme_byte_offset_at(std::string_view sv, size_t n) {
    if (n == 0) return 0;

    size_t clusters = 0;
    size_t offset = 0;
    size_t ri_toggle = 0;

    enum class GB11State { Normal, ExtPict, ExtPictZWJ };
    GB11State gb11 = GB11State::Normal;

    auto consume_trailing_extenders = [&]() {
        while (offset < sv.size()) {
            const unsigned char next_lead = static_cast<unsigned char>(sv[offset]);
            const size_t next_len = utf8_seq_len(next_lead);
            if (offset + next_len > sv.size()) break;
            const char32_t next_cp = utf8_decode_at(sv, offset);
            const bool next_ext = is_grapheme_extend(next_cp);
            const bool next_zwj = (next_cp == 0x200D);
            const bool next_ep  = is_extended_pictographic(next_cp);

            if (next_ep && gb11 == GB11State::ExtPictZWJ) {
                gb11 = GB11State::ExtPict;
                offset += next_len;
                continue;
            }
            if (next_zwj && gb11 == GB11State::ExtPict) {
                gb11 = GB11State::ExtPictZWJ;
                offset += next_len;
                continue;
            }
            if (next_ext) {
                offset += next_len;
                continue;
            }
            break;
        }
    };

    while (offset < sv.size() && clusters < n) {
        const unsigned char lead = static_cast<unsigned char>(sv[offset]);
        const size_t len = utf8_seq_len(lead);
        if (offset + len > sv.size()) break;

        const char32_t cp = utf8_decode_at(sv, offset);

        const bool is_ext = is_grapheme_extend(cp);
        const bool is_ri  = is_regional_indicator(cp);
        const bool is_ep  = is_extended_pictographic(cp);
        const bool is_zwj = (cp == 0x200D);

        if (is_ri) {
            if (ri_toggle == 0) {
                ++clusters;
                if (clusters == n) {
                    offset += len;
                    if (offset < sv.size()) {
                        const unsigned char lead2 = static_cast<unsigned char>(sv[offset]);
                        const size_t len2 = utf8_seq_len(lead2);
                        if (offset + len2 <= sv.size()) {
                            const char32_t next_cp = utf8_decode_at(sv, offset);
                            if (is_regional_indicator(next_cp)) {
                                offset += len2;
                            }
                        }
                    }
                    gb11 = GB11State::Normal;
                    consume_trailing_extenders();
                    return offset;
                }
            }
            ri_toggle ^= 1;
            offset += len;
            gb11 = GB11State::Normal;
            continue;
        }

        if (is_zwj && gb11 == GB11State::ExtPict) {
            gb11 = GB11State::ExtPictZWJ;
            ri_toggle = 0;
            offset += len;
            continue;
        }

        if (is_ext) {
            ri_toggle = 0;
            offset += len;
            continue;
        }

        if (is_ep && gb11 == GB11State::ExtPictZWJ) {
            gb11 = GB11State::ExtPict;
            ri_toggle = 0;
            offset += len;
            continue;
        }

        ++clusters;
        ri_toggle = 0;
        gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
        if (clusters == n) {
            offset += len;
            consume_trailing_extenders();
            return offset;
        }
        offset += len;
    }

    return sv.size();
}

} // namespace chronon3d::detail
