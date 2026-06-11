// =============================================================================
// Motion Presets — Reveal / Transform
//
// One-shot entry animations: fade, slide, scale, pop, blur, text reveal.
// All variants converge to a settled state by `t = 1` of the animation window.
// =============================================================================

#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <cmath>

namespace chronon3d::presets::motion {

void register_reveal_presets(MotionPresetRegistry& r) {
    r.register_preset({
        MotionPreset::None, "None", [](const FrameContext&, const MotionObject&, f32, MotionState&) {}
    });

    r.register_preset({
        MotionPreset::FadeIn, "FadeIn", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::FadeLift, "FadeLift", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.y += interpolate(t, 0.0f, 0.36f, 48.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.26f, 8.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::PopIn, "PopIn", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.28f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.28f, 0.84f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        }
    });

    r.register_preset({
        MotionPreset::PopGlow, "PopGlow", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.26f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.28f, 0.86f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.24f, 10.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::SlideUp, "SlideUp", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.y += interpolate(t, 0.0f, 0.32f, 72.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::SlideLeft, "SlideLeft", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.32f, 108.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::SlideIn, "SlideIn", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.34f, 120.0f, 0.0f, Easing::OutCubic);
            st.position.y += interpolate(t, 0.0f, 0.34f, 24.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.24f, 6.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::SoftPop, "SoftPop", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.28f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.30f, 0.90f, 1.0f, Easing::OutBack);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.22f, 4.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::ZoomBlur, "ZoomBlur", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.32f, 1.18f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.26f, 14.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::StaggerReveal, "StaggerReveal", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
            st.text_reveal = interpolate(t, 0.0f, 0.82f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.28f, -42.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.20f, 10.0f, 0.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.24f, 0.98f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        }
    });

    r.register_preset({
        MotionPreset::MaskSweep, "MaskSweep", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.34f, 0.0f, 1.0f, Easing::OutCubic);
            st.mask_reveal = interpolate(t, 0.0f, 0.82f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.34f, -18.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.24f, 4.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::TypewriterReveal, "TypewriterReveal", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.14f, 0.0f, 1.0f, Easing::OutCubic);
            st.text_reveal = interpolate(t, 0.0f, 0.80f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.20f, -24.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.12f, 8.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::KineticBounce, "KineticBounce", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.35f, 0.78f, 1.0f, Easing::OutBack);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.y += interpolate(t, 0.0f, 0.24f, 42.0f, 0.0f, Easing::OutBounce);
            st.rotation.z += interpolate(t, 0.0f, 0.24f, -3.0f, 0.0f, Easing::OutCubic);
        }
    });
}

} // namespace chronon3d::presets::motion
