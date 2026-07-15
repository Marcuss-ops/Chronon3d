// Node-level explicit access and motion preset implementations.

#include <chronon3d/scene/builders/layer_builder.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d {

NodeHandle LayerBuilder::last_node_handle() {
    if (m_layer.nodes.empty()) {
        static RenderNode sentinel;
        return NodeHandle(sentinel);
    }
    return NodeHandle(m_layer.nodes.back());
}

LayerBuilder& LayerBuilder::slide_in(Vec3 from, Frame duration, EasingCurve easing) {
    auto& position_track = position_anim();
    position_track.key(Frame{0}, from, easing);
    position_track.key(duration, m_layer.transform.position);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, easing);
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::soft_pop(Frame duration) {
    auto& scale_track = scale_anim();
    scale_track.key(
        Frame{0}, Vec3{0.90f, 0.90f, 1.0f},
        EasingCurve{Easing::OutBack});
    scale_track.key(duration, m_layer.transform.scale);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::float_idle(f32 amplitude_y, Frame cycle) {
    auto& position_track = position_anim();
    position_track.loop_mode(LoopMode::Loop);
    const int period = std::max(1, static_cast<int>(cycle));
    for (int frame = 0; frame <= period; ++frame) {
        const f32 phase = static_cast<f32>(frame) / static_cast<f32>(period);
        const f32 wave = std::sin(phase * 6.2831853071795864769f);
        position_track.key(
            Frame{frame}, Vec3{0.0f, std::round(wave * amplitude_y), 0.0f});
    }
    return *this;
}

LayerBuilder& LayerBuilder::depth_reveal(f32 depth_z, Frame duration) {
    m_layer.uses_2_5d_projection = true;
    m_layer.depth_offset = depth_z;
    Vec3 start_position = m_layer.transform.position;
    start_position.z += depth_z;
    auto& position_track = position_anim();
    position_track.key(
        Frame{0}, start_position, EasingCurve{Easing::OutCubic});
    position_track.key(duration, m_layer.transform.position);
    auto& scale_track = scale_anim();
    scale_track.key(
        Frame{0}, Vec3{0.94f, 0.94f, 1.0f},
        EasingCurve{Easing::OutCubic});
    scale_track.key(duration, m_layer.transform.scale);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::card_flip_2_5d(Frame duration) {
    m_layer.uses_2_5d_projection = true;
    Vec3 current_rotation = glm::degrees(
        glm::eulerAngles(m_layer.transform.rotation));
    Vec3 start_rotation = current_rotation;
    start_rotation.y -= 90.0f;
    auto& rotation_track = rotate_anim();
    rotation_track.key(
        Frame{0}, start_rotation, EasingCurve{Easing::OutCubic});
    rotation_track.key(duration, current_rotation);
    Vec3 start_position = m_layer.transform.position;
    start_position.z += 240.0f;
    auto& position_track = position_anim();
    position_track.key(
        Frame{0}, start_position, EasingCurve{Easing::OutCubic});
    position_track.key(duration, m_layer.transform.position);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic});
    opacity_track.key(
        Frame{static_cast<int>(duration * 0.6f)},
        m_layer.transform.opacity,
        EasingCurve{Easing::OutCubic});
    return *this;
}

LayerBuilder& LayerBuilder::settle(f32 overshoot, Frame duration) {
    auto& scale_track = scale_anim();
    scale_track.key(
        Frame{0}, m_layer.transform.scale * (1.0f + overshoot),
        EasingCurve{Easing::OutBack});
    scale_track.key(duration, m_layer.transform.scale);
    Vec3 current_rotation = glm::degrees(
        glm::eulerAngles(m_layer.transform.rotation));
    Vec3 start_rotation = current_rotation;
    start_rotation.z += 2.0f;
    auto& rotation_track = rotate_anim();
    rotation_track.key(
        Frame{0}, start_rotation, EasingCurve{Easing::OutBack});
    rotation_track.key(duration, current_rotation);
    Vec3 start_position = m_layer.transform.position;
    start_position.y += 8.0f;
    auto& position_track = position_anim();
    position_track.key(
        Frame{0}, start_position, EasingCurve{Easing::OutBack});
    position_track.key(duration, m_layer.transform.position);
    return *this;
}

