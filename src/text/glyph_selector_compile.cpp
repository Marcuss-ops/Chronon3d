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
#include <chronon3d/text/selector_expression.hpp>
#include <chronon3d/core/random/deterministic_random.hpp>
#include <chronon3d/math/expression.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d {

namespace {

[[nodiscard]] f32 smoothstep_unit(f32 value) noexcept {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

[[nodiscard]] f32 wiggly_lattice_sample(const WigglySelectorParams& params,
                                        i64 spatial_lattice,
                                        i64 temporal_lattice,
                                        u32 channel) noexcept {
    const u64 mixed = static_cast<u64>(spatial_lattice) ^
        (static_cast<u64>(temporal_lattice) * 0x9E3779B97F4A7C15ull) ^
        (static_cast<u64>(channel) * 0xBF58476D1CE4E5B9ull);
    return random::unit(random::Seed{params.seed}, mixed);
}

[[nodiscard]] f32 wiggly_noise_1d(const WigglySelectorParams& params,
                                  f32 spatial, f32 temporal,
                                  u32 channel) noexcept {
    const f32 spatial_position = spatial + params.spatial_phase / 360.0f;
    const f32 temporal_position = temporal + params.temporal_phase / 360.0f;

    if (params.mode == WigglySelectorMode::Jagged) {
        const auto sx = static_cast<i64>(std::llround(static_cast<double>(spatial_position)));
        const auto tx = static_cast<i64>(std::llround(static_cast<double>(temporal_position)));
        return wiggly_lattice_sample(params, sx, tx, channel);
    }

    const f32 spatial_floor = std::floor(spatial_position);
    const f32 temporal_floor = std::floor(temporal_position);
    f32 u = spatial_position - spatial_floor;
    f32 v = temporal_position - temporal_floor;
    if (params.mode == WigglySelectorMode::Smooth) {
        u = smoothstep_unit(u);
        v = smoothstep_unit(v);
    }

    const auto sx = static_cast<i64>(spatial_floor);
    const auto tx = static_cast<i64>(temporal_floor);
    const f32 a = wiggly_lattice_sample(params, sx, tx, channel);
    const f32 b = wiggly_lattice_sample(params, sx + 1, tx, channel);
    const f32 c = wiggly_lattice_sample(params, sx, tx + 1, channel);
    const f32 d = wiggly_lattice_sample(params, sx + 1, tx + 1, channel);
    const f32 ab = a + (b - a) * u;
    const f32 cd = c + (d - c) * u;
    return ab + (cd - ab) * v;
}

[[nodiscard]] f32 wiggly_noise(const WigglySelectorParams& params,
                               f32 spatial, f32 temporal) noexcept {
    if (params.lock_dimensions)
        return wiggly_noise_1d(params, spatial, temporal, 0u);
    const f32 x = wiggly_noise_1d(params, spatial, temporal, 0u);
    const f32 y = wiggly_noise_1d(params, spatial, temporal, 1u);
    const f32 z = wiggly_noise_1d(params, spatial, temporal, 2u);
    return (x + y + z) / 3.0f;
}

[[nodiscard]] f32 evaluate_selector_expression(
    const std::string& expression,
    const SelectorWeightParams& params,
    u32 text_index,
    f32 base_weight) noexcept {
    try {
        std::unordered_map<std::string, double> variables;
        variables.emplace("textIndex", static_cast<double>(text_index));
        variables.emplace("textTotal", static_cast<double>(params.total_units));
        variables.emplace("selectorValue", static_cast<double>(base_weight) * 100.0);

        math::ExpressionContext context;
        context.frame = params.sample_frame;
        context.time = params.time_seconds;
        context.fps = params.sample_fps;

        const double value = math::evaluate_expression(
            expression, context, variables, 0.0,
            math::ExpressionProfile::Selector);
        if (!std::isfinite(value)) return 0.0f;
        return std::clamp(static_cast<f32>(value) * 0.01f, 0.0f, 1.0f);
    } catch (...) {
        return 0.0f;
    }
}

[[nodiscard]] f32 apply_selector_expression(const GlyphSelectorSpec& spec,
                                            const SelectorWeightParams& params,
                                            u32 text_index,
                                            f32 weight) noexcept {
    if (!spec.expression || spec.expression->empty()) return weight;
    return evaluate_selector_expression(*spec.expression, params, text_index, weight);
}

} // namespace

namespace detail {

[[nodiscard]] bool semantic_target_matches(
    const GlyphSelectorSpec& spec,
    const GlyphSelectorUnitMap& unit_map,
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
    const GlyphSelectorUnitMap& unit_map,
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
    const GlyphSelectorUnitMap& unit_map,
    std::string_view source,
    const PlacedGlyphRun* placed
) {
    if (spec.order == TextSelectorOrder::Index && spec.randomize_order)
        throw std::invalid_argument("index selector order cannot be combined with randomize_order");
    if (static_cast<u8>(spec.range_units) > static_cast<u8>(TextSelectorRangeUnits::Index))
        throw std::invalid_argument("selector range units must be percentage or index");
    if (!std::isfinite(spec.smoothness) || spec.smoothness < 0.0f ||
        spec.smoothness > 100.0f)
        throw std::invalid_argument("selector smoothness must be a percentage in [0, 100]");
    if (spec.expression) {
        if (spec.expression->empty() || spec.expression->size() > 4096)
            throw std::invalid_argument(
                "selector expression must contain 1..4096 bytes");
        if (!validate_selector_expression(*spec.expression))
            throw std::invalid_argument(
                "selector expression uses an unsupported AE host variable or function");
    }
    const auto& modulation = spec.shape == TextSelectorShape::Noise ? spec.noise : spec.wiggly;
    if ((spec.shape == TextSelectorShape::Wiggly || spec.shape == TextSelectorShape::Noise) &&
        !valid_wiggly_selector_params(modulation)) {
        throw std::invalid_argument(
            "selector modulation parameters are outside their supported ranges");
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
    f32 start = spec.start.evaluate(time);
    f32 end = spec.end.evaluate(time);
    f32 offset = spec.offset.evaluate(time);
    const auto& modulation = spec.shape == TextSelectorShape::Noise ? spec.noise : spec.wiggly;

    // Range Selector Units: Index values are logical unit indices.  Bind them
    // once per frame into the percentage domain the canonical weight function
    // and the GPU ABI already consume.
    if (spec.range_units == TextSelectorRangeUnits::Index) {
        const f32 total = static_cast<f32>(std::max(total_units, 1u));
        start = start / total * 100.0f;
        end = end / total * 100.0f;
        offset = offset / total * 100.0f;
    }

    const u32 step = spec.order == TextSelectorOrder::Index
        ? std::max(spec.index_step, 1u) : 1u;
    const u32 effective_units = total_units == 0 ? 0 : 1u + (total_units - 1u) / step;

    SelectorWeightParams params;
    params.shape = spec.shape;
    params.start = start;
    params.end = end;
    params.offset = offset;
    params.amount = spec.amount.evaluate(time);
    params.total_units = effective_units;
    params.ordered_index = 0u;
    params.use_ordered_index = false;
    params.enabled = true;
    params.window_completed = std::abs(end - start) <= 1e-7f && start >= 100.0f;
    params.wiggly_frequency = modulation.frequency;
    params.wiggly_amplitude = modulation.amplitude;
    params.wiggly_seed = modulation.seed;
    params.smoothness = spec.smoothness;
    params.time_seconds = static_cast<f32>(time.seconds());
    params.sample_frame = static_cast<f32>(time.frame);
    params.sample_fps = static_cast<f32>(time.fps());
    params.wiggles_per_second = modulation.wiggles_per_second;
    params.correlation = modulation.correlation;
    params.temporal_phase = modulation.temporal_phase;
    params.spatial_phase = modulation.spatial_phase;
    params.min_amount = modulation.min_amount;
    params.max_amount = modulation.max_amount;
    params.lock_dimensions = modulation.lock_dimensions;
    params.wiggly_mode = modulation.mode;
    return params;
}

SelectorWeightParams bind_selector_runtime(
    const CompiledSelector& compiled,
    const GlyphSelectorUnitMap&,
    const SampleTime time
) {
    if (compiled.spec == nullptr) return {};
    return bind_selector_runtime(*compiled.spec, compiled.total_units, time);
}

SelectorWeightParams bind_selector_runtime(
    const CompiledSelector& compiled,
    const GlyphSelectorUnitMap& unit_map,
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
    params.raw_index = raw_index;
    return params;
}

SelectorWeight selector_weight(
    const SelectorWeightParams& params,
    const u32 glyph_index
) noexcept {
    if (!params.enabled) return 1.0f;
    WigglySelectorParams modulation;
    modulation.frequency = params.wiggly_frequency;
    modulation.amplitude = params.wiggly_amplitude;
    modulation.seed = params.wiggly_seed;
    modulation.wiggles_per_second = params.wiggles_per_second;
    modulation.correlation = params.correlation;
    modulation.temporal_phase = params.temporal_phase;
    modulation.spatial_phase = params.spatial_phase;
    modulation.min_amount = params.min_amount;
    modulation.max_amount = params.max_amount;
    modulation.lock_dimensions = params.lock_dimensions;
    modulation.mode = params.wiggly_mode;
    if ((params.shape == TextSelectorShape::Wiggly || params.shape == TextSelectorShape::Noise) &&
        !valid_wiggly_selector_params(modulation))
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

    // AE Range Selector Smoothness feathers the active window with a
    // smoothstep mask.  At 0 the historical hard shape is preserved.
    if (params.smoothness > 0.0f) {
        const f32 window_start = std::min(start, end);
        const f32 window_end = std::max(start, end);
        const f32 range_width = window_end - window_start;
        if (range_width > 1e-7f) {
            const f32 feather = range_width *
                std::clamp(params.smoothness * 0.01f, 0.0f, 1.0f);
            if (feather > 1e-7f) {
                const f32 edge_in = smoothstep_unit(
                    (position - window_start) / feather);
                const f32 edge_out = smoothstep_unit(
                    (window_end - position) / feather);
                shaped *= std::min(edge_in, edge_out);
            }
        }
    }

    if (params.shape == TextSelectorShape::Wiggly) {
        // Correlation reduces the spatial frequency: 100% gives every unit the
        // same sample, 0% leaves neighbouring units independent.
        const f32 correlation = std::clamp(params.correlation, 0.0f, 100.0f);
        const f32 spatial_frequency = params.wiggly_frequency *
            (1.0f - correlation * 0.01f);
        const f32 spatial = static_cast<f32>(position_index) * spatial_frequency;
        const f32 temporal = params.time_seconds * params.wiggles_per_second;
        const f32 noise = wiggly_noise(modulation, spatial, temporal);
        const f32 modulation_factor = 1.0f - params.wiggly_amplitude +
            2.0f * params.wiggly_amplitude * noise;
        const f32 low = std::clamp(params.min_amount * 0.01f, 0.0f, 1.0f);
        const f32 high = std::clamp(params.max_amount * 0.01f, 0.0f, 1.0f);
        shaped *= low + (high - low) *
            std::clamp(modulation_factor, 0.0f, 1.0f);
    } else if (params.shape == TextSelectorShape::Noise) {
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
        const f32 fraction = static_cast<f32>(phase - static_cast<double>(lattice));
        const f32 eased = fraction * fraction * (3.0f - 2.0f * fraction);
        const f32 next = random::unit(random::Seed{params.wiggly_seed},
                                      static_cast<u64>(lattice + 1));
        noise += (next - noise) * eased;
        const f32 modulation_factor =
            1.0f - params.wiggly_amplitude + 2.0f * params.wiggly_amplitude * noise;
        shaped *= modulation_factor;
    }
    return std::clamp(shaped * amount, 0.0f, 1.0f);
}

std::vector<CompiledSelector> compile_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const GlyphSelectorUnitMap& unit_map,
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
    const GlyphSelectorUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
) {
    const auto params = bind_selector_runtime(compiled, unit_map, time);
    return selector_weight(params, compiled, unit_map, glyph_index);
}

SelectorWeight selector_weight(
    const SelectorWeightParams& params,
    const CompiledSelector& compiled,
    const GlyphSelectorUnitMap& unit_map,
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
        named_params.raw_index = glyph_index;
        const f32 weight = selector_weight(named_params, 0);
        return apply_selector_expression(spec, named_params, glyph_index, weight);
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
    if (spec.end_inclusive && total > 0 && glyph_params.start < glyph_params.end) {
        // Bodymovin selectors address character cells by their source range
        // boundaries. The renderer samples each glyph at its center, so grow
        // the interval by half a unit on both sides to retain the boundary
        // glyphs while preserving an empty collapsed range.
        const f32 half_unit = 50.0f / static_cast<f32>(total);
        glyph_params.start = std::max(0.0f, glyph_params.start - half_unit);
        glyph_params.end = std::min(100.0f,
            glyph_params.end + half_unit + 1e-4f);
    }
    glyph_params.ordered_index = !compiled.random_permutation.empty()
        ? compiled.random_permutation[raw_index]
        : detail::apply_selector_order(spec.order, raw_index, total, 0);
    if (spec.order == TextSelectorOrder::Index)
        glyph_params.ordered_index /= std::max(spec.index_step, 1u);
    glyph_params.use_ordered_index = true;
    glyph_params.raw_index = raw_index;
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

    weight = apply_selector_expression(spec, glyph_params, raw_index, weight);
    return std::clamp(weight, 0.0f, 1.0f);
}

SelectorWeight evaluate_compiled_selector(
    const CompiledSelector& compiled,
    const GlyphSelectorUnitMap& unit_map,
    const u32 glyph_index,
    const SampleTime time
) {
    return selector_weight(compiled, unit_map, glyph_index, time);
}

SelectorWeight selector_weight(
    const GlyphSelectorSpec& spec,
    const GlyphSelectorUnitMap& unit_map,
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
    const GlyphSelectorUnitMap& unit_map,
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
            case SelectorCombineMode::Difference:
                combined = std::abs(combined - w);
                break;
        }
    }

    return std::clamp(combined, 0.0f, 1.0f);
}

} // namespace chronon3d
