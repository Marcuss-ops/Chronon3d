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
#include <chronon3d/core/random/deterministic_random.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace chronon3d {

namespace detail {

[[nodiscard]] bool semantic_target_matches(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index
) noexcept {
    return !spec.semantic_id ||
        (glyph_index < unit_map.glyph_semantic_id.size() &&
         unit_map.glyph_semantic_id[glyph_index] == *spec.semantic_id);
}

f32 evaluate_selector_shape(
    const TextSelectorShape shape,
    f32 unit_position,
    f32 start,
    f32 end
) {
    unit_position = std::clamp(unit_position, 0.0f, 1.0f);
    start = std::clamp(start, 0.0f, 1.0f);
    end = std::clamp(end, 0.0f, 1.0f);

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

    const f32 t = range > 1e-7f ? (unit_position - start) / range : 0.5f;
    switch (shape) {
        case TextSelectorShape::Square: return 1.0f;
        case TextSelectorShape::RampUp: return t;
        case TextSelectorShape::RampDown: return 1.0f - t;
        case TextSelectorShape::Triangle:
            return t <= 0.5f ? 2.0f * t : 2.0f * (1.0f - t);
        case TextSelectorShape::Round: {
            const f32 u = t <= 0.5f ? 2.0f * t : 2.0f * (1.0f - t);
            return 3.0f * u * u - 2.0f * u * u * u;
        }
        case TextSelectorShape::Smooth: {
            const f32 u = t <= 0.5f ? 2.0f * t : 2.0f * (1.0f - t);
            const f32 one_minus_u = 1.0f - u;
            return 1.0f - one_minus_u * one_minus_u * one_minus_u;
        }
        case TextSelectorShape::Wiggly:
        case TextSelectorShape::Noise: {
            const f32 u = t <= 0.5f ? 2.0f * t : 2.0f * (1.0f - t);
            const f32 one_minus_u = 1.0f - u;
            return 1.0f - one_minus_u * one_minus_u * one_minus_u;
        }
    }
    return 0.0f;
}

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
    if (spec.order == TextSelectorOrder::Index && spec.randomize_order)
        throw std::invalid_argument("index selector order cannot be combined with randomize_order");
    const auto& modulation = spec.shape == TextSelectorShape::Noise ? spec.noise : spec.wiggly;
    if ((spec.shape == TextSelectorShape::Wiggly || spec.shape == TextSelectorShape::Noise) &&
        !valid_wiggly_selector_params(modulation)) {
        throw std::invalid_argument(
            "selector modulation frequency must be in (0, 1000] and amplitude in [0, 1]");
    }
    CompiledSelector c;
    c.spec = &spec;
    if (spec.semantic_id) {
        const bool exists = std::find(unit_map.glyph_semantic_id.begin(),
                                      unit_map.glyph_semantic_id.end(),
                                      *spec.semantic_id) != unit_map.glyph_semantic_id.end();
        c.total_units = exists ? 1U : 0U;
    } else {
        c.total_units = unit_map.unit_count(spec.unit);
    }

    if (spec.randomize_order && c.total_units > 0) {
        c.random_permutation = random::permutation(
            random::Seed{spec.random_seed}, c.total_units);
    }