LayerBuilder& LayerBuilder::fade_in(Frame duration, EasingCurve easing) {
    auto& track = opacity_anim();
    track.key(Frame{0}, 0.0f, easing);
    track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::focus_in(
    f32 start_blur, Frame duration, EasingCurve easing
) {
    auto& blur_track = blur_anim();
    blur_track.key(Frame{0}, start_blur, easing);
    blur_track.key(duration, 0.0f);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, easing);
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::scale_drop(
    f32 start_scale, Frame duration, EasingCurve easing
) {
    auto& scale_track = scale_anim();
    scale_track.key(
        Frame{0}, Vec3{start_scale, start_scale, 1.0f}, easing);
    scale_track.key(duration, m_layer.transform.scale);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, easing);
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::fade_shift_vertical(
    Vec3 offset, Frame duration, EasingCurve easing
) {
    auto& position_track = position_anim();
    position_track.key(Frame{0}, m_layer.transform.position + offset, easing);
    position_track.key(duration, m_layer.transform.position);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, easing);
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::fade_shift_horizontal(
    Vec3 offset, Frame duration, EasingCurve easing
) {
    return fade_shift_vertical(offset, duration, easing);
}

LayerBuilder& LayerBuilder::reveal_from_bottom(
    f32 distance, Frame duration, EasingCurve easing
) {
    Vec3 start_position = m_layer.transform.position;
    start_position.y += distance;
    auto& position_track = position_anim();
    position_track.key(Frame{0}, start_position, easing);
    position_track.key(duration, m_layer.transform.position);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, easing);
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::center_split(Frame duration, EasingCurve easing) {
    auto& scale_track = scale_anim();
    scale_track.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing);
    scale_track.key(duration, m_layer.transform.scale);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, easing);
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::underline_draw(Frame duration, EasingCurve easing) {
    auto& track = scale_anim();
    track.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing);
    track.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::highlight_block(Frame duration, EasingCurve easing) {
    auto& scale_track = scale_anim();
    scale_track.key(Frame{0}, Vec3{0.0f, 1.0f, 1.0f}, easing);
    scale_track.key(duration, m_layer.transform.scale);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, 0.0f, easing);
    opacity_track.key(duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::framing_bracket(Frame duration, EasingCurve easing) {
    auto& track = scale_anim();
    track.key(Frame{0}, Vec3{1.0f, 0.0f, 1.0f}, easing);
    track.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::word_stagger(
    Frame delay, Frame duration, EasingCurve easing
) {
    auto& track = opacity_anim();
    track.key(Frame{0}, 0.0f, easing);
    track.key(delay, 0.0f, easing);
    track.key(delay + duration, m_layer.transform.opacity);
    return *this;
}

LayerBuilder& LayerBuilder::tracking_breathing(
    f32 scale_factor, Frame duration, EasingCurve easing
) {
    auto& track = scale_anim();
    track.key(Frame{0}, m_layer.transform.scale * scale_factor, easing);
    track.key(duration, m_layer.transform.scale);
    return *this;
}

LayerBuilder& LayerBuilder::elegant_exit_vertical(
    Vec3 offset, Frame duration, EasingCurve easing
) {
    auto& position_track = position_anim();
    position_track.key(Frame{0}, m_layer.transform.position, easing);
    position_track.key(duration, m_layer.transform.position + offset);
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, m_layer.transform.opacity, easing);
    opacity_track.key(duration, 0.0f);
    return *this;
}

LayerBuilder& LayerBuilder::elegant_exit_horizontal(
    Vec3 offset, Frame duration, EasingCurve easing
) {
    return elegant_exit_vertical(offset, duration, easing);
}

LayerBuilder& LayerBuilder::curtain_close(
    Frame duration, EasingCurve easing
) {
    auto& scale_track = scale_anim();
    scale_track.key(Frame{0}, m_layer.transform.scale, easing);
    scale_track.key(
        duration,
        Vec3{m_layer.transform.scale.x, 0.0f, m_layer.transform.scale.z});
    auto& opacity_track = opacity_anim();
    opacity_track.key(Frame{0}, m_layer.transform.opacity, easing);
    opacity_track.key(duration, 0.0f);
    return *this;
}

} // namespace chronon3d
