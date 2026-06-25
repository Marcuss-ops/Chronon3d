#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/effects/effect_ids.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

// ── Motion Presets ────────────────────────────────────────────────────
// Extracted from layer_builder.cpp in Phase 6 to reduce file size.
// These are the original LayerBuilder methods with full parameter support.
// The registry-based commands in motion_preset_commands.cpp provide
// parameterless versions for extension modules.

LayerBuilder& LayerBuilder::slide_in(Vec3 from, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, from, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::soft_pop(Frame duration) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.90f, 0.90f, 1.0f}, EasingCurve{Easing::OutBack});
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::float_idle(f32 amplitude_y, Frame cycle) {
    auto& pos = position_anim();
    pos.loop_mode(LoopMode::Loop);

    const int period = std::max(1, static_cast<int>(cycle));
    for (int f = 0; f <= period; ++f) {
        const f32 phase = static_cast<f32>(f) / static_cast<f32>(period);
        const f32 wave = std::sin(phase * 6.2831853071795864769f);
        const f32 snapped_y = std::round(wave * amplitude_y);
        pos.key(Frame{f}, Vec3{0.0f, snapped_y, 0.0f});
    }
    return *this;
}

LayerBuilder& LayerBuilder::depth_reveal(f32 depth_z, Frame duration) {
    m_layer.uses_2_5d_projection = true;
    // Initialise depth_offset to the configured z so callers (tests,
    // downstream consumers like tests/test_text_preset_registry.cpp
    // Sub-cases 9 + 29) can probe the directional `depth_offset > 0.0f`
    // invariant without depending on the position_anim evaluation
    // order inside `LayerBuilder::build()`.  This was previously missing,
    // making Sub-cases 9 + 29 silently fail once the FAIL_TEST compile
    // block was unblocked.  Pre-existing rot surfaced during the P1
    // emergency-cleanup pass; fixed here as a 1-line contract restoration.
    m_layer.depth_offset = depth_z;

    auto& pos = position_anim();
    Vec3 start = m_layer.transform.position;
    start.z += depth_z;
    pos.key(Frame{0}, start, EasingCurve{Easing::OutCubic});
    pos.key(duration, m_layer.transform.position);

    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic});
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::card_flip_2_5d(Frame duration) {
    m_layer.uses_2_5d_projection = true;

    auto& rot = rotate_anim();
    Vec3 current_euler = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    Vec3 start_euler = current_euler;
    start_euler.y -= 90.0f;
    rot.key(Frame{0}, start_euler, EasingCurve{Easing::OutCubic});
    rot.key(duration, current_euler);

    auto& pos = position_anim();
    Vec3 start_pos = m_layer.transform.position;
    start_pos.z += 240.0f;
    pos.key(Frame{0}, start_pos, EasingCurve{Easing::OutCubic});
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    op.key(Frame{static_cast<int>(duration * 0.6f)}, m_layer.transform.opacity, EasingCurve{Easing::OutCubic});
    return *this;
}

LayerBuilder& LayerBuilder::settle(f32 overshoot, Frame duration) {
    auto& sc = scale_anim();
    Vec3 start = m_layer.transform.scale * (1.0f + overshoot);
    sc.key(Frame{0}, start, EasingCurve{Easing::OutBack});
    sc.key(duration, m_layer.transform.scale);

    auto& rot = rotate_anim();
    Vec3 current_euler = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    Vec3 start_rot = current_euler;
    start_rot.z += 2.0f;
    rot.key(Frame{0}, start_rot, EasingCurve{Easing::OutBack});
    rot.key(duration, current_euler);

    auto& pos = position_anim();
    Vec3 start_pos = m_layer.transform.position;
    start_pos.y += 8.0f;
    pos.key(Frame{0}, start_pos, EasingCurve{Easing::OutBack});
    pos.key(duration, m_layer.transform.position);
    return *this;
}

LayerBuilder& LayerBuilder::fade_in(Frame duration, EasingCurve easing) {
    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::focus_in(f32 start_blur, Frame duration, EasingCurve easing) {
    auto& bl = blur_anim();
    bl.key(Frame{0}, start_blur, easing);
    bl.key(duration, 0.0f);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}



LayerBuilder& LayerBuilder::scale_drop(f32 start_scale, Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{start_scale, start_scale, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::fade_shift_vertical(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position + offset, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::fade_shift_horizontal(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position + offset, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::reveal_from_bottom(f32 distance, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    Vec3 start = m_layer.transform.position;
    start.y += distance;
    pos.key(Frame{0}, start, easing);
    pos.key(duration, m_layer.transform.position);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::center_split(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::underline_draw(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::highlight_block(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);

    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::framing_bracket(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing);
    sc.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::word_stagger(Frame delay, Frame duration, EasingCurve easing) {
    auto& op = opacity_anim();
    op.key(Frame{0}, 0.0f, easing);
    op.key(delay, 0.0f, easing);
    op.key(delay + duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::tracking_breathing(f32 scale_factor, Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, m_layer.transform.scale * scale_factor, easing);
    sc.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::elegant_exit_vertical(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position, easing);
    pos.key(duration, m_layer.transform.position + offset);

    auto& op = opacity_anim();
    op.key(Frame{0}, m_layer.transform.opacity, easing);
    op.key(duration, 0.0f);
    return *this;
}

LayerBuilder& LayerBuilder::elegant_exit_horizontal(Vec3 offset, Frame duration, EasingCurve easing) {
    auto& pos = position_anim();
    pos.key(Frame{0}, m_layer.transform.position, easing);
    pos.key(duration, m_layer.transform.position + offset);

    auto& op = opacity_anim();
    op.key(Frame{0}, m_layer.transform.opacity, easing);
    op.key(duration, 0.0f);
    return *this;
}

LayerBuilder& LayerBuilder::curtain_close(Frame duration, EasingCurve easing) {
    auto& sc = scale_anim();
    sc.key(Frame{0}, m_layer.transform.scale, easing);
    sc.key(duration, Vec3{m_layer.transform.scale.x, 0.0f, m_layer.transform.scale.z});

    auto& op = opacity_anim();
    op.key(Frame{0}, m_layer.transform.opacity, easing);
    op.key(duration, 0.0f);
    return *this;
}

} // namespace chronon3d
