#include <chronon3d/text/glyph_selector.hpp>

#include <algorithm>
#include <cmath>

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
    // Each HarfBuzz cluster maps to one grapheme unit.
    // Multiple glyphs within the same cluster share the same grapheme index.
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
    // For every glyph, we look at the source text at its byte_offset to
    // determine which word and line it belongs to.
    {
        u32 word_idx = 0;
        u32 line_idx = 0;
        bool in_word = false;

        // Build a per-byte word/line index lookup for O(1) mapping.
        const size_t src_len = source.size();
        std::vector<u32> byte_to_word(src_len + 1, 0);
        std::vector<u32> byte_to_line(src_len + 1, 0);

        for (size_t b = 0; b < src_len;) {
            // Decode UTF-8 to find code point length
            unsigned char lead = static_cast<unsigned char>(source[b]);
            size_t cp_len = 1;
            if ((lead & 0x80u) != 0) {
                if ((lead & 0xE0u) == 0xC0u) cp_len = 2;
                else if ((lead & 0xF0u) == 0xE0u) cp_len = 3;
                else if ((lead & 0xF8u) == 0xF0u) cp_len = 4;
            }
            if (b + cp_len > src_len) cp_len = src_len - b;

            // Check if this byte starts a whitespace or newline
            bool is_space = (cp_len == 1 && (lead == ' ' || lead == '\t'));
            bool is_newline = (cp_len == 1 && lead == '\n');

            if (is_newline) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = word_idx;
                }
                word_idx++;
                line_idx++;
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_line[b + j] = line_idx;
                }
            } else if (is_space) {
                if (in_word) { word_idx++; in_word = false; }
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = word_idx;
                    byte_to_line[b + j] = line_idx;
                }
                word_idx++;
            } else {
                in_word = true;
                for (size_t j = 0; j < cp_len && (b + j) < src_len; ++j) {
                    byte_to_word[b + j] = word_idx;
                    byte_to_line[b + j] = line_idx;
                }
            }

            b += cp_len;
        }

        // Fill trailing bytes
        for (size_t b = src_len; b <= src_len; ++b) {
            byte_to_word[b] = word_idx;
            byte_to_line[b] = line_idx;
        }

        if (in_word) word_idx++;
        map.word_count = word_idx;
        map.line_count = line_idx + 1;

        // Map each glyph to its word/line via its byte offset
        for (size_t g = 0; g < glyph_count; ++g) {
            size_t byte_off = placed.glyphs[g].byte_offset;
            if (byte_off > src_len) byte_off = src_len;
            map.glyph_to_word[g] = byte_to_word[byte_off];
            map.glyph_to_line[g] = byte_to_line[byte_off];
        }
    }

    return map;
}

// ═══════════════════════════════════════════════════════════════════════════
// Shape evaluation
// ═══════════════════════════════════════════════════════════════════════════

namespace detail {

f32 evaluate_selector_shape(
    TextSelectorShape shape,
    f32 unit_position,
    f32 start,
    f32 end
) {
    // Clamp inputs
    unit_position = std::clamp(unit_position, 0.0f, 1.0f);
    start = std::clamp(start, 0.0f, 1.0f);
    end   = std::clamp(end, 0.0f, 1.0f);

    // Handle reversed range (start > end)
    if (start > end) {
        std::swap(start, end);
        unit_position = 1.0f - unit_position;
    }

    const f32 range = end - start;

    // Outside the active window — handle edge cases per shape
    if (unit_position <= start) {
        // RampDown is fully active at the left edge
        if (shape == TextSelectorShape::RampDown) return 1.0f;
        return 0.0f;
    }
    if (unit_position >= end) {
        // RampUp is fully active at the right edge
        if (shape == TextSelectorShape::RampUp) return 1.0f;
        return 0.0f;
    }

    // Normalised position within [start, end]
    const f32 t = (range > 1e-7f) ? ((unit_position - start) / range) : 0.5f;

    switch (shape) {
        case TextSelectorShape::Square:
            return 1.0f;

        case TextSelectorShape::RampUp:
            return t;

        case TextSelectorShape::RampDown:
            return 1.0f - t;

        case TextSelectorShape::Triangle: {
            // Peak at t=0.5, linear ramp up then down
            if (t <= 0.5f) return 2.0f * t;
            return 2.0f * (1.0f - t);
        }

        case TextSelectorShape::Round: {
            // Bell shape: peak at centre, smoothstep on both sides
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
                return 3.0f * u * u - 2.0f * u * u * u;
            }
            const f32 u = 2.0f * (1.0f - t);
            return 3.0f * u * u - 2.0f * u * u * u;
        }

        case TextSelectorShape::Smooth: {
            // Cubic ease: smoother version, bell-curve
            // Uses smoothstep on both sides independently
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
                // Eased ramp-up: 1 - (1-u)³
                const f32 one_minus_u = 1.0f - u;
                return 1.0f - one_minus_u * one_minus_u * one_minus_u;
            }
            const f32 u = 2.0f * (1.0f - t);
            const f32 one_minus_u = 1.0f - u;
            return 1.0f - one_minus_u * one_minus_u * one_minus_u;
        }
    }

    return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Order application
