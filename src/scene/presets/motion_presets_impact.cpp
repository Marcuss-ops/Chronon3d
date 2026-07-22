// =============================================================================
// Motion Presets — Impact / Shake / Glitch
//
// High-energy, frame-rate-coupled disturbances: shake, glitch, focus pull,
// settle.  These use `ctx.frame` for high-frequency noise that decouples
// from the animation's normalized `t`.
// =============================================================================

#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <cmath>

namespace chronon3d::presets::motion {

void register_impact_presets(MotionPresetRegistry& r) {
    r.register_preset({
        MotionPreset::FocusPull, "FocusPull", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.24f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.34f, 1.05f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.y += interpolate(t, 0.0f, 0.28f, 12.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.18f, 2.5f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::ShakeImpact, "ShakeImpact", [](const FrameContext& ctx, const MotionObject&, f32 t, MotionState& st) {
            const f32 amp = interpolate(t, 0.0f, 0.35f, 18.0f, 0.0f, Easing::OutCubic);
            st.position.x += std::sin(static_cast<f32>(ctx.frame()) * 1.7f) * amp;
            st.position.y += std::cos(static_cast<f32>(ctx.frame()) * 2.1f) * amp;
        }
    });

    r.register_preset({
        MotionPreset::GlitchIn, "GlitchIn", [](const FrameContext& ctx, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.08f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += std::sin(static_cast<f32>(ctx.frame()) * 4.2f) * interpolate(t, 0.0f, 0.18f, 22.0f, 0.0f, Easing::OutCubic);
            st.position.y += std::cos(static_cast<f32>(ctx.frame()) * 3.7f) * interpolate(t, 0.0f, 0.18f, 14.0f, 0.0f, Easing::OutCubic);
            st.rotation.z += std::sin(static_cast<f32>(ctx.frame()) * 5.0f) * interpolate(t, 0.0f, 0.18f, 4.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.12f, 14.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::NewsImpact, "NewsImpact", [](const FrameContext& ctx, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.15f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.25f, 1.6f, 1.0f, Easing::OutBack);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            const f32 shake_amp = interpolate(t, 0.0f, 0.35f, 24.0f, 0.0f, Easing::OutCubic);
            const f32 frame = static_cast<f32>(ctx.frame());
            st.position.x += std::sin(frame * 2.2f) * shake_amp;
            st.position.y += std::cos(frame * 1.8f) * shake_amp;
            st.effects.glow_enabled = true;
            st.effects.glow.intensity = interpolate(t, 0.0f, 0.40f, 1.8f, 0.30f, Easing::OutCubic);
            st.effects.glow.radius = interpolate(t, 0.0f, 0.40f, 60.0f, 20.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::Settle, "Settle", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            const f32 s = interpolate(t, 0.0f, 0.32f, 1.08f, 1.0f, Easing::OutBack);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.rotation.z += interpolate(t, 0.0f, 0.28f, 2.0f, 0.0f, Easing::OutBack);
            st.position.y += interpolate(t, 0.0f, 0.28f, 8.0f, 0.0f, Easing::OutBack);
            st.blur = interpolate(t, 0.0f, 0.20f, 3.0f, 0.0f, Easing::OutCubic);
        }
    });
}

} // namespace chronon3d::presets::motion
