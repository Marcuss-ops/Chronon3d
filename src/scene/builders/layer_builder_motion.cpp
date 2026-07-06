// ============================================================================
// layer_builder_motion.cpp — Motion timeline adapters for LayerBuilder
// ============================================================================
//
// FASE 22 split: layer_builder.cpp was monolithic (~456L) and carried
// constructor logic, simple setters, motion timeline adapters, font
// engine getter/setter, `text_run()` and `build()` in one TU.  This
// file owns ONLY the motion timeline adapter surface:
//
//   apply_axis_timeline  (anonymous-namespace helper)
//   rotate_x / rotate_y / rotate_z
//   position_x / position_y / position_z
//   opacity_timeline
//   scale_x / scale_y
//   blur_timeline
//
// Each adapter maps a single-axis `motion::Timeline<f32>` onto one
// axis of an `AnimatedValue<Vec3>` (position / scale / rotate) or the
// whole `AnimatedValue<f32>` (opacity / blur), preserving the other
// axes at their base values.
//
// Dependency: layer_builder.hpp (class declared there).  All transitive
// includes motion/timeline.hpp, glm_types.hpp, cmath + utility through
// the public header — no additional top-level includes needed.
// ============================================================================

#include <chronon3d/scene/builders/layer_builder.hpp>

namespace chronon3d {

namespace {

/// Helper: apply a single-axis timeline to an AnimatedValue<Vec3>,
/// keeping the other two axes at their base values.
void apply_axis_timeline(AnimatedValue<Vec3>& dest, Vec3 base,
                         int axis,  // 0=X, 1=Y, 2=Z
                         const motion::Timeline<f32>& timeline) {
    dest.clear();
    for (const auto& pt : timeline.points()) {
        Vec3 v = base;
        v[axis] = pt.value;
        dest.key(pt.frame, v, pt.outgoing_easing);
    }
}

} // anonymous namespace

LayerBuilder& LayerBuilder::rotate_x(const motion::Timeline<f32>& timeline) {
    Vec3 base = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    apply_axis_timeline(rotate_anim(), base, 0, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::rotate_y(const motion::Timeline<f32>& timeline) {
    Vec3 base = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    apply_axis_timeline(rotate_anim(), base, 1, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::rotate_z(const motion::Timeline<f32>& timeline) {
    Vec3 base = glm::degrees(glm::eulerAngles(m_layer.transform.rotation));
    apply_axis_timeline(rotate_anim(), base, 2, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::position_x(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.position;
    apply_axis_timeline(position_anim(), base, 0, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::position_y(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.position;
    apply_axis_timeline(position_anim(), base, 1, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::position_z(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.position;
    apply_axis_timeline(position_anim(), base, 2, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::opacity_timeline(const motion::Timeline<f32>& timeline) {
    auto& anim = opacity_anim();
    anim.clear();
    for (const auto& pt : timeline.points()) {
        anim.key(pt.frame, pt.value, pt.outgoing_easing);
    }
    return *this;
}

LayerBuilder& LayerBuilder::scale_x(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.scale;
    apply_axis_timeline(scale_anim(), base, 0, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::scale_y(const motion::Timeline<f32>& timeline) {
    Vec3 base = m_layer.transform.scale;
    apply_axis_timeline(scale_anim(), base, 1, timeline);
    return *this;
}

LayerBuilder& LayerBuilder::blur_timeline(const motion::Timeline<f32>& timeline) {
    auto& anim = blur_anim();
    anim.clear();
    for (const auto& pt : timeline.points()) {
        anim.key(pt.frame, pt.value, pt.outgoing_easing);
    }
    return *this;
}

} // namespace chronon3d
