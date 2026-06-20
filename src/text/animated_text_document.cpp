// ═══════════════════════════════════════════════════════════════════════════
// animated_text_document.cpp — AnimatedTextDocument implementation
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// Deterministic hash helper (splitmix64-based)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Deterministic pseudo-random u64 from (seed, index).
/// Uses splitmix64 which is fast and well-distributed.
/// Identical to the implementation in glyph_selector.cpp — kept local
/// to avoid a dependency from animated_text_document → glyph_selector.
[[nodiscard]] u64 scramble_hash(u64 seed, u64 index) noexcept {
    u64 x = seed;
    x ^= index + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

/// Deterministic float in [0, 1) from (seed, index, frame).
[[nodiscard]] f32 scramble_float(u64 seed, u64 index, u64 frame_val) noexcept {
    const u64 h = scramble_hash(seed, index ^ (frame_val * 0x9e3779b97f4a7c15ULL));
    constexpr f64 kInvU64Max = 1.0 / static_cast<f64>(UINT64_MAX);
    return static_cast<f32>(static_cast<f64>(h) * kInvU64Max);
}

/// Count grapheme clusters in a UTF-8 string (inline, lightweight).
/// Avoids the full dependency chain of grapheme_cluster_count() from
/// text_layout_engine.hpp — we only need a rough count for scramble.
[[nodiscard]] size_t count_utf8_codepoints(std::string_view s) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++count;
    }
    return count;
}

