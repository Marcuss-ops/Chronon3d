#include <chronon3d/presets/motion_resolver.hpp>
#include <chronon3d/animation/interpolate.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::presets::motion {

static f32 clamp01(f32 v) {
    return std::clamp(v, 0.0f, 1.0f);
}

MotionState resolve_motion_state(const FrameContext& ctx, const MotionObject& obj) {
    MotionState st;

    st.visible = obj.time_value.contains(ctx.frame);
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

    if (!st.visible) {
        st.opacity = 0.0f;
        return st;
    }

    const f32 t = obj.time_value.normalized(ctx.frame);

    switch (obj.preset_value) {
    case MotionPreset::None:
        break;

    case MotionPreset::FadeIn:
        st.opacity *= interpolate(t, 0.0f, 0.22f, 0.0f, 1.0f, Easing::OutCubic);
        break;

    case MotionPreset::PopIn: {
        st.opacity *= interpolate(t, 0.0f, 0.20f, 0.0f, 1.0f, Easing::OutCubic);
        const f32 s = interpolate(t, 0.0f, 0.22f, 0.78f, 1.0f, Easing::OutBack);
        st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        break;
    }

    case MotionPreset::PopGlow: {
        st.opacity *= interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
        const f32 s = interpolate(t, 0.0f, 0.22f, 0.82f, 1.0f, Easing::OutBack);
        st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        st.blur = interpolate(t, 0.0f, 0.20f, 12.0f, 0.0f, Easing::OutCubic);
        break;
    }

    case MotionPreset::SlideUp:
        st.opacity *= interpolate(t, 0.0f, 0.25f, 0.0f, 1.0f, Easing::OutCubic);
        st.position.y += interpolate(t, 0.0f, 0.25f, 64.0f, 0.0f, Easing::OutCubic);
        break;

    case MotionPreset::SlideLeft:
        st.opacity *= interpolate(t, 0.0f, 0.25f, 0.0f, 1.0f, Easing::OutCubic);
        st.position.x += interpolate(t, 0.0f, 0.25f, 96.0f, 0.0f, Easing::OutCubic);
        break;

    case MotionPreset::ZoomBlur: {
        st.opacity *= interpolate(t, 0.0f, 0.25f, 0.0f, 1.0f, Easing::OutCubic);
        const f32 s = interpolate(t, 0.0f, 0.25f, 1.22f, 1.0f, Easing::OutCubic);
        st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        st.blur = interpolate(t, 0.0f, 0.25f, 18.0f, 0.0f, Easing::OutCubic);
        break;
    }

    case MotionPreset::PushIn3D: {
        st.opacity *= interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
        const f32 s = interpolate(t, 0.0f, 0.25f, 0.82f, 1.0f, Easing::OutBack);
        st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        st.position.z += interpolate(t, 0.0f, 0.35f, 280.0f, 0.0f, Easing::OutCubic);
        st.rotation.y += interpolate(t, 0.0f, 0.35f, -18.0f, 0.0f, Easing::OutCubic);
        st.blur = interpolate(t, 0.0f, 0.22f, 18.0f, 0.0f, Easing::OutCubic);
        break;
    }

    case MotionPreset::ParallaxDrift:
        st.position.x += std::sin(static_cast<f32>(ctx.frame) * 0.025f) * 20.0f * obj.motion3d.parallax;
        st.position.y += std::cos(static_cast<f32>(ctx.frame) * 0.018f) * 12.0f * obj.motion3d.parallax;
        st.rotation.y += std::sin(static_cast<f32>(ctx.frame) * 0.02f) * 4.0f;
        st.rotation.x += std::cos(static_cast<f32>(ctx.frame) * 0.017f) * 2.5f;
        break;

    case MotionPreset::Orbit2_5D: {
        constexpr f32 pi = 3.14159265358979323846f;
        const f32 angle = interpolate(t, 0.0f, 1.0f, -12.0f, 12.0f, Easing::InOutCubic);
        st.rotation.y += angle;
        st.position.x += std::sin(t * pi * 2.0f) * 40.0f;
        st.position.z += std::cos(t * pi * 2.0f) * 80.0f;
        break;
    }

    case MotionPreset::ShakeImpact: {
        const f32 amp = interpolate(t, 0.0f, 0.35f, 18.0f, 0.0f, Easing::OutCubic);
        st.position.x += std::sin(static_cast<f32>(ctx.frame) * 1.7f) * amp;
        st.position.y += std::cos(static_cast<f32>(ctx.frame) * 2.1f) * amp;
        break;
    }
    }

    st.opacity = clamp01(st.opacity);
    return st;
}

} // namespace chronon3d::presets::motion