// ═══════════════════════════════════════════════════════════════════════════

u32 apply_selector_order(
    TextSelectorOrder order,
    u32 original_index,
    u32 total_units,
    u64 random_seed
) {
    if (total_units <= 1) return 0;

    switch (order) {
        case TextSelectorOrder::Forward:
            return original_index;

        case TextSelectorOrder::Reverse:
            return total_units - 1 - original_index;

        case TextSelectorOrder::FromCenter: {
            // Distribute outward from centre.
            // Even count (4): [0,1,2,3] → ordered output [1,0,2,3]
            //   index 0→pos 1, index 1→pos 0, index 2→pos 2, index 3→pos 3
            // Odd count (5):  [0,1,2,3,4] → ordered output [2,1,3,0,4]
            //   index 2→pos 0, index 1→pos 1, index 3→pos 2, index 0→pos 3, index 4→pos 4
            const u32 mid = total_units / 2;

            if (total_units % 2 == 1) {
                // Odd count: centre element goes to position 0
                if (original_index == mid) return 0;

                if (original_index < mid) {
                    // Left side: fill positions 1, 3, 5... (odd) outward from centre
                    const u32 dist_from_centre = mid - 1 - original_index;
                    return dist_from_centre * 2 + 1;
                }
                // Right side: fill positions 2, 4, 6... (even) outward from centre
                const u32 dist_from_centre = original_index - mid - 1;
                return dist_from_centre * 2 + 2;
            }

            // Even count: no centre element
            if (original_index < mid) {
                // Left side: fill positions 0, 1, 2... outward from centre
                return mid - 1 - original_index;
            }
            // Right side: keep original relative order
            return original_index;
        }

        case TextSelectorOrder::ToCenter: {
            // Opposite of FromCenter
            const u32 from_centre = apply_selector_order(
                TextSelectorOrder::FromCenter, original_index, total_units, random_seed
            );
            return total_units - 1 - from_centre;
        }

        case TextSelectorOrder::Random: {
            // Deterministic shuffle using Fisher-Yates via hash
            // The hash determines where this unit ends up.
            const f32 r = hash_to_unit_float(random_seed, static_cast<u64>(original_index));
            return static_cast<u32>(std::floor(r * static_cast<f32>(total_units)));
        }
    }

    return original_index;
}

// ═══════════════════════════════════════════════════════════════════════════
// Deterministic pseudo-random float from (seed, unit_index)
// ═══════════════════════════════════════════════════════════════════════════

