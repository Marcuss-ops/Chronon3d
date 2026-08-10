// ═══════════════════════════════════════════════════════════════════════════
// Material — fluent authoring façade over `TextMaterial`.
//
// Wraps a `TextMaterial` value and exposes the most-commonly-tuned knobs
// (gradient / bevel / inner-shadow / highlight / emissive)
// as chainable setters. The public surface is typed; the underlying
// `TextMaterial` remains an internal value object.
//
// Construction goes through the `material::*` factory functions:
//
//   auto style = material::premium()
//                    .bevel(1.5f)
//                    .inner_shadow({0.0f, 2.0f}, 4.0f, Color::black());
//
// The builder is move-only. Consumers pull the spec out via the rvalue
// `release()` helper (see `friend` declarations below).
//
// ── C++20 ref-qualifier note ────────────────────────────────────────────
//
// Single `&` ref-qualifier per setter, identical user-facing syntax to
// `Animator` and `Selector`. See `selector.hpp` for the rationale.
//
// ── Auto-enable behaviour ───────────────────────────────────────────────
//
// Any mutator setter flips `enabled = true`.  This matches the user's
// design doc (`premium()` already enables, and any subsequent tweak keeps
// it on). Default-constructed `Material` is disabled; the user opts in
// by calling any setter, `enable()`, or any `material::*` factory.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/text_material.hpp>

#include <utility>

namespace chronon3d::authoring {

// Forward-declare the test-only release accessor.  The struct itself is
// defined in `tests/authoring/test_animator_dsl.cpp` so this is the only
// public surface needed by the builder header.
namespace testing {
struct MaterialTestAccess;
struct AnimatorTestAccess;
}

class Material {
public:
    Material() = default;
    explicit Material(TextMaterial value) : value_(std::move(value)) {}

    Material(const Material&)            = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&)                 = default;
    Material& operator=(Material&&)      = default;

    // ── Enable / disable ─────────────────────────────────────────────────
    Material& enable() & {
        value_.enabled = true;
        return *this;
    }
    Material&& enable() && {
        value_.enabled = true;
        return std::move(*this);
    }
    Material& disable() & {
        value_.enabled = false;
        return *this;
    }
    Material&& disable() && {
        value_.enabled = false;
        return std::move(*this);
    }

    // ── Gradient fill (top → bottom by default; rotate via angle) ────────
    Material& gradient(Color top, Color bottom, f32 angle_degrees = 90.0f) & {
        value_.top_color      = top;
        value_.bottom_color   = bottom;
        value_.gradient_angle = angle_degrees;
        value_.enabled        = true;
        return *this;
    }
    Material&& gradient(Color top, Color bottom, f32 angle_degrees = 90.0f) && {
        value_.top_color      = top;
        value_.bottom_color   = bottom;
        value_.gradient_angle = angle_degrees;
        value_.enabled        = true;
        return std::move(*this);
    }
    Material& top_color(Color c) & {
        value_.top_color = c;
        value_.enabled    = true;
        return *this;
    }
    Material&& top_color(Color c) && {
        value_.top_color = c;
        value_.enabled    = true;
        return std::move(*this);
    }
    Material& bottom_color(Color c) & {
        value_.bottom_color = c;
        value_.enabled       = true;
        return *this;
    }
    Material&& bottom_color(Color c) && {
        value_.bottom_color = c;
        value_.enabled       = true;
        return std::move(*this);
    }

