#include <chronon3d/text/glyph_selector.hpp>

// FASE 23: `glyph_selector_random.hpp` is no longer needed in this TU.
// The Random-order branch of `detail::apply_selector_order` (which calls
// `get_or_build_permutation`) lives in glyph_selector_math.cpp, and the
// random-prebake branch of `compile_selector` lives in
// glyph_selector_compile.cpp.  Both transitively include
// glyph_selector_random.hpp themselves.
//
// `numeric` / `unordered_map` were only used by the moved-out Compile +
// Evaluate blocks; the slim glyph_selector.cpp keeps `algorithm` (used
// by `detail::{decode_utf8_codepoint_from, is_unicode_whitespace}` via
// `<algorithm>` if those paths include it here -- safety net) and
// `cmath` (no direct usage but std::floor / std::clamp are pulled
// transitively through glyph_selector.hpp).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextUnitMap builder
// ═══════════════════════════════════════════════════════════════════════════

TextUnitMap build_text_unit_map(
    const PlacedGlyphRun& placed,
    std::string_view source
) {
    TextUnitMap map;

    const size_t glyph_count = placed.glyphs.size();
    if (glyph_count == 0) return map;

    // ── Size all three arrays ────────────────────────────────────────
    map.glyph_to_grapheme.resize(glyph_count);
    map.glyph_to_word.resize(glyph_count);
    map.glyph_to_line.resize(glyph_count);

    // ── Grapheme mapping: use cluster info ───────────────────────────
    {
        u32 grapheme_idx = 0;
        u32 prev_cluster = UINT32_MAX;

        for (size_t g = 0; g < glyph_count; ++g) {
            u32 cluster = placed.glyphs[g].cluster;
            if (cluster != prev_cluster) {
                grapheme_idx++;
                prev_cluster = cluster;
            }
            map.glyph_to_grapheme[g] = grapheme_idx - 1;
        }
        map.grapheme_count = grapheme_idx;
    }

    // ── Word and line mapping: walk source text byte-by-byte ─────────
    {
        u32 word_idx = 0;
        u32 line_idx = 0;
        bool in_word = false;

        constexpr u32 kNoUnit = UINT32_MAX;
        const size_t src_len = source.size();
        std::vector<u32> byte_to_word(src_len + 1, kNoUnit);
        std::vector<u32> byte_to_line(src_len + 1, 0);

        for (size_t b = 0; b < src_len;) {
            size_t cp_len = 1;
            const char32_t cp = detail::decode_utf8_codepoint_from(source, b, cp_len);
            if (b + cp_len > src_len) cp_len = src_len - b;

            bool is_newline = (cp == u'\n' || cp == 0x2028 || cp == 0x2029);
            bool is_space = (!is_newline && detail::is_unicode_whitespace(cp));

            if (is_newline) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = kNoUnit;
                }
                line_idx++;
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_line[b + j] = line_idx;
                }
            } else if (is_space) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = kNoUnit;
                    byte_to_line[b + j] = line_idx;
                }
            } else {
                in_word = true;
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = word_idx;
                    byte_to_line[b + j] = line_idx;
                }
            }

            b += cp_len;
        }

        for (size_t b = src_len; b <= src_len; ++b) {
            byte_to_word[b] = kNoUnit;
            byte_to_line[b] = line_idx;
        }

        if (in_word) word_idx++;
        map.word_count = word_idx;
        map.line_count = line_idx + 1;

        constexpr u32 kNotFound = UINT32_MAX;
        map.first_glyph_for_word.assign(word_idx, kNotFound);
        map.first_glyph_for_line.assign(line_idx + 1, kNotFound);

        for (size_t g = 0; g < glyph_count; ++g) {
            size_t byte_off = placed.glyphs[g].byte_offset;
            if (byte_off > src_len) byte_off = src_len;
            map.glyph_to_word[g] = byte_to_word[byte_off];
            map.glyph_to_line[g] = byte_to_line[byte_off];

            const u32 w = map.glyph_to_word[g];
            if (w != kNoUnit && map.first_glyph_for_word[w] == kNotFound) {
                map.first_glyph_for_word[w] = static_cast<u32>(g);
            }
            const u32 l = map.glyph_to_line[g];
            if (l < map.first_glyph_for_line.size() && map.first_glyph_for_line[l] == kNotFound) {
                map.first_glyph_for_line[l] = static_cast<u32>(g);
            }
        }
    }

    return map;
}

// detail::evaluate_selector_shape + detail::apply_selector_order
//                                                              → glyph_selector_math.cpp
// detail::should_exclude_unit + compile_selector / compile_selectors /
// evaluate_compiled_selector / evaluate_compiled_selectors    → glyph_selector_compile.cpp

// ═══════════════════════════════════════════════════════════════════════════
// Public API (legacy — delegates to compiled path)
// ═══════════════════════════════════════════════════════════════════════════

SelectorWeight evaluate_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed
) {
    auto compiled = compile_selector(spec, unit_map, source, placed);
    return evaluate_compiled_selector(compiled, unit_map, glyph_index, time);
}

SelectorWeight evaluate_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed
) {
    auto compiled = compile_selectors(specs, unit_map, source, placed);
    return evaluate_compiled_selectors(compiled, unit_map, glyph_index, time);
}

} // namespace chronon3d