// Local hash_combine — avoids dependency on render_graph_hashing.hpp.
[[nodiscard]] inline u64 hash_combine_local(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

f32 hash_to_unit_float(u64 seed, u64 unit_index) {
    // Combine seed and unit_index, then mix to produce a high-quality u64.
    // Two-round mixing via splitmix64 is fast and well-distributed.
    u64 x = hash_combine_local(seed, unit_index);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    constexpr f64 kInvU64Max = 1.0 / static_cast<f64>(UINT64_MAX);
    return static_cast<f32>(static_cast<f64>(x) * kInvU64Max);
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

SelectorWeight evaluate_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time
) {
    // ── Space exclusion ────────────────────────────────────────────────
    if (spec.exclude_spaces) {
        // Check if this unit maps to a whitespace-only character.
        // For glyph mode: examine the glyph's cluster byte offset in source.
        // For word/line mode: the unit_index identifies the word/line;
        // if the first byte of that word/line is a space, it's excluded.
        if (glyph_index < unit_map.glyph_to_grapheme.size()) {
            const u32 unit_idx = unit_map.unit_index(glyph_index, spec.unit);
            // Approximate check: use the grapheme index to find the byte offset
            // in the source and check if it's whitespace.
            // For a precise check we would need access to the PlacedGlyphRun's
            // byte_offset field, but we only have source text here.
            // As a heuristic, if the grapheme index maps to a space-only
            // cluster, we can detect this via the source text.
            // For now, a simple implementation: check if ANY glyph in the
            // grapheme cluster has a space at its byte_offset in source.
            (void)unit_idx; // see note above — full implementation deferred
            // Deferred: full space exclusion needs per-cluster source inspection.
            // The glyph_selector is primarily consumed by a text run evaluator
            // that will have access to the full PlacedGlyphRun.
        }
    }

    // ── Determine unit index ───────────────────────────────────────────
    const u32 raw_index = unit_map.unit_index(glyph_index, spec.unit);
    const u32 total = unit_map.unit_count(spec.unit);

    if (total == 0) return 0.0f;

    // ── Apply order ────────────────────────────────────────────────────
    u32 ordered_index;
    if (spec.randomize_order) {
        ordered_index = detail::apply_selector_order(
            TextSelectorOrder::Random, raw_index, total, spec.random_seed
        );
    } else {
        ordered_index = detail::apply_selector_order(
            spec.order, raw_index, total, 0
        );
    }

    // ── Normalise unit position to [0, 1] ─────────────────────────────
    // Use centre of unit: (index + 0.5) / total
    const f32 unit_position = (static_cast<f32>(ordered_index) + 0.5f)
                            / static_cast<f32>(total);

    // ── Evaluate animated parameters ──────────────────────────────────
    const f32 animated_start  = spec.start.evaluate(time)  / 100.0f;
    const f32 animated_end    = spec.end.evaluate(time)    / 100.0f;
    const f32 animated_offset = spec.offset.evaluate(time) / 100.0f;
    const f32 animated_amount = spec.amount.evaluate(time) / 100.0f;

    // ── Apply offset with wrap-around ─────────────────────────────────
    f32 shifted_position = unit_position - animated_offset;
    // Wrap to [0, 1] — x - floor(x) always yields [0, 1) in C++
    shifted_position = shifted_position - std::floor(shifted_position);

    // ── Evaluate shape ────────────────────────────────────────────────
    f32 weight = detail::evaluate_selector_shape(
        spec.shape,
        shifted_position,
        animated_start,
        animated_end
    );

    // ── Apply ease_low / ease_high ────────────────────────────────────
    // ease_low: smooth transition at the start of the active range
    // ease_high: smooth transition at the end of the active range
    if (weight > 0.0f && weight < 1.0f) {
        const f32 ease_low_norm  = std::clamp(spec.ease_low  / 100.0f, 0.0f, 1.0f);
        const f32 ease_high_norm = std::clamp(spec.ease_high / 100.0f, 0.0f, 1.0f);

        if (ease_low_norm > 0.0f || ease_high_norm < 1.0f) {
            // Use absolute range to handle reversed start/end
            const f32 abs_range = std::abs(animated_end - animated_start);
            if (abs_range > 1e-7f) {
                const f32 t_raw = (shifted_position - animated_start) / abs_range;
                const f32 t_clamped = std::clamp(t_raw, 0.0f, 1.0f);

                f32 ease_factor = 1.0f;
                if (t_clamped < ease_low_norm) {
                    // In ease-low region: smooth from 0 to ease_low_norm
                    const f32 et = (ease_low_norm > 1e-7f)
                        ? std::clamp(t_clamped / ease_low_norm, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = et * et * (3.0f - 2.0f * et); // smoothstep
                } else if (t_clamped > ease_high_norm) {
                    // In ease-high region
                    const f32 one_minus_ease_high = 1.0f - ease_high_norm;
                    const f32 et = (one_minus_ease_high > 1e-7f)
                        ? std::clamp((t_clamped - ease_high_norm) / one_minus_ease_high, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = 1.0f - et * et * (3.0f - 2.0f * et); // reverse smoothstep
                }
                weight *= ease_factor;
            }
        }
    }

    // ── Apply amount multiplier ───────────────────────────────────────
    weight *= std::clamp(animated_amount, 0.0f, 1.0f);

    return std::clamp(weight, 0.0f, 1.0f);
}

SelectorWeight evaluate_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time
) {
    SelectorWeight combined = 0.0f;
    bool first = true;

    for (const auto& spec : specs) {
        const SelectorWeight w = evaluate_selector(spec, unit_map, glyph_index, source, time);

        if (first) {
            combined = w;
            first = false;
            continue;
        }

        switch (spec.combine) {
            case SelectorCombineMode::Replace:
                combined = w;
                break;
            case SelectorCombineMode::Add:
                combined = std::min(combined + w, 1.0f);
                break;
            case SelectorCombineMode::Subtract:
                combined = std::max(combined - w, 0.0f);
                break;
            case SelectorCombineMode::Intersect:
            case SelectorCombineMode::Min:
                combined = std::min(combined, w);
                break;
            case SelectorCombineMode::Max:
                combined = std::max(combined, w);
                break;
        }
    }

    return std::clamp(combined, 0.0f, 1.0f);
}

} // namespace chronon3d
