// ============================================================================
// glyph_selector_compile.cpp — Compile-time selector precompute + evaluation
// ============================================================================
//
// FASE 23 split: owns the CompiledSelector precomputation path and the
// evaluator that consumes it:
//
//   detail::should_exclude_unit(spec, unit_map, glyph_index, source, placed)
//       Whitespace-aware unit exclusion.  Used by compile_selector to
//       pre-bake the per-unit whitespace bit for fast evaluator rejection.
//
//   compile_selector(spec, unit_map, source, placed)
//       Pre-bake a single spec into a CompiledSelector: total_units,
//       random_permutation (if spec.randomize_order == true, via
//       get_or_build_permutation from glyph_selector_random.hpp),
//       per-unit `unit_is_whitespace` flag array (if spec.exclude_spaces
//       AND placed != nullptr).
//
//   compile_selectors(specs, ...)
//       Loop compile_selector over std::vector<GlyphSelectorSpec>.
//
//   evaluate_compiled_selector(compiled, unit_map, glyph_index, time)
//       Hot-path evaluator for one pre-baked CompiledSelector: reads
//       random_permutation when baked, falls back to
//       detail::apply_selector_order for non-Random orders.
//
//   evaluate_compiled_selectors(compiled, ...)
//       Loop-side evaluator: combines per-spec evaluator weights via
//       SelectorCombineMode (Replace / Add / Subtract / Intersect / Min / Max).
//
// Dependency: glyph_selector.hpp (full types) + glyph_selector_random.hpp
// (Random prebake permutation cache) + <algorithm>, <cmath>, <vector>.
//
// The slim glyph_selector.cpp keeps ONLY `build_text_unit_map` and the
// legacy public-API delegates (`evaluate_selector` + `evaluate_selectors`)
// which forward to compile_*/evaluate_compiled_* here.
// ============================================================================

#include <chronon3d/text/glyph_selector.hpp>
#include "glyph_selector_random.hpp"  // FASE 8 — Random permutation cache

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d {

namespace detail {

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

} // namespace detail (should_exclude_unit)

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

} // namespace chronon3d
