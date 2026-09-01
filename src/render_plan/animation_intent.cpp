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

    // Explicit 0 disables the duration; positive values are clamped to [1, total].
    // Unspecified enter defaults to Frame{8}, unspecified exit defaults to Frame{0}.
    const i64 total = std::max<i64>(out.layer_duration.integral(), 1);
    if (enter.has_value()) {
        const i64 raw = enter->integral();
        out.enter_duration = (raw <= 0) ? Frame{0} : Frame{std::clamp<i64>(raw, 1, total)};
    } else {
        out.enter_duration = Frame{std::clamp<i64>(8, 1, total)};
    }

    if (exit.has_value()) {
        const i64 raw = exit->integral();
        if (raw <= 0) {
            out.exit_duration = Frame{0};
        } else {
            const i64 x = std::clamp<i64>(raw, 1, total);
            const i64 e = out.enter_duration.integral();
            out.exit_duration = (e + x < total) ? Frame{x} : Frame{0};
        }
    } else {
        out.exit_duration = Frame{0};
    }

    // Any explicit unit or active enter/exit intent -> the per-unit text
    // animator is emitted; a bare layer motion preset keeps the text static.
    out.text_intent =
        !out.unit.empty() ||
        (enter.has_value() && enter->integral() > 0) ||
        (exit.has_value() && exit->integral() > 0);

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
    // Durations <= 0 are treated as disabled (0 frames). Absent values keep
    // defaults (8 enter / 6 exit) clamped into the window.
    const i64 enter = enter_duration_frames
        ? (enter_duration_frames->integral() <= 0 ? 0 : std::clamp<i64>(enter_duration_frames->integral(), 1, total))
        : std::min<i64>(8, total);
    const i64 exit = exit_duration_frames
        ? (exit_duration_frames->integral() <= 0 ? 0 : std::clamp<i64>(exit_duration_frames->integral(), 1, total))
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

    // Enter ramp 0->1 (or instant 1 if enter == 0), then exit ramp 1->0 when
    // exit > 0 and enter + exit < total. Monotonic keyframes satisfy
    // TextAnimatorSpec::is_valid() Inv 2 (strictly increasing frames).
    chronon3d::OpacityProperty opacity;
    opacity.value.clear();
    if (enter > 0) {
        opacity.value.add_keyframe(f0, 0.0f);
        opacity.value.add_keyframe(f0 + Frame{enter}, 1.0f,
                                   chronon3d::EasingCurve{chronon3d::Easing::OutCubic});
    } else {
        opacity.value.add_keyframe(f0, 1.0f);
    }
    if (exit > 0 && enter + exit < total) {
        opacity.value.add_keyframe(end - Frame{exit}, 1.0f);
        opacity.value.add_keyframe(end, 0.0f,
                                   chronon3d::EasingCurve{chronon3d::Easing::InCubic});
    }
    animator.properties.push_back(std::move(opacity));
    return animator;
}

}  // namespace chronon3d::render_plan
