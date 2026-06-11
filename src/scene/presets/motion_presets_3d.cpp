// =============================================================================
// Motion Presets — 3D / 2.5D
//
// Camera-relative depth animations: push-in, dolly, orbit, tilt, perspective.
// Requires Motion3D::enabled == true on the MotionObject.
// =============================================================================

#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::presets::motion {

void register_3d_presets(MotionPresetRegistry& r) {
    r.register_preset({
        MotionPreset::PushIn3D, "PushIn3D", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.28f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.30f, 0.88f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.z += interpolate(t, 0.0f, 0.40f, 260.0f, 0.0f, Easing::OutCubic);
            st.rotation.y += interpolate(t, 0.0f, 0.40f, -14.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.26f, 14.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::SoftDollyReveal, "SoftDollyReveal", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.38f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.42f, 0.94f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.30f, 4.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::DollyRotate2_5D, "DollyRotate2_5D", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            const f32 reveal = interpolate(t, 0.0f, 0.40f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 settle = std::clamp((reveal - 0.70f) / 0.30f, 0.0f, 1.0f);

            st.opacity *= reveal;
            const f32 s = interpolate(t, 0.0f, 0.42f, 0.92f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.z += interpolate(t, 0.0f, 0.46f, 220.0f, 0.0f, Easing::OutCubic);
            st.rotation.y += interpolate(t, 0.0f, 0.40f, -18.0f, 0.0f, Easing::OutCubic);
            st.rotation.z += interpolate(t, 0.0f, 0.32f, 6.0f, 0.0f, Easing::OutCubic);
            st.rotation.x += interpolate(t, 0.0f, 0.28f, 2.5f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.24f, 5.0f, 0.0f, Easing::OutCubic);

            if (settle > 0.0f) {
                const f32 settle_wave = std::sin(settle * 3.1415926535f);
                st.rotation.y += settle_wave * 1.25f * settle;
                st.rotation.z += settle_wave * 0.8f * settle;
                st.position.x += settle_wave * 12.0f * settle;
                st.position.z -= 8.0f * settle;
            }
        }
    });

    r.register_preset({
        MotionPreset::Orbit2_5D, "Orbit2_5D", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            constexpr f32 pi = 3.14159265358979323846f;
            const f32 angle = interpolate(t, 0.0f, 1.0f, -12.0f, 12.0f, Easing::InOutCubic);
            st.rotation.y += angle;
            st.position.x += std::sin(t * pi * 2.0f) * 40.0f;
            st.position.z += std::cos(t * pi * 2.0f) * 80.0f;
        }
    });

    r.register_preset({
        MotionPreset::TiltSweep2_5D, "TiltSweep2_5D", [](const FrameContext&, const MotionObject&, f32, MotionState&) {
            // Handled natively by Sweep2_5D amplitude calculation
        }
    });

    r.register_preset({
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

    r.register_preset({
        MotionPreset::CinematicPushIn, "CinematicPushIn", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            st.position.z += interpolate(t, 0.0f, 0.45f, 320.0f, 0.0f, Easing::OutCubic);
            st.rotation.y += interpolate(t, 0.0f, 0.45f, -12.0f, 0.0f, Easing::OutCubic);
            st.rotation.x += interpolate(t, 0.0f, 0.45f, 4.0f, 0.0f, Easing::OutCubic);
            st.opacity *= interpolate(t, 0.0f, 0.22f, 0.0f, 1.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::OrbitCard, "OrbitCard", [](const FrameContext&, const MotionObject&, f32 t, MotionState& st) {
            constexpr f32 pi = 3.1415926535f;
            st.rotation.y += interpolate(t, 0.0f, 1.0f, -18.0f, 18.0f, Easing::InOutCubic);
            st.position.z += std::cos(t * pi) * 60.0f;
        }
    });

    r.register_preset({
        MotionPreset::DepthReveal, "DepthReveal", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.32f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.38f, 0.94f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.position.z += interpolate(t, 0.0f, 0.42f, 320.0f, 0.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.26f, 5.0f, 0.0f, Easing::OutCubic);
            st.rotation.y += interpolate(t, 0.0f, 0.40f, -8.0f, 0.0f, Easing::OutCubic);
        }
    });

    r.register_preset({
        MotionPreset::CardFlip2_5D, "CardFlip2_5D", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.22f, 0.0f, 1.0f, Easing::OutCubic);
            st.rotation.y += interpolate(t, 0.0f, 0.45f, -90.0f, 0.0f, Easing::OutCubic);
            st.position.z += interpolate(t, 0.0f, 0.45f, 240.0f, 0.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.40f, 0.88f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            st.blur = interpolate(t, 0.0f, 0.28f, 8.0f, 0.0f, Easing::OutCubic);
        }
    });
}

} // namespace chronon3d::presets::motion
