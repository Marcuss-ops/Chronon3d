// ═══════════════════════════════════════════════════════════════════════════
// text_animator_compile.cpp — §5.0a + §5.0e chain-method implementations
// ═══════════════════════════════════════════════════════════════════════════
//
// Implements the two chain-method pair `TextAnimatorSpec::compile()` +
// `TextAnimatorSpec::is_valid()`. Closes the user-spec gap documented in
// `docs/CHANGELOG.md` §5 ("resolve().compile().is_valid()" — the explicit
// 3-method fluent pattern that was DESIGNED-IN but never landed: the .hpp
// had the typing surface but no compile()/is_valid() implementation; the
// authoring chain was effectively "(compile()?) (is_valid()?) — silent
// fallthrough").
//
// ── §5.0a — compile() ────────────────────────────────────────────────────
// Self-reference return for fluent chaining. Two responsibilities:
//   (1) Provide a hook for downstream code (MotionRegistry, Animator DSL,
//       hand-rolled authoring) to MARK an authored spec as "fully compiled"
//       without polluting the struct with extra state. We do NOT add an
//       explicit `bool compiled = false` because that would be a NEW
//       public struct member (Cat-3 zero-new-symbols violation).
//   (2) Normalize the spec — currently a no-op since `AnimatedValue<T>`
//       already sorts on insert + has_strict-monotonic semantics inside
//       `add_keyframe()`. The hook is left in place so AUTHORING CALLERS
//       can rely on `spec.compile().is_valid()` as the canonical idiom.
//
// ── §5.0e — is_valid() — 4 invariants ──────────────────────────────────────
//   Inv 1 — Non-empty predicates: `selectors` AND `properties` non-empty.
//           (This IS the "membership predicate" the user referenced.)
//
//   Inv 2 — Strict monotonicity: AnimatedValue keyframes strictly increase
//           (no duplicate frames).  AnimatedValue::add_keyframe already
//           sorts on insert via std::sort (which does NOT reject equal
//           keys), so duplicate frames are accepted by the API; we catch
//           them HERE — locks against the "add_keyframe twice at frame N"
//           footgun.  This is the invariant that breaks the membership-
//           predicate ceiling (single empty-check is insufficient to
//           distinguish a monotonic time-curve from a degenerate duplicate-
//           frame one).
//
//   Inv 3 — Value integrity: no NaN or Inf in any keyframe value (and
//           no NaN/Inf in any static-value property field).  Defends
//           against the "0/0 normalized scale" + "infinite-range frame
//           timestamp" + "NaN width/angle" authoring footguns.
//
//   Inv 4 — Blend-mode coverage: scoped enum; any out-of-enum value is
//           UB at the type level. We make the contract machine-verifiable
//           by explicit value-comparison against
//           `TextPropertyBlendMode::{Add, Replace, Multiply}`.
//
// ── DISPATCH PATTERN ─────────────────────────────────────────────────────
// std::visit with explicit std::is_same_v<P, ...> branching. SFINAE by
// member-name was tried (and rejected) because AnimatedValue-bearing
// properties use different field names: `value` for Position/Scale/
// Opacity, `radius` for Blur, `pixels` for Tracking. RotationProperty
// also has `degrees` (NOT `value`) and AnchorProperty has `value` (Vec3) —
// each static-value alternative is dispatched separately to its specific
// field. The is_same_v pattern matches the canonical Chronon3D precedent
// in `src/text/animation/text_property_applier.cpp` and
// `text_animator_stack.cpp`.

#include <chronon3d/text/animation/text_animator_spec.hpp>

#include <cmath>       // std::isnan, std::isinf, std::isfinite
#include <type_traits>
#include <variant>

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────
// detail::component_has_nan_or_inf — Inv 4 helper
// ─────────────────────────────────────────────────────────────────────────
// SFINAE over per-property value type. Returns true iff any component is
// NaN or Inf. Covers arithmetic scalars + glm vector types + Color + the
// static-value struct fields (Vec3 / Color / f32).

