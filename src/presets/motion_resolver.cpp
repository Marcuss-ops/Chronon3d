#include <chronon3d/presets/motion_resolver.hpp>
#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::presets::motion {

static f32 clamp01(f32 v) {
    return std::clamp(v, 0.0f, 1.0f);
}

f32 sweep_wave(Frame frame, Frame start_frame, const Motion3D::Sweep2_5D& sweep) {
    if (!sweep.enabled) return 0.0f;

    const f32 t = static_cast<f32>(frame) - static_cast<f32>(start_frame) - sweep.start_delay;
    if (sweep.one_shot) {
        if (t <= 0.0f) return sweep.sweep_from;
        const f32 duration = std::max(sweep.sweep_duration_frames, 1.0f);
        const f32 progress = clamp01(t / duration);
        return sweep.sweep_from + (sweep.sweep_to - sweep.sweep_from) * progress;
    }

    if (t <= 0.0f) return 0.0f;

    constexpr f32 kTwoPi = 6.28318530718f;
    const f32 period = std::max(sweep.period_frames, 1.0f);
    const f32 phase = (t / period) * kTwoPi + sweep.phase_frames;
    return std::sin(phase);
}

MotionState resolve_motion_state(const FrameContext& ctx, const MotionObject& obj) {
    MotionState st;

    st.visible = obj.time_value.contains(ctx.frame.frame.frame);
    st.position = obj.position_value + obj.motion3d.position;
    st.position.z += obj.motion3d.z;
    st.scale = {
        obj.scale_value.x * obj.motion3d.scale.x,
        obj.scale_value.y * obj.motion3d.scale.y,
        obj.scale_value.z * obj.motion3d.scale.z
    };
    st.rotation = obj.rotation_value + obj.motion3d.rotation;
    st.opacity = obj.opacity_value;
    st.blur = 0.0f;
    st.text_reveal = 1.0f;
    st.mask_reveal = 1.0f;

    st.effects.glow_enabled = obj.style.glow_enabled;
    st.effects.glow = obj.style.glow;
    st.effects.shadow_enabled = obj.style.shadow_enabled;
    st.effects.shadow = obj.style.shadow;
    st.effects.bloom_enabled = obj.style.bloom_enabled;
    st.effects.bloom = obj.style.bloom;

    if (obj.motion3d.sweep_2_5d.enabled) {
        const f32 wave = sweep_wave(ctx.frame.frame.frame, obj.time_value.start, obj.motion3d.sweep_2_5d);
        const auto& sweep = obj.motion3d.sweep_2_5d;
        st.position.x += sweep.position_amplitude.x * wave;
        st.position.y += sweep.position_amplitude.y * wave;
        st.position.z += sweep.position_amplitude.z * wave;
        st.rotation.x += sweep.rotation_amplitude.x * wave;
        st.rotation.y += sweep.rotation_amplitude.y * wave;
        st.rotation.z += sweep.rotation_amplitude.z * wave;
        const f32 scale_delta = 1.0f + sweep.scale_amount * std::abs(wave);
        st.scale = {st.scale.x * scale_delta, st.scale.y * scale_delta, st.scale.z};
    }

    if (!st.visible) {
        st.opacity = 0.0f;
        return st;
    }

    const f32 t = obj.time_value.normalized(ctx.frame.frame.frame);

    auto& registry = MotionPresetRegistry::instance();
    if (registry.contains(obj.preset_value)) {
        registry.get(obj.preset_value).resolve(ctx, obj, t, st);
    }

    // Apply custom modular animations
    AnimationContext actx{
        ctx.frame.frame.frame,
        obj.time_value.end - obj.time_value.start,
        ctx.frame.frame.fps()
    };
    for (const auto& anim : obj.get_animations()) {
        anim.apply(actx, st);
    }

    st.opacity = clamp01(st.opacity);
    return st;
}

} // namespace chronon3d::presets::motion
