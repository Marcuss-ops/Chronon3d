#include <chronon3d/render_plan/animation_intent.hpp>

#include <chronon3d/animation/easing/easing.hpp>               // Easing, EasingCurve
#include <chronon3d/text/animation/text_animator_properties.hpp>  // OpacityProperty

#include <algorithm>
#include <string>

namespace chronon3d::render_plan {

ResolvedAnimation resolve_animation(
    const std::optional<chronon3d::registry::AnimationSpec>& preset_animation,
    const LayerPlan& layer,
    Frame composition_frames) {
    ResolvedAnimation out;
    out.layer_start = layer.start_frame.value_or(Frame{0});
    out.layer_duration =
        layer.duration_frames.value_or(composition_frames);

    // Explicit RenderPlan overrides win; registry defaults fill the rest.
    std::optional<Frame> enter;
    std::optional<Frame> exit;
    if (layer.animation) {
        out.preset = layer.animation->preset;
        out.unit = layer.animation->unit;
        enter = layer.animation->enter_duration_frames;
        exit = layer.animation->exit_duration_frames;
    }
    if (preset_animation) {
        if (out.preset.empty()) out.preset = preset_animation->preset;
        if (out.unit.empty()) out.unit = preset_animation->unit;
        if (!enter && preset_animation->enter_duration_frames)
            enter = Frame{*preset_animation->enter_duration_frames};
        if (!exit && preset_animation->exit_duration_frames)
            exit = Frame{*preset_animation->exit_duration_frames};
    }

    // Any explicit unit/enter/exit intent anywhere → the per-unit text
    // animator is emitted; a bare layer motion preset keeps the text static.
    out.text_intent =
        !out.unit.empty() || enter.has_value() || exit.has_value();

    // Deterministic clamp into the layer window — the SAME rule
    // build_unit_reveal_animator() applies (enter-only when the window is
    // too short for a stable gap):
    //   enter ∈ [1, total]; exit kept only when enter + exit < total.
    // Exit defaults to 0 (no exit transition) when NEITHER the plan NOR the
    // registry preset carries an exit intent — a bare `preset: fade_in`
    // stays a one-shot entry, it is never turned into a double animation.
    const i64 total = std::max<i64>(out.layer_duration.integral(), 1);
    const i64 e = std::clamp<i64>(enter.value_or(Frame{8}).integral(), 1, total);
    out.enter_duration = Frame{e};
    if (exit.has_value()) {
        const i64 x = std::clamp<i64>(exit->integral(), 1, total);
        out.exit_duration = (e + x < total) ? Frame{x} : Frame{0};
    } else {
        out.exit_duration = Frame{0};
    }
    return out;
}

chronon3d::TextSelectorUnit selector_unit(std::string_view unit) noexcept {
    if (unit == "glyph") return chronon3d::TextSelectorUnit::Glyph;
    if (unit == "line") return chronon3d::TextSelectorUnit::Line;
    return chronon3d::TextSelectorUnit::Word;  // "word" + empty/unknown default
}

chronon3d::TextAnimatorSpec build_unit_reveal_animator(
    std::string_view unit,
    Frame start_frame,
    Frame duration_frames,
    std::optional<Frame> enter_duration_frames,
    std::optional<Frame> exit_duration_frames) {
    const i64 total = std::max<i64>(duration_frames.integral(), 1);
    // Clamp durations into [1, total]; absent values keep the preset
    // defaults (8 enter / 6 exit) but never exceed the window.
    const i64 enter = enter_duration_frames
        ? std::clamp<i64>(enter_duration_frames->integral(), 1, total)
        : std::min<i64>(8, total);
    const i64 exit = exit_duration_frames
        ? std::clamp<i64>(exit_duration_frames->integral(), 1, total)
        : std::min<i64>(6, total);

    const chronon3d::TextSelectorUnit scope = selector_unit(unit);

    chronon3d::TextAnimatorSpec animator;
    animator.id = std::string{"rp_unit_reveal_"} + std::string{unit};
    animator.enabled = true;

    // Single selector over the whole run, scoped to the requested unit.
    chronon3d::GlyphSelectorSpec selector;
    selector.id = animator.id + "_selector";
    selector.unit = scope;
    selector.shape = chronon3d::TextSelectorShape::Square;
    selector.start = {0.0f};
    selector.end = {100.0f};
    selector.amount = {100.0f};
    selector.exclude_spaces = (scope != chronon3d::TextSelectorUnit::Glyph);
    animator.selectors.push_back(std::move(selector));

    const Frame f0 = start_frame;
    const Frame end = start_frame + Frame{total};

    // Enter ramp 0→1, then an exit ramp 1→0 when the window has a stable
    // gap between them (enter + exit < total).  Monotonic keyframes satisfy
    // TextAnimatorSpec::is_valid() Inv 2 (strictly increasing frames).
    chronon3d::OpacityProperty opacity;
    opacity.value.clear();
    opacity.value.add_keyframe(f0, 0.0f);
    opacity.value.add_keyframe(f0 + Frame{enter}, 1.0f,
                               chronon3d::EasingCurve{chronon3d::Easing::OutCubic});
    if (enter + exit < total) {
        opacity.value.add_keyframe(end - Frame{exit}, 1.0f);
        opacity.value.add_keyframe(end, 0.0f,
                                   chronon3d::EasingCurve{chronon3d::Easing::InCubic});
    }
    animator.properties.push_back(std::move(opacity));
    return animator;
}

}  // namespace chronon3d::render_plan
