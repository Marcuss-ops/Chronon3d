// ═══════════════════════════════════════════════════════════════════════════
// text_scramble_helpers.cpp — scramble/morph transition helpers (FASE 15)
// ═══════════════════════════════════════════════════════════════════════════
//
// Extracted from animated_text_document.cpp's anonymous namespace.
// All 7 helpers are in namespace chronon3d::text_internal.

#include "text_scramble_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace chronon3d::text_internal {

u64 scramble_hash(u64 seed, u64 index) noexcept {
    u64 x = seed;
    x ^= index + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

f32 scramble_float(u64 seed, u64 index, u64 frame_val) noexcept {
    const u64 h = scramble_hash(seed, index ^ (frame_val * 0x9e3779b97f4a7c15ULL));
    constexpr f64 kInvU64Max = 1.0 / static_cast<f64>(UINT64_MAX);
    return static_cast<f32>(static_cast<f64>(h) * kInvU64Max);
}

size_t count_utf8_codepoints(std::string_view s) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++count;
    }
    return count;
}

char32_t nth_codepoint(std::string_view s, size_t n, size_t& out_len) noexcept {
    size_t count = 0;
    size_t i = 0;
    while (i < s.size() && count < n) {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((lead & 0xE0) == 0xC0) len = 2;
        else if ((lead & 0xF0) == 0xE0) len = 3;
        else if ((lead & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        if (count + 1 == n) {
            out_len = len;
            if (len == 1) return static_cast<char32_t>(lead);
            if (len == 2) return static_cast<char32_t>(((lead & 0x1F) << 6) | (static_cast<unsigned char>(s[i+1]) & 0x3F));
            if (len == 3) return static_cast<char32_t>(((lead & 0x0F) << 12) | ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i+2]) & 0x3F));
            return static_cast<char32_t>(((lead & 0x07) << 18) | ((static_cast<unsigned char>(s[i+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[i+2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i+3]) & 0x3F));
        }
        ++count;
        i += len;
    }
    out_len = (i < s.size()) ? static_cast<size_t>(static_cast<unsigned char>(s[i]) < 0x80 ? 1 : 2) : 1;
    return 0xFFFD;
}

size_t encode_utf8(char32_t cp, char out[4]) noexcept {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

std::string generate_scrambled_text(
    std::string_view prev_text,
    std::string_view next_text,
    u64 seed,
    std::string_view char_pool,
    f32 intensity,
    f32 mix,
    u64 frame_val)
{
    if (char_pool.empty()) return std::string(next_text);
    if (mix <= 0.0f) return std::string(prev_text);
    if (mix >= 1.0f) return std::string(next_text);

    const size_t prev_cp = count_utf8_codepoints(prev_text);
    const size_t next_cp = count_utf8_codepoints(next_text);
    const size_t max_cp = std::max(prev_cp, next_cp);

    struct CpInfo { char32_t cp; size_t len; };
    auto build_cps = [](std::string_view s) {
        std::vector<CpInfo> cps;
        const size_t n = count_utf8_codepoints(s);
        cps.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            size_t len;
            char32_t cp = nth_codepoint(s, i, len);
            cps.push_back({cp, len});
        }
        return cps;
    };
    const auto prev_cps = build_cps(prev_text);
    const auto next_cps = build_cps(next_text);
    const auto pool_cps = build_cps(char_pool);

    const f32 p_substitute = intensity * 4.0f * mix * (1.0f - mix);

    std::string result;
    result.reserve(std::max(prev_text.size(), next_text.size()) + max_cp * 2);

    for (size_t i = 0; i < max_cp; ++i) {
        const f32 h = scramble_float(seed, static_cast<u64>(i), frame_val);
        const bool use_next = (h < mix);

        char32_t cp;
        size_t len;

        if (use_next && i < next_cps.size()) {
            cp = next_cps[i].cp;
            len = next_cps[i].len;
        } else if (!use_next && i < prev_cps.size()) {
            cp = prev_cps[i].cp;
            len = prev_cps[i].len;
        } else {
            if (i < next_cps.size()) {
                cp = next_cps[i].cp;
                len = next_cps[i].len;
            } else if (i < prev_cps.size()) {
                cp = prev_cps[i].cp;
                len = prev_cps[i].len;
            } else {
                continue;
            }
        }

        const f32 h2 = scramble_float(seed ^ 0x5555, static_cast<u64>(i), frame_val);
        if (h2 < p_substitute && !pool_cps.empty()) {
            const size_t pool_idx = static_cast<size_t>(
                scramble_float(seed ^ 0xAAAA, static_cast<u64>(i), frame_val)
                * static_cast<f32>(pool_cps.size()));
            cp = pool_cps[pool_idx].cp;
        }

        char buf[4];
        const size_t blen = encode_utf8(cp, buf);
        result.append(buf, blen);
    }

    return result;
}

std::string generate_morphed_text(
    std::string_view prev_text,
    std::string_view next_text,
    f32 mix,
    MorphParams::EnterDirection direction,
    const EasingCurve& easing,
    std::vector<std::pair<int, int>>& out_morph_map)
{
    out_morph_map.clear();

    if (mix <= 0.0f) return std::string(prev_text);
    if (mix >= 1.0f) return std::string(next_text);

    const size_t prev_cp = count_utf8_codepoints(prev_text);
    const size_t next_cp = count_utf8_codepoints(next_text);
    const size_t max_cp = std::max(prev_cp, next_cp);

    struct CpInfo { char32_t cp; size_t len; };
    auto build_cps = [](std::string_view s) {
        std::vector<CpInfo> cps;
        const size_t n = count_utf8_codepoints(s);
        cps.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            size_t len;
            char32_t cp = nth_codepoint(s, i, len);
            cps.push_back({cp, len});
        }
        return cps;
    };
    const auto prev_cps = build_cps(prev_text);
    const auto next_cps = build_cps(next_text);

    auto char_normalized_pos = [&](size_t char_idx, bool /*is_new_char*/) -> f32 {
        const size_t total = max_cp > 0 ? max_cp : 1;
        if (total <= 1) return 0.5f;
        switch (direction) {
            case MorphParams::EnterDirection::FromRight:
                return static_cast<f32>(total - 1 - char_idx) / static_cast<f32>(total - 1);
            case MorphParams::EnterDirection::FromLeft:
                return static_cast<f32>(char_idx) / static_cast<f32>(total - 1);
            case MorphParams::EnterDirection::FromCenter: {
                const f32 center = static_cast<f32>(total - 1) * 0.5f;
                const f32 dist = std::abs(static_cast<f32>(char_idx) - center);
                return dist / (center + 0.5f);
            }
        }
        return static_cast<f32>(char_idx) / static_cast<f32>(total - 1);
    };

    std::string result;
    result.reserve(std::max(prev_text.size(), next_text.size()) + max_cp * 2);

    for (size_t i = 0; i < max_cp; ++i) {
        const char32_t prev_c = (i < prev_cps.size()) ? prev_cps[i].cp : 0;
        const char32_t next_c = (i < next_cps.size()) ? next_cps[i].cp : 0;

        if (prev_c != 0 && next_c != 0 && prev_c == next_c) {
            char buf[4];
            const size_t blen = encode_utf8(next_c, buf);
            result.append(buf, blen);
            out_morph_map.emplace_back(static_cast<int>(i), static_cast<int>(i));
        } else if (prev_c != 0 && next_c != 0 && prev_c != next_c) {
            const f32 char_pos = char_normalized_pos(i, true);
            const f32 eased = easing.apply(char_pos);
            if (mix >= eased) {
                char buf[4];
                const size_t blen = encode_utf8(next_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(-1, static_cast<int>(i));
            } else {
                char buf[4];
                const size_t blen = encode_utf8(prev_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(static_cast<int>(i), -1);
            }
        } else if (prev_c != 0 && next_c == 0) {
            const f32 char_pos = char_normalized_pos(i, false);
            const f32 eased = easing.apply(char_pos);
            if (mix < eased) {
                char buf[4];
                const size_t blen = encode_utf8(prev_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(static_cast<int>(i), -1);
            } else {
                result.push_back(' ');
                out_morph_map.emplace_back(-1, -1);
            }
        } else if (prev_c == 0 && next_c != 0) {
            const f32 char_pos = char_normalized_pos(i, true);
            const f32 eased = easing.apply(char_pos);
            if (mix >= eased) {
                char buf[4];
                const size_t blen = encode_utf8(next_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(-1, static_cast<int>(i));
            } else {
                result.push_back(' ');
                out_morph_map.emplace_back(-1, -1);
            }
        }
    }

    return result;
}

} // namespace chronon3d::text_internal
