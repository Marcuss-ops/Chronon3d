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


} // namespace chronon3d::detail
