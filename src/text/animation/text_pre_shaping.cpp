// ═══════════════════════════════════════════════════════════════════════════
// text_pre_shaping.cpp — PreShaping property evaluation (FASE 2a)
//
// Evaluates PreShaping properties (CharacterOffset) against the source
// text BEFORE HarfBuzz shaping, so glyph selection, kerning, ligatures,
// and bidi reordering all see the offset characters.
//
// Previously CharacterOffset was applied to GlyphInstanceState.character_offset
// after layout (architecturally wrong — different code points produce
// different glyph shapes, metrics, clusters).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animation/text_pre_shaping.hpp>

#include <algorithm>
#include <variant>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// apply_character_offset_to_source — Caesar-cipher on UTF-8 code points
// ═══════════════════════════════════════════════════════════════════════════

std::string apply_character_offset_to_source(
    std::string_view source,
    i32 offset
) {
    if (offset == 0 || source.empty()) {
        return std::string{source};
    }

    std::string result;
    result.reserve(source.size());

    const auto* data = source.data();
    const auto* end  = data + source.size();

    while (data < end) {
        // Decode UTF-8 lead byte.
        unsigned char lead = static_cast<unsigned char>(*data);

        if (lead < 0x80) {
            // Single-byte ASCII.
            char c = static_cast<char>(lead);

            // Apply Caesar shift to ASCII letters only.
            if (c >= 'A' && c <= 'Z') {
                i32 shifted = static_cast<i32>(c - 'A') + offset;
                // Euclidean modulo (handles negative offsets correctly).
                shifted = ((shifted % 26) + 26) % 26;
                c = static_cast<char>('A' + shifted);
            } else if (c >= 'a' && c <= 'z') {
                i32 shifted = static_cast<i32>(c - 'a') + offset;
                shifted = ((shifted % 26) + 26) % 26;
                c = static_cast<char>('a' + shifted);
            }
            // Non-letter ASCII: pass through unchanged.
            result.push_back(c);
            ++data;
        } else {
            // Multi-byte UTF-8 sequence: pass through unchanged.
            // Determine length from lead byte.
            size_t seq_len = 1;
            if ((lead & 0xE0) == 0xC0)       seq_len = 2;
            else if ((lead & 0xF0) == 0xE0)  seq_len = 3;
            else if ((lead & 0xF8) == 0xF0)  seq_len = 4;

            for (size_t i = 0; i < seq_len && data < end; ++i) {
                result.push_back(*data++);
            }
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// has_pre_shaping_properties
// ═══════════════════════════════════════════════════════════════════════════

bool has_pre_shaping_properties(
    const std::vector<TextAnimatorSpec>& animators
) {
    for (const auto& anim : animators) {
        if (!anim.enabled) continue;
        for (const auto& prop : anim.properties) {
            if (std::holds_alternative<CharacterOffsetProperty>(prop)) {
                return true;
            }
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_pre_shaping_source
// ═══════════════════════════════════════════════════════════════════════════

std::string evaluate_pre_shaping_source(
    const std::vector<TextAnimatorSpec>& animators,
    std::string_view original_source
) {
    i32 total_offset = 0;

    for (const auto& anim : animators) {
        if (!anim.enabled) continue;
        for (const auto& prop : anim.properties) {
            if (auto* cop = std::get_if<CharacterOffsetProperty>(&prop)) {
                total_offset += cop->offset;
            }
        }
    }

    if (total_offset == 0) {
        return std::string{original_source};
    }

    return apply_character_offset_to_source(original_source, total_offset);
}

} // namespace chronon3d