    // ── Bevel (fake 3D edge thickness) ───────────────────────────────────
    Material& bevel(f32 px,
                     f32 highlight_opacity = 0.35f,
                     f32 shadow_opacity    = 0.25f) & {
        value_.bevel_px                = px;
        value_.bevel_highlight_opacity = highlight_opacity;
        value_.bevel_shadow_opacity    = shadow_opacity;
        value_.enabled                 = true;
        return *this;
    }
    Material&& bevel(f32 px,
                     f32 highlight_opacity = 0.35f,
                     f32 shadow_opacity    = 0.25f) && {
        value_.bevel_px                = px;
        value_.bevel_highlight_opacity = highlight_opacity;
        value_.bevel_shadow_opacity    = shadow_opacity;
        value_.enabled                 = true;
        return std::move(*this);
    }
    Material& bevel_highlight_color(Color c) & {
        value_.bevel_highlight_color = c;
        value_.enabled               = true;
        return *this;
    }
    Material&& bevel_highlight_color(Color c) && {
        value_.bevel_highlight_color = c;
        value_.enabled               = true;
        return std::move(*this);
    }

    // ── Inner shadow (AE-style cast inside glyph boundaries) ─────────────
    Material& inner_shadow(Vec2 offset, f32 blur, Color color) & {
        value_.inner_shadow_enabled = true;
        value_.inner_shadow_offset  = offset;
        value_.inner_shadow_blur    = blur;
        value_.inner_shadow_color   = color;
        value_.enabled              = true;
        return *this;
    }
    Material&& inner_shadow(Vec2 offset, f32 blur, Color color) && {
        value_.inner_shadow_enabled = true;
        value_.inner_shadow_offset  = offset;
        value_.inner_shadow_blur    = blur;
        value_.inner_shadow_color   = color;
        value_.enabled              = true;
        return std::move(*this);
    }
    Material& no_inner_shadow() & {
        value_.inner_shadow_enabled = false;
        return *this;
    }
    Material&& no_inner_shadow() && {
        value_.inner_shadow_enabled = false;
        return std::move(*this);
    }

    // ── Top highlight / bottom shade (AE-style bevel embellishment) ──────
    Material& top_highlight(f32 opacity, f32 fraction = 0.10f) & {
        value_.top_highlight_opacity   = opacity;
        value_.top_highlight_fraction  = fraction;
        value_.enabled                 = true;
        return *this;
    }
    Material&& top_highlight(f32 opacity, f32 fraction = 0.10f) && {
        value_.top_highlight_opacity   = opacity;
        value_.top_highlight_fraction  = fraction;
        value_.enabled                 = true;
        return std::move(*this);
    }
    Material& bottom_shade(f32 opacity, f32 fraction = 0.08f) & {
        value_.bottom_shade_opacity   = opacity;
        value_.bottom_shade_fraction  = fraction;
        value_.enabled                = true;
        return *this;
    }
    Material&& bottom_shade(f32 opacity, f32 fraction = 0.08f) && {
        value_.bottom_shade_opacity   = opacity;
        value_.bottom_shade_fraction  = fraction;
        value_.enabled                = true;
        return std::move(*this);
    }

    // ── Emissive / brightness boost ──────────────────────────────────────
    Material& emissive(f32 factor) & {
        value_.emissive = factor;
        return *this;
    }
    Material&& emissive(f32 factor) && {
        value_.emissive = factor;
        return std::move(*this);
    }

private:
    friend class Text;                                                    // PR 3
    friend struct chronon3d::authoring::testing::MaterialTestAccess;      // tests
    friend struct chronon3d::authoring::testing::AnimatorTestAccess;       // tests

    [[nodiscard]]
    TextMaterial release() && {
        return std::move(value_);
    }

    TextMaterial value_;
};

// ── Convenience factories (mirror the engine's static presets) ─────────
namespace material {

/// Premium look: gradient + bevel + highlights + glow + shadow.
inline Material premium() {
    return Material{TextMaterial::premium()};
}
/// Neon glow: bright gradient + strong glow.
inline Material neon() {
    return Material{TextMaterial::neon()};
}
/// Glass: semi-transparent + subtle edge highlight.
inline Material glass() {
    return Material{TextMaterial::glass()};
}
/// Flat: solid colour, no effects.  Useful as a base to layer onto.
inline Material flat() {
    return Material{TextMaterial::flat()};
}

} // namespace material

} // namespace chronon3d::authoring
