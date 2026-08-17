#pragma once

// animation_intent.hpp — resolves the chronon.render-plan.v1 animation
// intent (motion preset + selector `unit` word/glyph/line + enter/exit frame
// durations) into a single `ResolvedAnimation`, then lowers it onto the
// canonical execution paths:
//
//   preset animation defaults + RenderPlan overrides = ResolvedAnimation
//        ↓ (layer motion)        ↓ (text selector units)
//   LayerBuilder::motion(        build_unit_reveal_animator()
//     preset, duration)               ↓
//   + transition_out()          TextAnimatorSpec (selector + enter/exit
//     (exit via the catalog)      opacity keyframes)
//
// The render-plan layer carries an EDITORIAL animation intent: WHAT motion
// to play, at WHAT granularity (word / glyph / line) and HOW LONG the
// enter/exit ramps last.  This module maps that intent onto the concrete
// types Chronon's pipelines consume unchanged.
//
// PURE and deterministic: identical inputs always produce an identical
// ResolvedAnimation / TextAnimatorSpec, so distributed workers agree
// byte-for-byte.

#include <chronon3d/core/types/frame.hpp>                  // Frame
#include <chronon3d/registry/visual_preset_descriptor.hpp>  // AnimationSpec
#include <chronon3d/render_plan/render_plan.hpp>            // LayerPlan, AnimationTiming
#include <chronon3d/text/animation/text_animator_spec.hpp>  // TextAnimatorSpec
#include <chronon3d/text/glyph_selector_spec.hpp>           // TextSelectorUnit, GlyphSelectorSpec

#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::render_plan {

/// Maps a render-plan animation `unit` string to the canonical text-run
/// selector granularity.  "glyph" → Glyph, "line" → Line; anything else
/// (including empty and "word") → Word (the dominant overlay case).
[[nodiscard]] chronon3d::TextSelectorUnit selector_unit(
    std::string_view unit) noexcept;

/// Composes the canonical per-unit reveal animator for a text layer: one
/// selector scoped to `unit` over the whole run, plus an enter opacity ramp
/// (0→1) and an exit ramp (1→0).  `start_frame` is the animation start;
/// `duration_frames` is the total window.  Absent enter/exit durations keep
/// the preset defaults (8 / 6 frames).
///
/// When the window is too short to hold enter + exit with at least one stable
/// frame between them, only the enter ramp is emitted — a deterministic clamp
/// (the same short window always yields the same spec).
[[nodiscard]] chronon3d::TextAnimatorSpec build_unit_reveal_animator(
    std::string_view unit,
    Frame start_frame,
    Frame duration_frames,
    std::optional<Frame> enter_duration_frames = std::nullopt,
    std::optional<Frame> exit_duration_frames = std::nullopt);

/// Resolved layer-level animation: the registry preset's animation defaults
/// merged with the render-plan's explicit overrides (plan wins).  All
/// durations are deterministically clamped into the layer window:
/// `enter_duration` ∈ [1, layer_duration]; `exit_duration` is 0 (no exit
/// transition) when the window cannot hold enter + exit with at least one
/// stable frame between them — the same clamp
/// `build_unit_reveal_animator()` applies, so layer motion and per-unit
/// text ramps always agree.
struct ResolvedAnimation {
    /// LayerBuilder motion preset id ("" = no layer motion).
    std::string preset;
    /// Text selector scope ("word" | "glyph" | "line"; "" = none).
    std::string unit;
    /// Entry ramp length, clamped to the layer window.
    Frame enter_duration{8};
    /// Exit ramp length in frames; 0 = no exit transition (no exit intent
    /// anywhere, or the window cannot hold enter + exit with a stable gap).
    Frame exit_duration{0};
    /// Layer window start (absolute composition frame).
    Frame layer_start{0};
    /// Layer window length (composition frames when the plan omits it).
    Frame layer_duration{0};
    /// True when any explicit unit/enter/exit intent exists anywhere — the
    /// per-unit text animator is only emitted in that case (a bare layer
    /// motion preset keeps the text run static).
    bool text_intent{false};
};

/// Resolve the layer-level animation intent:
///
///   preset animation defaults + RenderPlan animation overrides
///                                      = ResolvedAnimation
///
/// `preset_animation` is the registry descriptor's AnimationSpec (nullopt
/// when the layer has no visual preset); `layer` carries the explicit plan
/// overrides; `composition_frames` is the fallback layer window length.
/// Exit is only emitted when an exit intent exists (plan or preset) — a
/// bare `preset: fade_in` stays a one-shot entry animation.
[[nodiscard]] ResolvedAnimation resolve_animation(
    const std::optional<chronon3d::registry::AnimationSpec>& preset_animation,
    const LayerPlan& layer,
    Frame composition_frames);

}  // namespace chronon3d::render_plan