/// Extract the n-th UTF-8 code point from a string (0-indexed).
/// Returns the code point and sets out_len to its byte length.
/// Returns 0xFFFD (replacement char) on out-of-bounds or decode error.
[[nodiscard]] char32_t nth_codepoint(std::string_view s, size_t n, size_t& out_len) noexcept {
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

/// Encode a code point into a UTF-8 sequence.  Returns the byte length (1-4).
[[nodiscard]] size_t encode_utf8(char32_t cp, char out[4]) noexcept {
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

/// Generate scrambled text for the Scramble transition.
///
/// Algorithm:
///   1. Take the longer of prev_text and next_text as the template length.
///   2. Pre-build code point arrays for O(1) per-char lookup.
///   3. For each character position i:
///      - Compute deterministic float h = scramble_float(seed, i, frame).
///      - Compute substitution probability p based on mix and intensity.
///        p peaks at mix=0.5 (max chaos), falls to 0 at mix=0 and mix=1.
///      - If h < p, substitute with a random character from char_pool.
///      - Otherwise, use the character from the document closer to the
///        current mix (blend: at mix<0.5 use prev, at mix>=0.5 use next).
///   4. At mix=0: text matches prev_doc; at mix=1: text matches next_doc.
[[nodiscard]] std::string generate_scrambled_text(
    std::string_view prev_text,
    std::string_view next_text,
    u64 seed,
    std::string_view char_pool,
    f32 intensity,
    f32 mix,
    u64 frame_val
) {
    if (char_pool.empty()) return std::string(next_text);
    if (mix <= 0.0f) return std::string(prev_text);
    if (mix >= 1.0f) return std::string(next_text);

    const size_t prev_cp = count_utf8_codepoints(prev_text);
    const size_t next_cp = count_utf8_codepoints(next_text);
    const size_t max_cp = std::max(prev_cp, next_cp);

    // Pre-build code point arrays for O(1) per-char lookup.
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

    // Probability curve: bell-shaped, peaks at mix=0.5
    // p = intensity * 4 * mix * (1 - mix)
    // This gives: p(0)=0, p(0.5)=intensity, p(1)=0
    const f32 p_substitute = intensity * 4.0f * mix * (1.0f - mix);

    std::string result;
    result.reserve(std::max(prev_text.size(), next_text.size()) + max_cp * 2);

    for (size_t i = 0; i < max_cp; ++i) {
        const f32 h = scramble_float(seed, static_cast<u64>(i), frame_val);

        // Which document to prefer at this mix (blend toward next as mix increases)
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
            // Position doesn't exist in the preferred doc — use the other
            if (i < next_cps.size()) {
                cp = next_cps[i].cp;
                len = next_cps[i].len;
            } else if (i < prev_cps.size()) {
                cp = prev_cps[i].cp;
                len = prev_cps[i].len;
            } else {
                continue;  // shouldn't happen
            }
        }

        // Second hash determines if this character gets substituted
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

/// Generate morphed text and character mapping for the Morph transition.
///
/// Algorithm:
///   1. Find common characters (same code point at same position) — they persist.
///   2. Compute a per-character normalized position based on the configured
///      EnterDirection and the text length.
///   3. Apply the easing curve to each character's normalized position.
///   4. Characters whose eased position is below the current mix are visible;
///      others are hidden (replaced with space or shown as prev).
///   5. New characters enter from the configured direction; removed ones exit.
///   6. Build morph_map: (prev_index, next_index) pairs.
[[nodiscard]] std::string generate_morphed_text(
    std::string_view prev_text,
    std::string_view next_text,
    f32 mix,
    MorphParams::EnterDirection direction,
    const EasingCurve& easing,
    std::vector<std::pair<int, int>>& out_morph_map
) {
    out_morph_map.clear();

    if (mix <= 0.0f) return std::string(prev_text);
    if (mix >= 1.0f) return std::string(next_text);

    const size_t prev_cp = count_utf8_codepoints(prev_text);
    const size_t next_cp = count_utf8_codepoints(next_text);
    const size_t max_cp = std::max(prev_cp, next_cp);

    // Pre-build code point arrays for O(1) lookup.
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

    // ── Compute per-character normalized position based on direction ───
    // Each character gets a position in [0, 1] representing when it should
    // appear during the morph transition.  The easing curve is applied
    // per-character, producing staggered entry.
    auto char_normalized_pos = [&](size_t char_idx, bool is_new_char) -> f32 {
        const size_t total = max_cp > 0 ? max_cp : 1;
        if (total <= 1) return 0.5f;
        switch (direction) {
            case MorphParams::EnterDirection::FromRight:
                // Last characters enter first (right-to-left wave)
                return static_cast<f32>(total - 1 - char_idx) / static_cast<f32>(total - 1);
            case MorphParams::EnterDirection::FromLeft:
                // First characters enter first (left-to-right wave)
                return static_cast<f32>(char_idx) / static_cast<f32>(total - 1);
            case MorphParams::EnterDirection::FromCenter:
                // Center characters enter first, edges last.
                // Smaller dist → smaller char_pos → fires earlier.
                {
                    const f32 center = static_cast<f32>(total - 1) * 0.5f;
                    const f32 dist = std::abs(static_cast<f32>(char_idx) - center);
                    return dist / (center + 0.5f);  // 0 at center, ~1 at edges
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
            // Common character — persists at this position
            char buf[4];
            const size_t blen = encode_utf8(next_c, buf);
            result.append(buf, blen);
            out_morph_map.emplace_back(static_cast<int>(i), static_cast<int>(i));
        } else if (prev_c != 0 && next_c != 0 && prev_c != next_c) {
            // Different character — staggered crossfade
            const f32 char_pos = char_normalized_pos(i, true);
            const f32 eased = easing.apply(char_pos);
            if (mix >= eased) {
                // Show the new character
                char buf[4];
                const size_t blen = encode_utf8(next_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(-1, static_cast<int>(i));
            } else {
                // Still showing the old character
                char buf[4];
                const size_t blen = encode_utf8(prev_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(static_cast<int>(i), -1);
            }
        } else if (prev_c != 0 && next_c == 0) {
            // Character being removed — staggered exit.
            // Uses the same per-character timing as entering characters.
            // Shows the old character until mix passes its threshold, then disappears.
            const f32 char_pos = char_normalized_pos(i, false);
            const f32 eased = easing.apply(char_pos);
            if (mix < eased) {
                // Still visible (hasn't reached exit threshold yet)
                char buf[4];
                const size_t blen = encode_utf8(prev_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(static_cast<int>(i), -1);
            } else {
                // Exited — emit space placeholder
                result.push_back(' ');
                out_morph_map.emplace_back(-1, -1);
            }
        } else if (prev_c == 0 && next_c != 0) {
            // New character entering — staggered entry
            const f32 char_pos = char_normalized_pos(i, true);
            const f32 eased = easing.apply(char_pos);
            if (mix >= eased) {
                char buf[4];
                const size_t blen = encode_utf8(next_c, buf);
                result.append(buf, blen);
                out_morph_map.emplace_back(-1, static_cast<int>(i));
            } else {
                // Not visible yet — emit a space as placeholder
                result.push_back(' ');
                out_morph_map.emplace_back(-1, -1);
            }
        }
    }

    return result;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// add_keyframe
// ═══════════════════════════════════════════════════════════════════════════

void AnimatedTextDocument::add_keyframe(SourceTextKeyframe kf) {
    // Overwrite duplicate frame.
    auto it = std::lower_bound(m_keyframes.begin(), m_keyframes.end(),
                               kf.frame,
                               [](const SourceTextKeyframe& a, Frame f) {
                                   return a.frame < f;
                               });
    if (it != m_keyframes.end() && it->frame == kf.frame) {
        *it = std::move(kf);
        return;
    }
    m_keyframes.insert(it, std::move(kf));
}

// ═══════════════════════════════════════════════════════════════════════════
// sample_at
// ═══════════════════════════════════════════════════════════════════════════

ActiveTextState AnimatedTextDocument::sample_at(Frame frame) const {
    ActiveTextState result;

    if (m_keyframes.empty()) {
        return result;
    }

    const size_t n = m_keyframes.size();

    // ── Before the first keyframe: hold the first document ────────────
    if (frame < m_keyframes[0].frame) {
        result.active = &m_keyframes[0].document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        return result;
    }

    // ── Find the keyframe pair: prev (<= frame) and next (> frame) ────
    // Linear scan — keyframe lists are small (typically < 20 entries).
    size_t prev_idx = 0;
    for (size_t i = 0; i < n; ++i) {
        if (m_keyframes[i].frame <= frame) {
            prev_idx = i;
        } else {
            break;
        }
    }

    const SourceTextKeyframe& prev_kf = m_keyframes[prev_idx];
    const bool has_next = (prev_idx + 1 < n);
    const SourceTextKeyframe* next_kf = has_next ? &m_keyframes[prev_idx + 1] : nullptr;

    // ── At or after the last keyframe: hold the last document ──────────
    if (!has_next) {
        result.active = &prev_kf.document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        return result;
    }        // ── Exactly at the next keyframe: the next document becomes active ──
        if (frame == next_kf->frame) {
            // At the boundary, the incoming keyframe's document takes over.
            result.active = &next_kf->document;
            // At the boundary, no transition is in progress.
            // next_kf->transition describes FROM next_kf TO its successor,
            // which hasn't started yet, so we report Hold.
            result.transition = SourceTextTransition::Hold;
            result.mix = 0.0f;
            result.crossfade_from = nullptr;
            return result;
    }

    // ── Between keyframes ──────────────────────────────────────────────
    // The transition type is determined by the PREVIOUS keyframe
    // (prev_kf.transition) — it governs how we move FROM prev TO next.

    // Compute generic gap progress once (shared by CrossfadeLayouts, Scramble, Morph).
    const float gap = static_cast<float>(next_kf->frame - prev_kf.frame);
    const float pos  = static_cast<float>(frame - prev_kf.frame);
    const float raw_mix = (gap > 0.0f) ? (pos / gap) : 1.0f;
    const float mix = std::clamp(raw_mix, 0.0f, 1.0f);

    switch (prev_kf.transition) {

    case SourceTextTransition::Hold:
        // Hold: the previous document stays visible until the NEXT keyframe
        // arrives.  No crossfade.
        result.active = &prev_kf.document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        break;

    case SourceTextTransition::Cut:
        // Cut: instant switch to the next document.
        result.active = &next_kf->document;
        result.transition = SourceTextTransition::Cut;
        result.mix = 0.0f;
        break;

    case SourceTextTransition::CrossfadeLayouts: {
        result.active = &next_kf->document;
        result.crossfade_from = &prev_kf.document;
        result.transition = SourceTextTransition::CrossfadeLayouts;
        result.mix = mix;
        break;
    }

    case SourceTextTransition::Scramble: {
        // During the scramble phase, both documents are referenced.
        result.active = &next_kf->document;
        result.crossfade_from = &prev_kf.document;
        result.transition = SourceTextTransition::Scramble;
        result.mix = mix;

        // Generate the per-frame scrambled text.
        // Uses the PREVIOUS keyframe's scramble_params (the one that
        // declared Scramble as its outgoing transition).
        result.transition_text = generate_scrambled_text(
            prev_kf.document.utf8,
            next_kf->document.utf8,
            prev_kf.scramble_params.seed,
            prev_kf.scramble_params.char_pool,
            prev_kf.scramble_params.intensity,
            mix,
            static_cast<u64>(frame)
        );
        break;
    }

    case SourceTextTransition::Morph: {
        result.active = &next_kf->document;
        result.crossfade_from = &prev_kf.document;
        result.transition = SourceTextTransition::Morph;
        result.mix = mix;

        result.transition_text = generate_morphed_text(
            prev_kf.document.utf8,
            next_kf->document.utf8,
            mix,
            prev_kf.morph_params.direction,
            prev_kf.morph_params.easing,
            result.morph_map
        );
        break;
    }

    default:
        result.active = &prev_kf.document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        break;
    }

    return result;
}

} // namespace chronon3d
