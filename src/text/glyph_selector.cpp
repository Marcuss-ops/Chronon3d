#include <chronon3d/text/glyph_selector.hpp>
#include "glyph_selector_random.hpp"  // FASE 8 — permutation/random extracted

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

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

// ═══════════════════════════════════════════════════════════════════════════
// Shape + Order evaluation (in detail namespace)
// ═══════════════════════════════════════════════════════════════════════════

namespace detail {

f32 evaluate_selector_shape(
    TextSelectorShape shape,
    f32 unit_position,
    f32 start,
    f32 end
) {
    unit_position = std::clamp(unit_position, 0.0f, 1.0f);
    start = std::clamp(start, 0.0f, 1.0f);
    end   = std::clamp(end, 0.0f, 1.0f);

    if (start > end) {
        std::swap(start, end);
        unit_position = 1.0f - unit_position;
    }

    const f32 range = end - start;

    if (unit_position <= start) {
        if (shape == TextSelectorShape::RampDown) return 1.0f;
        return 0.0f;
    }
    if (unit_position >= end) {
        if (shape == TextSelectorShape::RampUp) return 1.0f;
        return 0.0f;
    }

    const f32 t = (range > 1e-7f) ? ((unit_position - start) / range) : 0.5f;

    switch (shape) {
        case TextSelectorShape::Square:
            return 1.0f;

        case TextSelectorShape::RampUp:
            return t;

        case TextSelectorShape::RampDown:
            return 1.0f - t;

        case TextSelectorShape::Triangle: {
            if (t <= 0.5f) return 2.0f * t;
            return 2.0f * (1.0f - t);
        }

        case TextSelectorShape::Round: {
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
                return 3.0f * u * u - 2.0f * u * u * u;
            }
            const f32 u = 2.0f * (1.0f - t);
            return 3.0f * u * u - 2.0f * u * u * u;
        }

        case TextSelectorShape::Smooth: {
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
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
            const u32 mid = total_units / 2;

            if (total_units % 2 == 1) {
                if (original_index == mid) return 0;
                if (original_index < mid) {
                    const u32 dist = mid - original_index;
                    return 2u * dist - 1u;
                }
                const u32 dist = original_index - mid;
                return 2u * dist;
            }

            if (original_index == mid - 1) return 0;
            if (original_index == mid)      return 1;

            if (original_index < mid - 1) {
                const u32 pair_dist = (mid - 1) - original_index;
                return 2u + 2u * (pair_dist - 1u);
            }

            const u32 pair_dist = original_index - mid;
            return 3u + 2u * (pair_dist - 1u);
        }

        case TextSelectorOrder::ToCenter: {
            const u32 from_centre = apply_selector_order(
                TextSelectorOrder::FromCenter, original_index, total_units, random_seed
            );
            return total_units - 1 - from_centre;
        }

        case TextSelectorOrder::Random: {
            const auto& perm = get_or_build_permutation(random_seed, total_units);
            return perm[original_index];
        }
    }

    return original_index;
}

// ─── Whitespace-aware unit exclusion ─────────────────────────────────

bool should_exclude_unit(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    const PlacedGlyphRun* placed
) {
    if (!spec.exclude_spaces) return false;
    if (placed == nullptr) return false;
    if (placed->glyphs.empty()) return false;

    switch (spec.unit) {
        case TextSelectorUnit::Glyph:
        case TextSelectorUnit::Grapheme:
        case TextSelectorUnit::Character: {
            if (glyph_index >= placed->glyphs.size()) return false;
            const auto& g = placed->glyphs[glyph_index];
            return is_whitespace_run(source, g.byte_offset, g.byte_len);
        }
        case TextSelectorUnit::Word: {
            if (glyph_index >= unit_map.glyph_to_word.size()) return false;
            const u32 word_idx = unit_map.glyph_to_word[glyph_index];
            if (word_idx >= unit_map.first_glyph_for_word.size()) return false;
            const u32 gi = unit_map.first_glyph_for_word[word_idx];
            if (gi == UINT32_MAX || gi >= placed->glyphs.size()) return false;
            const auto& g = placed->glyphs[gi];
            return is_whitespace_run(source, g.byte_offset, g.byte_len);
        }
        case TextSelectorUnit::Line: {
            if (glyph_index >= unit_map.glyph_to_line.size()) return false;
            const u32 line_idx = unit_map.glyph_to_line[glyph_index];
            if (line_idx >= unit_map.first_glyph_for_line.size()) return false;
            const u32 gi = unit_map.first_glyph_for_line[line_idx];
            if (gi == UINT32_MAX || gi >= placed->glyphs.size()) return false;
            const auto& g = placed->glyphs[gi];
            return is_whitespace_run(source, g.byte_offset, g.byte_len);
        }
    }
    return false;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════
// CompiledSelector — compile-time precomputation
// ═══════════════════════════════════════════════════════════════════════════

CompiledSelector compile_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    std::string_view source,
    const PlacedGlyphRun* placed
) {
    CompiledSelector c;
    c.spec = &spec;
    c.total_units = unit_map.unit_count(spec.unit);

    if (spec.randomize_order && c.total_units > 0) {
        const auto& perm = detail::get_or_build_permutation(spec.random_seed, c.total_units);
        c.random_permutation = perm;
    }

    if (spec.exclude_spaces && placed != nullptr && c.total_units > 0) {
        c.unit_is_whitespace.resize(c.total_units, false);

        if (spec.unit == TextSelectorUnit::Glyph ||
            spec.unit == TextSelectorUnit::Grapheme ||
            spec.unit == TextSelectorUnit::Character) {
            for (u32 gi = 0; gi < c.total_units && gi < placed->glyphs.size(); ++gi) {
                const auto& g = placed->glyphs[gi];
                c.unit_is_whitespace[gi] = detail::is_whitespace_run(
                    source, g.byte_offset, g.byte_len);
            }
        } else if (spec.unit == TextSelectorUnit::Word) {
            for (u32 wi = 0; wi < c.total_units; ++wi) {
                if (wi < unit_map.first_glyph_for_word.size()) {
                    const u32 gi = unit_map.first_glyph_for_word[wi];
                    if (gi != UINT32_MAX && gi < placed->glyphs.size()) {
                        const auto& g = placed->glyphs[gi];
                        c.unit_is_whitespace[wi] = detail::is_whitespace_run(
                            source, g.byte_offset, g.byte_len);
                    }
                }
            }
        } else if (spec.unit == TextSelectorUnit::Line) {
            for (u32 li = 0; li < c.total_units; ++li) {
                if (li < unit_map.first_glyph_for_line.size()) {
                    const u32 gi = unit_map.first_glyph_for_line[li];
                    if (gi != UINT32_MAX && gi < placed->glyphs.size()) {
                        const auto& g = placed->glyphs[gi];
                        c.unit_is_whitespace[li] = detail::is_whitespace_run(
                            source, g.byte_offset, g.byte_len);
                    }
                }
            }
        }
    }

    return c;
}

