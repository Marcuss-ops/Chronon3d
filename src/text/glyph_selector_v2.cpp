// ═══════════════════════════════════════════════════════════════════════════
// TEXT-SEL-01 — evaluate_selector_v2 / evaluate_selectors_v2
// ═══════════════════════════════════════════════════════════════════════════
//
// Variant dispatch via std::visit.  RangeSelector composes on the legacy
// `evaluate_selector` by synthesising a temporary GlyphSelectorSpec; this
// reuses the well-tested Range math without duplicating it.  Wiggly and
// Expression selectors are implemented fresh with seed-driven determinism
// and SafeAccessMap safe property lookup (missing keys → 0.0, no throw).
//
// Back-compat: legacy `evaluate_selector`, `evaluate_selectors`,
// `evaluate_selector_shape`, `apply_selector_order`, `hash_to_unit_float`,
// `should_exclude_unit` are NOT touched.

#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace chronon3d {

namespace {

// Inverse of `2π` factor for `hash_to_unit_float` modulo normalisation.
constexpr f32 kTwoPi = 6.28318530717958647692f;

// ── SafeAccessMap accessors (out-of-line) ─────────────────────────────────

std::optional<f32> SafeAccessMap::get(std::string_view key) const noexcept {
    const std::string s_key(key);
    auto it = entries_.find(s_key);
    if (it != entries_.end()) return it->second;
    return std::nullopt;
}

std::optional<f32> SafeAccessMap::evaluate(std::string_view key, SampleTime time) const noexcept {
    const std::string s_key(key);
    auto ae = animated_.find(s_key);
    if (ae != animated_.end()) return ae->second.evaluate(time);
    auto se = entries_.find(s_key);
    if (se != entries_.end()) return se->second;
    return std::nullopt;
}

// ── targets filter ────────────────────────────────────────────────────────

[[nodiscard]] bool selector_targets_match(
    const GlyphSelectorSpec& spec,
    u32 glyph_index,
    const TextUnitMap& map
) noexcept {
    if (spec.targets.empty()) return true;  // no filter — applies to all glyphs
    for (const auto& t : spec.targets) {
        const u32 idx = map.unit_index(glyph_index, t.unit);
        if (idx == t.index && idx != TextUnitMap::kInvalid) return true;
    }
    return false;
}

// ── bind compositinal variables ───────────────────────────────────────────
//
// ExpressionSelectors resolve `textIndex`, `textTotal`, `frame` as
// builtin variable names.  We pre-bind them into a copy of `props` before
// evaluating the user's named value.

[[nodiscard]] SafeAccessMap bind_builtin_variables(
    const SafeAccessMap& base,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) {
    SafeAccessMap out(base);
    out.set("textIndex", static_cast<f32>(glyph_index));
    out.set("textTotal", static_cast<f32>(ctx.text_total));
    out.set("frame", static_cast<f32>(ctx.time.frame));
    return out;
}

// ── RangeSelector dispatch — composes on legacy ───────────────────────────

[[nodiscard]] SelectorWeight dispatch_range(
    const RangeSelector& r,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) noexcept {
    // Synthesise a temporary legacy GlyphSelectorSpec with the Range
    // fields.  This reuses the existing field-by-field evaluation logic
    // in `evaluate_selector` (well-tested by 19 tests in test_text_unit_map.cpp
    // + tests in test_selector_evaluate.cpp + test_selector_shapes.cpp).
    GlyphSelectorSpec legacy{};
    legacy.id = "range_v2_" + std::to_string(glyph_index);
    legacy.unit = r.unit;
    legacy.shape = r.shape;
    legacy.order = r.order;
    legacy.combine = r.combine;
    legacy.start = r.start;
    legacy.end = r.end;
    legacy.offset = r.offset;
    legacy.amount = r.amount;
    legacy.ease_low = r.ease_low;
    legacy.ease_high = r.ease_high;
    legacy.exclude_spaces = r.exclude_spaces;
    legacy.randomize_order = r.randomize_order;
    legacy.random_seed = r.random_seed;

    return evaluate_selector(
        legacy,
        ctx.unit_map,
        glyph_index,
        ctx.source,
        ctx.time,
        ctx.placed
    );
}

// ── WigglySelector dispatch — sin-jitter with seed-driven noise ───────────

[[nodiscard]] SelectorWeight dispatch_wiggly(
    const WigglySelector& w,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) noexcept {
    const f32 frame_f = static_cast<f32>(ctx.time.frame);
    const f32 x_norm = (ctx.unit_map.glyph_to_grapheme.size() > 0)
                            ? (static_cast<f32>(glyph_index) + 0.5f)
                              / static_cast<f32>(ctx.unit_map.glyph_to_grapheme.size())
                            : 0.0f;

    const f32 t_phase = w.temporal_phase.evaluate(ctx.time);
    const f32 s_phase = w.spatial_phase.evaluate(ctx.time);

    const f32 phase = kTwoPi * (w.wps * (frame_f + t_phase))
                    + s_phase * x_norm;

    // Seed-driven hash on (glyph_index, frame) → reproducible per-frame jitter.
    const u64 mix = static_cast<u64>(glyph_index)
                  ^ (static_cast<u64>(ctx.time.frame) << 16u);
    const f32 noise_seed = hash_to_unit_float(w.seed, mix);
    const f32 noise = (noise_seed - 0.5f) * 2.0f;

    // Two-channel sin with correlation mixing.
    const f32 phase_b = phase + w.correlation * kTwoPi * 0.5f;
    const f32 sin_a = static_cast<f32>(std::sin(phase + noise * 0.1f));
    const f32 sin_b = static_cast<f32>(std::sin(phase_b - noise * 0.1f));
    const f32 mixed = (sin_a + w.correlation * sin_b);

    // sin ∈ [-1, 1]; map to [0, 1] via (mixed + 1) * 0.5; then apply shape.
    const f32 norm = std::clamp(0.5f + 0.5f * mixed, 0.0f, 1.0f);
    const f32 shaped = evaluate_selector_shape(w.shape, norm, 0.0f, 1.0f);

    const f32 min_v = w.min_amount.evaluate(ctx.time);
    const f32 max_v = w.max_amount.evaluate(ctx.time);
    const f32 weight = std::clamp(min_v + (max_v - min_v) * shaped, 0.0f, 1.0f);
    return static_cast<SelectorWeight>(weight);
}

// ── ExpressionSelector dispatch — SafeAccessMap lookup ────────────────────

[[nodiscard]] SelectorWeight dispatch_expression(
    const ExpressionSelector& e,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) noexcept {
    SafeAccessMap bound = bind_builtin_variables(e.props, glyph_index, ctx);
    const std::optional<f32> resolved = bound.evaluate(e.value, ctx.time);

    // Safe fallback: missing property ⇒ 0.0 (no throw).
    if (!resolved.has_value()) {
        return SelectorWeight{0.0f};
    }

    const f32 norm = std::clamp(*resolved / 100.0f, 0.0f, 1.0f);
    const f32 shaped = evaluate_selector_shape(e.shape, norm, 0.0f, 1.0f);
    const f32 weight = std::clamp(shaped, 0.0f, 1.0f);
    return static_cast<SelectorWeight>(weight);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Public v2 entry points
// ═══════════════════════════════════════════════════════════════════════════

SelectorWeight evaluate_selector_v2(
    const GlyphSelectorSpec& spec,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) noexcept {
    if (!selector_targets_match(spec, glyph_index, ctx.unit_map)) {
        return SelectorWeight{0.0f};
    }
    return std::visit(
        [&](auto&& concrete) -> SelectorWeight {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, RangeSelector>) {
                return dispatch_range(concrete, glyph_index, ctx);
            } else if constexpr (std::is_same_v<T, WigglySelector>) {
                return dispatch_wiggly(concrete, glyph_index, ctx);
            } else if constexpr (std::is_same_v<T, ExpressionSelector>) {
                return dispatch_expression(concrete, glyph_index, ctx);
            } else {
                return SelectorWeight{0.0f};
            }
        },
        spec.kind
    );
}

SelectorWeight evaluate_selectors_v2(
    const std::vector<GlyphSelectorSpec>& specs,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) noexcept {
    SelectorWeight combined = 0.0f;
    bool first = true;
    for (const auto& spec : specs) {
        const SelectorWeight w = evaluate_selector_v2(spec, glyph_index, ctx);
        if (first) {
            combined = w;
            first = false;
            continue;
        }
        // combine mode lives on the active variant; we lift it to a uniform
        // dispatch via the first selector's combine (multi-selector stacks
        // all share the same combine mode in practice).
        SelectorCombineMode combine = SelectorCombineMode::Replace;
        std::visit([&](auto&& concrete) { combine = concrete.combine; }, spec.kind);

        switch (combine) {
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
