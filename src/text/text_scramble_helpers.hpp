// ═══════════════════════════════════════════════════════════════════════════
// text_scramble_helpers.hpp — scramble/morph transition helpers (FASE 15)
// ═══════════════════════════════════════════════════════════════════════════
//
// Internal-only header (NOT installed).  Extracted from
// animated_text_document.cpp to keep the AnimatedTextDocument TU focused
// on the state machine (add_keyframe / sample_at).
//
// Provides deterministic UTF-8 code-point helpers and the Scramble/Morph
// text generators used by AnimatedTextDocument::sample_at().

#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/text/animated_text_document.hpp>  // MorphParams + EnterDirection

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::text_internal {

/// Deterministic pseudo-random u64 from (seed, index) using splitmix64.
[[nodiscard]] u64 scramble_hash(u64 seed, u64 index) noexcept;

/// Deterministic float in [0, 1) from (seed, index, frame).
[[nodiscard]] f32 scramble_float(u64 seed, u64 index, u64 frame_val) noexcept;

/// Count UTF-8 code points (lightweight, no full grapheme-cluster).
[[nodiscard]] size_t count_utf8_codepoints(std::string_view s) noexcept;

/// Extract the n-th UTF-8 code point from a string (0-indexed).
/// Returns the code point and sets out_len to its byte length.
[[nodiscard]] char32_t nth_codepoint(std::string_view s, size_t n, size_t& out_len) noexcept;

/// Encode a code point into a UTF-8 sequence. Returns byte length (1-4).
[[nodiscard]] size_t encode_utf8(char32_t cp, char out[4]) noexcept;

/// Generate scrambled text for the Scramble transition.
/// At mix=0: matches prev_text; at mix=1: matches next_text.
[[nodiscard]] std::string generate_scrambled_text(
    std::string_view prev_text,
    std::string_view next_text,
    u64 seed,
    std::string_view char_pool,
    f32 intensity,
    f32 mix,
    u64 frame_val);

/// Generate morphed text and character mapping for the Morph transition.
[[nodiscard]] std::string generate_morphed_text(
    std::string_view prev_text,
    std::string_view next_text,
    f32 mix,
    MorphParams::EnterDirection direction,
    const EasingCurve& easing,
    std::vector<std::pair<int, int>>& out_morph_map);

} // namespace chronon3d::text_internal