std::vector<CompiledSelector> compile_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    std::string_view source,
    const PlacedGlyphRun* placed
) {
    std::vector<CompiledSelector> compiled;
    compiled.reserve(specs.size());
    for (const auto& spec : specs) {
        compiled.push_back(compile_selector(spec, unit_map, source, placed));
    }
    return compiled;
}

SelectorWeight evaluate_compiled_selector(
    const CompiledSelector& compiled,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
) {
    const auto& spec = *compiled.spec;
    const u32 total = compiled.total_units;

    if (total == 0) return 0.0f;

    const u32 raw_index = unit_map.unit_index(glyph_index, spec.unit);

    if (spec.exclude_spaces && !compiled.unit_is_whitespace.empty()) {
        if (raw_index < compiled.unit_is_whitespace.size()
            && compiled.unit_is_whitespace[raw_index]) {
            return 0.0f;
        }
    }

    if (raw_index >= total) return 0.0f;

    u32 ordered_index;
    if (!compiled.random_permutation.empty()) {
        ordered_index = compiled.random_permutation[raw_index];
    } else {
        ordered_index = detail::apply_selector_order(
            spec.order, raw_index, total, 0);
    }

    const f32 unit_position = (static_cast<f32>(ordered_index) + 0.5f)
                            / static_cast<f32>(total);

    const f32 animated_start  = spec.start.evaluate(time)  / 100.0f;
    const f32 animated_end    = spec.end.evaluate(time)    / 100.0f;
    const f32 animated_offset = spec.offset.evaluate(time) / 100.0f;
    const f32 animated_amount = spec.amount.evaluate(time) / 100.0f;

    f32 shifted_position = unit_position - animated_offset;
    shifted_position = shifted_position - std::floor(shifted_position);

    f32 weight = detail::evaluate_selector_shape(
        spec.shape, shifted_position, animated_start, animated_end);

    if (weight > 0.0f && weight < 1.0f) {
        const f32 ease_low_norm  = std::clamp(spec.ease_low  / 100.0f, 0.0f, 1.0f);
        const f32 ease_high_norm = std::clamp(spec.ease_high / 100.0f, 0.0f, 1.0f);

        if (ease_low_norm > 0.0f || ease_high_norm < 1.0f) {
            const f32 abs_range = std::abs(animated_end - animated_start);
            if (abs_range > 1e-7f) {
                const f32 t_raw = (shifted_position - animated_start) / abs_range;
                const f32 t_clamped = std::clamp(t_raw, 0.0f, 1.0f);

                f32 ease_factor = 1.0f;
                if (t_clamped < ease_low_norm) {
                    const f32 et = (ease_low_norm > 1e-7f)
                        ? std::clamp(t_clamped / ease_low_norm, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = et * et * (3.0f - 2.0f * et);
                } else if (t_clamped > ease_high_norm) {
                    const f32 one_minus_ease_high = 1.0f - ease_high_norm;
                    const f32 et = (one_minus_ease_high > 1e-7f)
                        ? std::clamp((t_clamped - ease_high_norm) / one_minus_ease_high, 0.0f, 1.0f)
                        : 1.0f;
                    ease_factor = 1.0f - et * et * (3.0f - 2.0f * et);
                }
                weight *= ease_factor;
            }
        }
    }

    weight *= std::clamp(animated_amount, 0.0f, 1.0f);
    return std::clamp(weight, 0.0f, 1.0f);
}

SelectorWeight evaluate_compiled_selectors(
    const std::vector<CompiledSelector>& compiled,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
) {
    SelectorWeight combined = 0.0f;
    bool first = true;

    for (const auto& cs : compiled) {
        const SelectorWeight w = evaluate_compiled_selector(cs, unit_map, glyph_index, time);

        if (first) {
            combined = w;
            first = false;
            continue;
        }

        switch (cs.spec->combine) {
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