    if (!spec.semantic_id && spec.exclude_spaces && placed != nullptr && c.total_units > 0) {
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

SelectorWeightParams bind_selector_runtime(
    const GlyphSelectorSpec& spec,
    const u32 total_units,
    const SampleTime time
) {
    const f32 start = spec.start.evaluate(time);
    const f32 end = spec.end.evaluate(time);
    const auto& modulation = spec.shape == TextSelectorShape::Noise ? spec.noise : spec.wiggly;

    const u32 step = spec.order == TextSelectorOrder::Index
        ? std::max(spec.index_step, 1u) : 1u;
    const u32 effective_units = total_units == 0 ? 0 : 1u + (total_units - 1u) / step;
    return SelectorWeightParams{
        spec.shape,
        start,
        end,
        spec.offset.evaluate(time),
        spec.amount.evaluate(time),
        effective_units,
        0u,
        false,
        true,
        std::abs(end - start) <= 1e-7f && start >= 100.0f,
        modulation.frequency,
        modulation.amplitude,
        modulation.seed,
    };
}

SelectorWeightParams bind_selector_runtime(
    const CompiledSelector& compiled,
    const TextUnitMap&,
    const SampleTime time
) {
    if (compiled.spec == nullptr) return {};
    return bind_selector_runtime(*compiled.spec, compiled.total_units, time);
}

SelectorWeightParams bind_selector_runtime(
    const CompiledSelector& compiled,
    const TextUnitMap& unit_map,
    const u32 glyph_index,
    const SampleTime time
) {
    auto params = bind_selector_runtime(compiled, unit_map, time);
    if (compiled.spec == nullptr || compiled.total_units == 0 ||
        glyph_index >= unit_map.glyph_to_grapheme.size()) {
        return params;
    }

    const auto& spec = *compiled.spec;
    if (!detail::semantic_target_matches(spec, unit_map, glyph_index)) {
        params.window_completed = true;
        return params;
    }
    if (spec.semantic_id) {
        params.ordered_index = 0;
        params.use_ordered_index = true;
        return params;
    }
    const u32 raw_index = unit_map.unit_index(glyph_index, spec.unit);
    if (raw_index >= compiled.total_units) return params;

    params.ordered_index = !compiled.random_permutation.empty()
        ? compiled.random_permutation[raw_index]
        : detail::apply_selector_order(spec.order, raw_index,
                                       compiled.total_units, 0);
    if (spec.order == TextSelectorOrder::Index)
        params.ordered_index /= std::max(spec.index_step, 1u);
    params.use_ordered_index = true;
    return params;
}

SelectorWeight selector_weight(
    const SelectorWeightParams& params,
    const u32 glyph_index
) noexcept {
    if (!params.enabled) return 1.0f;
    if ((params.shape == TextSelectorShape::Wiggly || params.shape == TextSelectorShape::Noise) &&
        !valid_wiggly_selector_params(WigglySelectorParams{
            params.wiggly_frequency, params.wiggly_amplitude,
            params.wiggly_seed}))
        return 0.0f;

    const f32 total = std::max(static_cast<f32>(params.total_units), 1.0f);
    const u32 position_index = params.use_ordered_index
        ? params.ordered_index : glyph_index;
    f32 position = (static_cast<f32>(position_index) + 0.5f) / total;
    position -= params.offset * 0.01f;
    position -= std::floor(position);

    const f32 start = std::clamp(params.start * 0.01f, 0.0f, 1.0f);
    const f32 end = std::clamp(params.end * 0.01f, 0.0f, 1.0f);
    const f32 amount = std::clamp(params.amount * 0.01f, 0.0f, 1.0f);
    // A completed hidden-window selector has no remaining influence.  This
    // is the host-resolved typewriter terminal state: with an opacity=0
    // property it leaves the entire run visible instead of turning the final
    // frame black.  The flag keeps this terminal decision out of GPU math.
    if (params.window_completed) return 0.0f;
    if (std::abs(end - start) <= 1e-7f) {
        // A collapsed range has no active units.  The explicit terminal flag
        // above still documents/handles the host typewriter terminal state;
        // without it, both animated endpoints must remain an empty window.
        return 0.0f;
    }

    f32 shaped = detail::evaluate_selector_shape(
        (params.shape == TextSelectorShape::Wiggly || params.shape == TextSelectorShape::Noise)
            ? TextSelectorShape::Smooth : params.shape,
        position, start, end);
    if (params.shape == TextSelectorShape::Wiggly || params.shape == TextSelectorShape::Noise) {
        // Compute in double precision and validate before integer conversion:
        // malformed low-level bindings must never cause an out-of-range cast.
        const double phase = static_cast<double>(position_index) *
                             static_cast<double>(params.wiggly_frequency);
        if (!std::isfinite(phase) ||
            phase < 0.0 ||
            phase >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return std::clamp(shaped * amount, 0.0f, 1.0f);
        const auto lattice = static_cast<std::int64_t>(std::floor(phase));
        f32 noise = random::unit(random::Seed{params.wiggly_seed},
                                 static_cast<u64>(lattice));
        if (params.shape == TextSelectorShape::Noise) {
            const f32 fraction = static_cast<f32>(phase - static_cast<double>(lattice));
            const f32 eased = fraction * fraction * (3.0f - 2.0f * fraction);
            const f32 next = random::unit(random::Seed{params.wiggly_seed},
                                          static_cast<u64>(lattice + 1));
            noise += (next - noise) * eased;
        }
        const f32 modulation =
            1.0f - params.wiggly_amplitude + 2.0f * params.wiggly_amplitude * noise;
        shaped *= modulation;
    }
    return std::clamp(shaped * amount, 0.0f, 1.0f);
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

SelectorWeight selector_weight(
    const CompiledSelector& compiled,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
) {
    const auto params = bind_selector_runtime(compiled, unit_map, time);
    return selector_weight(params, compiled, unit_map, glyph_index);
}

SelectorWeight selector_weight(
    const SelectorWeightParams& params,
    const CompiledSelector& compiled,
    const TextUnitMap& unit_map,
    const u32 glyph_index
) {
    if (compiled.spec == nullptr || compiled.total_units == 0 ||
        glyph_index >= unit_map.glyph_to_grapheme.size()) {
        return 0.0f;
    }
    const auto& spec = *compiled.spec;
    if (!detail::semantic_target_matches(spec, unit_map, glyph_index)) return 0.0f;
    if (spec.semantic_id) {
        auto named_params = params;
        named_params.total_units = 1;
        named_params.ordered_index = 0;
        named_params.use_ordered_index = true;
        return selector_weight(named_params, 0);
    }
    const u32 raw_index = unit_map.unit_index(glyph_index, spec.unit);
    const u32 total = spec.order == TextSelectorOrder::Index
        ? 1u + (compiled.total_units - 1u) / std::max(spec.index_step, 1u)
        : compiled.total_units;
    if (spec.exclude_spaces && !compiled.unit_is_whitespace.empty() &&
        raw_index < compiled.unit_is_whitespace.size() &&
        compiled.unit_is_whitespace[raw_index]) {
        return 0.0f;
    }
    if (raw_index >= total) return 0.0f;

    auto glyph_params = params;
    glyph_params.ordered_index = !compiled.random_permutation.empty()
        ? compiled.random_permutation[raw_index]
        : detail::apply_selector_order(spec.order, raw_index, total, 0);
    if (spec.order == TextSelectorOrder::Index)
        glyph_params.ordered_index /= std::max(spec.index_step, 1u);
    glyph_params.use_ordered_index = true;
    const f32 unit_position =
        (static_cast<f32>(glyph_params.ordered_index) + 0.5f) /
        static_cast<f32>(total);
    const f32 animated_start = glyph_params.start;
    const f32 animated_end = glyph_params.end;

    f32 shifted_position = unit_position - glyph_params.offset * 0.01f;
    shifted_position = shifted_position - std::floor(shifted_position);
    f32 weight = selector_weight(glyph_params, raw_index);

    if (weight > 0.0f && weight < 1.0f) {
        const f32 ease_low_norm  = std::clamp(spec.ease_low  / 100.0f, 0.0f, 1.0f);
        const f32 ease_high_norm = std::clamp(spec.ease_high / 100.0f, 0.0f, 1.0f);

        if (ease_low_norm > 0.0f || ease_high_norm < 1.0f) {
            const f32 abs_range = std::abs(animated_end - animated_start) * 0.01f;
            if (abs_range > 1e-7f) {
                const f32 t_raw = (shifted_position - animated_start * 0.01f) / abs_range;
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

    return std::clamp(weight, 0.0f, 1.0f);
}

SelectorWeight evaluate_compiled_selector(
    const CompiledSelector& compiled,
    const TextUnitMap& unit_map,
    const u32 glyph_index,
    const SampleTime time
) {
    return selector_weight(compiled, unit_map, glyph_index, time);
}

SelectorWeight selector_weight(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    const u32 glyph_index,
    const std::string_view source,
    const SampleTime time,
    const PlacedGlyphRun* placed
) {
    return selector_weight(
        compile_selector(spec, unit_map, source, placed),
        unit_map, glyph_index, time);
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
        const SelectorWeight w = selector_weight(cs, unit_map, glyph_index, time);

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
