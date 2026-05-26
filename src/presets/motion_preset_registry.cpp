#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/animation/interpolate.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::presets::motion {

MotionPresetRegistry& MotionPresetRegistry::instance() {
    static MotionPresetRegistry inst;
    return inst;
}

MotionPresetRegistry::MotionPresetRegistry() {
    register_builtins();
}

void MotionPresetRegistry::register_preset(MotionPresetDescriptor desc) {
    m_presets[desc.preset] = std::move(desc);
}

bool MotionPresetRegistry::contains(MotionPreset preset) const {
    return m_presets.find(preset) != m_presets.end();
}

const MotionPresetDescriptor& MotionPresetRegistry::get(MotionPreset preset) const {
    auto it = m_presets.find(preset);
    if (it != m_presets.end()) {
        return it->second;
    }
    static const MotionPresetDescriptor fallback{
        MotionPreset::None, "None", [](auto&, auto&, auto, auto&) {}
    };
    return fallback;
}

void MotionPresetRegistry::register_builtins() {
    register_preset({
        MotionPreset::None, "None", [](const FrameContext&, const MotionObject&, f32, MotionState&) {}
    });

    register_preset({
        MotionPreset::FadeIn, "FadeIn", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::FadeLift, "FadeLift", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.y += interpolate(t, 0.0f, 0.36f, 48.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.26f, 8.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::PopIn, "PopIn", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.28f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.28f, 0.84f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        }
    });

    register_preset({
        MotionPreset::PopGlow, "PopGlow", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.26f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.28f, 0.86f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.24f, 10.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::SlideUp, "SlideUp", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.y += interpolate(t, 0.0f, 0.32f, 72.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::SlideLeft, "SlideLeft", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.32f, 108.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::ZoomBlur, "ZoomBlur", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.32f, 1.18f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.26f, 14.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::PushIn3D, "PushIn3D", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.28f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.30f, 0.88f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.z += interpolate(t, 0.0f, 0.40f, 260.0f, 0.0f, Easing::OutCubic);
            st.rotation.y += interpolate(t, 0.0f, 0.40f, -14.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.26f, 14.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::SoftDollyReveal, "SoftDollyReveal", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.38f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.42f, 0.94f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.30f, 4.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::GlowBloom, "GlowBloom", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.34f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.30f, 1.03f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.28f, 14.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::StaggerReveal, "StaggerReveal", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
            st.text_reveal = interpolate(t, 0.0f, 0.82f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.28f, -42.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.20f, 10.0f, 0.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.24f, 0.98f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
        }
    });

    register_preset({
        MotionPreset::ParallaxDrift, "ParallaxDrift", [](const FrameContext& ctx, const MotionObject& obj, f32, MotionState& st) {
            st.position.x += std::sin(static_cast<f32>(ctx.frame) * 0.025f) * 20.0f * obj.motion3d.parallax;
            st.position.y += std::cos(static_cast<f32>(ctx.frame) * 0.018f) * 12.0f * obj.motion3d.parallax;
            st.rotation.y += std::sin(static_cast<f32>(ctx.frame) * 0.02f) * 4.0f;
            st.rotation.x += std::cos(static_cast<f32>(ctx.frame) * 0.017f) * 2.5f;
        }
    });

    register_preset({
        MotionPreset::Orbit2_5D, "Orbit2_5D", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            constexpr f32 pi = 3.14159265358979323846f;
            const f32 angle = interpolate(t, 0.0f, 1.0f, -12.0f, 12.0f, Easing::InOutCubic);
            st.rotation.y += angle;
            st.position.x += std::sin(t * pi * 2.0f) * 40.0f;
            st.position.z += std::cos(t * pi * 2.0f) * 80.0f;
        }
    });

    register_preset({
        MotionPreset::TiltSweep2_5D, "TiltSweep2_5D", [](const FrameContext&, const MotionObject&, f32, MotionState&) {
            // Handled natively by Sweep2_5D amplitude calculation
        }
    });

    register_preset({
        MotionPreset::PerspectiveSweepTextReveal, "PerspectiveSweepTextReveal", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            const f32 p = t;
            const f32 reveal = interpolate(p, 0.0f, 0.86f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 settle = std::clamp((reveal - 0.62f) / 0.38f, 0.0f, 1.0f);

            st.opacity *= interpolate(p, 0.0f, 0.14f, 0.0f, 1.0f, Easing::OutCubic);
            st.text_reveal = reveal;

            st.position.x += interpolate(p, 0.0f, 0.30f, -120.0f, 0.0f, Easing::OutCubic);
            st.position.y += interpolate(p, 0.0f, 0.30f, 6.0f, 0.0f, Easing::OutCubic);
            st.position.z += interpolate(p, 0.0f, 0.30f, 140.0f, 0.0f, Easing::OutCubic);

            st.rotation.x += interpolate(p, 0.0f, 0.30f, 3.0f, 0.0f, Easing::OutCubic);
            st.rotation.y += interpolate(p, 0.0f, 0.30f, -34.0f, 0.0f, Easing::OutCubic);
            st.rotation.z += interpolate(p, 0.0f, 0.30f, 1.5f, 0.0f, Easing::OutCubic);

            const f32 sx = interpolate(p, 0.0f, 0.30f, 0.90f, 1.0f, Easing::OutCubic);
            const f32 sy = interpolate(p, 0.0f, 0.30f, 0.96f, 1.0f, Easing::OutCubic);
            st.scale = {st.scale.x * sx, st.scale.y * sy, st.scale.z};

            st.blur = interpolate(p, 0.0f, 0.18f, 9.0f, 0.0f, Easing::OutCubic);

            if (settle > 0.0f) {
                const f32 settle_wave = std::sin(settle * 3.1415926535f);
                st.rotation.y *= (1.0f - 0.06f * settle);
                st.rotation.x += 0.6f * settle_wave * settle;
                st.rotation.z += 0.4f * settle_wave * settle;
                st.position.z -= 8.0f * settle;
                st.position.x += 18.0f * settle_wave * settle;
                st.scale.x += 0.01f * settle_wave * settle;
                st.scale.y += 0.005f * settle_wave * settle;
            }
        }
    });

    register_preset({
        MotionPreset::MaskSweep, "MaskSweep", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.34f, 0.0f, 1.0f, Easing::OutCubic);
            st.mask_reveal = interpolate(t, 0.0f, 0.82f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.34f, -18.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.24f, 4.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::FocusPull, "FocusPull", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.24f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.34f, 1.05f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.y += interpolate(t, 0.0f, 0.28f, 12.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.18f, 2.5f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::ShakeImpact, "ShakeImpact", [](const FrameContext& ctx, const MotionObject&, f32 t, MotionState& st) {
            const f32 amp = interpolate(t, 0.0f, 0.35f, 18.0f, 0.0f, Easing::OutCubic);
            st.position.x += std::sin(static_cast<f32>(ctx.frame) * 1.7f) * amp;
            st.position.y += std::cos(static_cast<f32>(ctx.frame) * 2.1f) * amp;
        }
    });

    register_preset({
        MotionPreset::TypewriterReveal, "TypewriterReveal", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.14f, 0.0f, 1.0f, Easing::OutCubic);
            st.text_reveal = interpolate(t, 0.0f, 0.80f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += interpolate(t, 0.0f, 0.20f, -24.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.12f, 8.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::KineticBounce, "KineticBounce", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.18f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.35f, 0.78f, 1.0f, Easing::OutBack);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.y += interpolate(t, 0.0f, 0.24f, 42.0f, 0.0f, Easing::OutBounce);
            st.rotation.z += interpolate(t, 0.0f, 0.24f, -3.0f, 0.0f, Easing::OutCubic);
        }
    });

    register_preset({
        MotionPreset::GlitchIn, "GlitchIn", [](const FrameContext& ctx, const MotionObject&, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.08f, 0.0f, 1.0f, Easing::OutCubic);
            st.position.x += std::sin(static_cast<f32>(ctx.frame) * 4.2f) * interpolate(t, 0.0f, 0.18f, 22.0f, 0.0f, Easing::OutCubic);
            st.position.y += std::cos(static_cast<f32>(ctx.frame) * 3.7f) * interpolate(t, 0.0f, 0.18f, 14.0f, 0.0f, Easing::OutCubic);
            st.rotation.z += std::sin(static_cast<f32>(ctx.frame) * 5.0f) * interpolate(t, 0.0f, 0.18f, 4.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.12f, 14.0f, 0.0f, Easing::OutCubic);
        }
    });
}

} // namespace chronon3d::presets::motion
