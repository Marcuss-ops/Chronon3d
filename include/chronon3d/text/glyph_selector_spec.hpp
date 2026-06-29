#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// TEXT-SEL-01 — Canonical GlyphSelectorSpec via std::variant
// ═══════════════════════════════════════════════════════════════════════════
//
// The legacy `GlyphSelectorSpec` (in `glyph_selector.hpp`) is functionally a
// `RangeSelector` in disguise: start / end / offset / amount / shape /
// order / random.  TEXT-SEL-01 promotes it to one of three canonical
// selector kinds:
//
//   - RangeSelector      (subsumes the legacy spec; lowest-risk migration)
//   - WigglySelector     (time/position-driven jitter via AnimatedValue<>)
//   - ExpressionSelector (safe property access via SafeAccessMap)
//
// All three are stored under a single `std::variant<>` so the evaluator
// can switch on type at runtime.  New code uses `evaluate_selectors_v2`;
// the legacy `evaluate_selectors` is preserved verbatim for back-compat.
//
// The thin subset chosen for `ExpressionSelector` (textIndex / textTotal /
// frame / seed / safe-access) deliberately avoids pulling in
// `experimental/expressions/` so the text/ subsystem stays self-contained
// per `ANTI_DUPLICATION_RULES.md`.

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/glyph_selector.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace chronon3d {

// ── TextUnitRef — abstract reference to a text unit ──────────────────────
//
// OOB-safe: when `index == TextUnitMap::kInvalid` the ref resolves to
// "no unit" (e.g. excluded-glyph sentinel).  `byte_anchor` is optional,
// used by ExpressionSelector to query TEXT-UNM-01's 8-level ladder via
// `TextUnitMap::identity_at_byte(anchor)`.

struct TextUnitRef {
    TextSelectorUnit    unit{TextSelectorUnit::Glyph};
    u32                 index{TextUnitMap::kInvalid};
    std::optional<u32>  byte_anchor{};

    constexpr TextUnitRef() = default;
    constexpr TextUnitRef(TextSelectorUnit u, u32 i) noexcept : unit(u), index(i) {}
    constexpr TextUnitRef(TextSelectorUnit u, u32 i, std::optional<u32> b) noexcept
        : unit(u), index(i), byte_anchor(b) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return index != TextUnitMap::kInvalid;
    }
};

// ── SafeAccessMap — deterministic property-access store ──────────────────
//
// Per-frame `evaluate(name, time)` returns the bound value or std::nullopt
// on missing key (does NOT throw — appropriate for animation property
// lookups where missing entries are common during reflow).

class SafeAccessMap {
public:
    void set(std::string key, f32 value) {
        entries_[std::move(key)] = value;
    }
    void set(std::string key, AnimatedValue<f32> value) {
        animated_[std::move(key)] = std::move(value);
    }

    // Static (non-animated) value lookup.
    [[nodiscard]] std::optional<f32> get(std::string_view key) const noexcept;

    // Animated value lookup at the given sample time.
    [[nodiscard]] std::optional<f32> evaluate(std::string_view key, SampleTime time) const noexcept;

    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return entries_.find(std::string(key)) != entries_.end()
            || animated_.find(std::string(key)) != animated_.end();
    }

    void clear() noexcept { entries_.clear(); animated_.clear(); }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size() + animated_.size();
    }

private:
    std::unordered_map<std::string, f32> entries_;
    std::unordered_map<std::string, AnimatedValue<f32>> animated_;
};

// ── RangeSelector — start / end / offset / amount with shape & order ──────
//
// Subsumes the legacy `GlyphSelectorSpec`: the existing struct's fields are
// the RangeSelector's fields verbatim.  Dispatch on this variant composes
// on the legacy `evaluate_selector` after synthesising a temporary
// `GlyphSelectorSpec` (so the well-tested Range math is reused).

struct RangeSelector {
    TextSelectorUnit    unit{TextSelectorUnit::Glyph};
    TextSelectorShape   shape{TextSelectorShape::Smooth};
    TextSelectorOrder   order{TextSelectorOrder::Forward};
    SelectorCombineMode combine{SelectorCombineMode::Replace};