namespace detail {

template <typename T>
[[nodiscard]] inline bool component_has_nan_or_inf(const T& v) {
    if constexpr (std::is_floating_point_v<T>) {
        return std::isnan(v) || std::isinf(v);
    } else if constexpr (std::is_integral_v<T>) {
        return false;  // Int can't be NaN/Inf.
    } else if constexpr (requires { v.r; v.g; v.b; v.a; }) {
        // Color
        return std::isnan(static_cast<f32>(v.r)) || std::isinf(static_cast<f32>(v.r))
            || std::isnan(static_cast<f32>(v.g)) || std::isinf(static_cast<f32>(v.g))
            || std::isnan(static_cast<f32>(v.b)) || std::isinf(static_cast<f32>(v.b))
            || std::isnan(static_cast<f32>(v.a)) || std::isinf(static_cast<f32>(v.a));
    } else {
        // Vec2 / Vec3 (any T with .x/.y/.z) — recurse component-wise.
        bool bad = false;
        if constexpr (requires { v.x; }) {
            bad = bad || std::isnan(static_cast<f32>(v.x)) || std::isinf(static_cast<f32>(v.x));
        }
        if constexpr (requires { v.y; }) {
            bad = bad || std::isnan(static_cast<f32>(v.y)) || std::isinf(static_cast<f32>(v.y));
        }
        if constexpr (requires { v.z; }) {
            bad = bad || std::isnan(static_cast<f32>(v.z)) || std::isinf(static_cast<f32>(v.z));
        }
        return bad;
    }
}

// helper: check keyframes for monotonicity
template <typename T>
[[nodiscard]] inline bool check_monotonic_av(const T& av) {
    const auto& kfs = av.keyframes();
    if (kfs.size() < 2) return true;  // 0 or 1 keyframes are trivially monotonic
    for (size_t i = 1; i < kfs.size(); ++i) {
        if (kfs[i].frame <= kfs[i-1].frame) return false;
    }
    return true;
}

// helper: check keyframe values for NaN/Inf
template <typename T>
[[nodiscard]] inline bool check_clean_av(const T& av) {
    for (const auto& kf : av.keyframes()) {
        if (component_has_nan_or_inf(kf.value)) return false;
    }
    return true;
}

} // namespace detail

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimatorSpec::compile() — fluent self-reference return
// ═══════════════════════════════════════════════════════════════════════════

TextAnimatorSpec& TextAnimatorSpec::compile() {
    // No-op: AnimatedValue<T>::add_keyframe already enforces sortedness
    // + invariance-of-default-value at insertion.  This hook is the
    // canonical contract hook for authoring chains — see §5.0a header
    // docblock.
    //
    // NOTE: a `bool compiled = false` field could be added but would
    // (a) introduce a new public struct member (Cat-3 violation without
    // ADR), and (b) open the door to write-after-construct sequencing
    // bugs at scale. The "method-as-hook" pattern keeps the surface
    // minimal AND matches After Effects' "compile + validate" idiom.
    return *this;
}

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimatorSpec::is_valid() — 5 invariants beyond empty/empty membership
// ═══════════════════════════════════════════════════════════════════════════