    AnimatedValue<f32>  start{0.0f};        // 0–100 normalised
    AnimatedValue<f32>  end{100.0f};        // 0–100 normalised
    AnimatedValue<f32>  offset{0.0f};       // 0–100 normalised, wraps
    AnimatedValue<f32>  amount{100.0f};     // 0–100 %, multiplies final weight

    f32  ease_low{0.0f};                    // 0–100 % of range for ease-in
    f32  ease_high{100.0f};                 // 0–100 % of range for ease-out
    bool exclude_spaces{true};
    bool randomize_order{false};
    u64  random_seed{0};
};

// ── WigglySelector — time / position-driven deterministic jitter ──────────
//
// semantics:
//   wps              — waves per second
//   correlation      — 0..1 mixing factor between two phase-offset channels
//   temporal_phase   — phase offset along time axis (AnimatedValue available)
//   spatial_phase    — phase offset along position axis (per-glyph)
//   min_amount/max_amount — weight-floor / weight-ceiling bounds
//   seed             — deterministic randomness seed (per-glyph+frame hash)
//
// Implementation uses sin(2π × wps × (t + temporal_phase) + spatial_phase ×
// glyph_norm) and a per-glyph+frame hash for noise, in line with the
// existing `include/chronon3d/animation/effects/wiggle.hpp` family without
// creating a hard coupling.  Composite weight is shaped via the existing
// `evaluate_selector_shape` (with degenerate start/end so the shape acts as
// a remap curve).

struct WigglySelector {
    TextSelectorUnit    unit{TextSelectorUnit::Glyph};
    TextSelectorShape   shape{TextSelectorShape::Smooth};
    SelectorCombineMode combine{SelectorCombineMode::Replace};

    AnimatedValue<f32>  min_amount{-1.0f};
    AnimatedValue<f32>  max_amount{1.0f};
    f32                 wps{2.0f};
    f32                 correlation{0.0f};
    AnimatedValue<f32>  temporal_phase{0.0f};
    AnimatedValue<f32>  spatial_phase{0.0f};
    bool                lock_dimensions{true};
    u64                 seed{0};
};

// ── ExpressionSelector — safe property access expression ─────────────────
//
// Resolves the named property (`value` field) through `SafeAccessMap` and
// shapes the result.  Missing keys yield std::nullopt → selection weight
// 0.0 (NOT a throw).  Compositional variables (`textIndex`, `textTotal`,
// `frame`) are bound by the evaluator before `evaluate_selectors_v2` runs.

struct ExpressionSelector {
    TextSelectorUnit    unit{TextSelectorUnit::Glyph};
    TextSelectorShape   shape{TextSelectorShape::Smooth};
    SelectorCombineMode combine{SelectorCombineMode::Replace};

    SafeAccessMap       props{};
    std::string         value{};            // property name to read at evaluate time
    u64                 seed{0};
};

// ── GlyphSelectorVariant — std::variant of the 3 canonical types ──────────

using GlyphSelectorVariant = std::variant<RangeSelector, WigglySelector, ExpressionSelector>;

// ── GlyphSelectorSpec — declarative selector with targets ─────────────────
//
// `targets` is the set of units the selector applies to.  When empty,
// applies to all units of `kind`'s discriminator.  Each TextUnitRef is
// matched against the per-glyph lookup via TextUnitMap.

struct GlyphSelectorSpec {
    std::string                  id{};       // unique id for diagnostics
    GlyphSelectorVariant         kind;
    std::vector<TextUnitRef>     targets;
};

// ── GlyphSelectorContext — evaluation scope ───────────────────────────────

struct GlyphSelectorContext {
    const TextUnitMap&  unit_map;
    SampleTime          time{};
    std::string_view    source;
    const PlacedGlyphRun* placed{nullptr};
    u32                 text_total{0};
};

// ── v2 evaluation entry points ────────────────────────────────────────────

[[nodiscard]] SelectorWeight evaluate_selector_v2(
    const GlyphSelectorSpec& spec,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) noexcept;

[[nodiscard]] SelectorWeight evaluate_selectors_v2(
    const std::vector<GlyphSelectorSpec>& specs,
    u32 glyph_index,
    const GlyphSelectorContext& ctx
) noexcept;

} // namespace chronon3d