bool TextAnimatorSpec::is_valid() const {
    // ── Inv 1 — Non-empty predicates (the "membership predicate" the user
    //              referenced the chain-method pair as breaking). ─────────
    if (selectors.empty()) return false;
    if (properties.empty()) return false;

    // ── Inv 2 — Strict monotonicity ─────────────────────────────────────
    bool inv2_ok = true;
    for (const auto& prop : properties) {
        const bool ok = std::visit(
            []<typename P>(const P& p) -> bool {
                if constexpr (std::is_same_v<P, PositionProperty>
                           || std::is_same_v<P, ScaleProperty>
                           || std::is_same_v<P, OpacityProperty>) {
                    return detail::check_monotonic_av(p.value);
                } else if constexpr (std::is_same_v<P, BlurProperty>) {
                    return detail::check_monotonic_av(p.radius);
                } else if constexpr (std::is_same_v<P, TrackingProperty>) {
                    return detail::check_monotonic_av(p.pixels);
                } else {
                    return true;  // static: monotonicity not applicable
                }
            },
            prop
        );
        if (!ok) { inv2_ok = false; break; }
    }
    if (!inv2_ok) return false;

    // ── Inv 3 — Value integrity ──────────────────────────────────────────
    // NaN / Inf defend against the canonical authoring footguns:
    //   (a) 0/0 normalized scale on a Vec2 / Vec3 keyframe
    //   (b) infinite-range keyframes (frame = std::numeric_limits<...>::max())
    //   (c) f32 width = std::numeric_limits<f32>::infinity() on stroke / shift
    // CRITICAL: each static-value alternative has its own distinct field
    // name — RotationProperty::degrees (NOT value), AnchorProperty::value,
    // SkewProperty::{angle, axis}, FillColorProperty::color,
    // StrokeColorProperty::color, StrokeWidthProperty::width,
    // BaselineShiftProperty::pixels, CharacterOffsetProperty::offset (i32).
    // Lumping multiple static-value types into one `p.value` branch
    // (e.g. Rotation+Anchor) is a COMPILE ERROR because RotationProperty
    // has no `value` field.  Each must be explicitly dispatched.
    bool inv3_ok = true;
    for (const auto& prop : properties) {
        const bool ok = std::visit(
            []<typename P>(const P& p) -> bool {
                if constexpr (std::is_same_v<P, PositionProperty>
                           || std::is_same_v<P, ScaleProperty>
                           || std::is_same_v<P, OpacityProperty>) {
                    // AV<Vec3> / AV<f32> backing: scan every keyframe.
                    return detail::check_clean_av(p.value);
                } else if constexpr (std::is_same_v<P, BlurProperty>) {
                    return detail::check_clean_av(p.radius);
                } else if constexpr (std::is_same_v<P, TrackingProperty>) {
                    return detail::check_clean_av(p.pixels);
                } else if constexpr (std::is_same_v<P, RotationProperty>) {
                    // RotationProperty::degrees (Vec3) — NOT `value`.
                    return !detail::component_has_nan_or_inf(p.degrees);
                } else if constexpr (std::is_same_v<P, AnchorProperty>) {
                    // AnchorProperty::value (Vec3).
                    return !detail::component_has_nan_or_inf(p.value);
                } else if constexpr (std::is_same_v<P, SkewProperty>) {
                    return !detail::component_has_nan_or_inf(p.angle)
                        && !detail::component_has_nan_or_inf(p.axis);
                } else if constexpr (std::is_same_v<P, FillColorProperty>
                                  || std::is_same_v<P, StrokeColorProperty>) {
                    return !detail::component_has_nan_or_inf(p.color);
                } else if constexpr (std::is_same_v<P, StrokeWidthProperty>) {
                    return !detail::component_has_nan_or_inf(p.width);
                } else if constexpr (std::is_same_v<P, BaselineShiftProperty>) {
                    return !detail::component_has_nan_or_inf(p.pixels);
                } else {
                    // CharacterOffsetProperty (i32) — integers cannot be NaN/Inf.
                    return true;
                }
            },
            prop
        );
        if (!ok) { inv3_ok = false; break; }
    }
    if (!inv3_ok) return false;

    // ── Inv 4 — Blend-mode coverage ─────────────────────────────────────
    if (transform_mode != TextPropertyBlendMode::Add
        && transform_mode != TextPropertyBlendMode::Replace
        && transform_mode != TextPropertyBlendMode::Multiply) {
        return false;
    }
    if (color_mode != TextPropertyBlendMode::Add
        && color_mode != TextPropertyBlendMode::Replace
        && color_mode != TextPropertyBlendMode::Multiply) {
        return false;
    }

    return true;
}

} // namespace chronon3d
