// =============================================================================
// Motion Presets — Idle / Parallax / Loop
//
// Continuous-frame ambient motion: parallax drift, orbital float, idle bob.
// These presets do NOT converge — they oscillate around the rest state using
// `ctx.frame.frame.frame` as a phase index (independent of `t`).
// =============================================================================

#include <chronon3d/presets/motion_preset_registry.hpp>
#include <cmath>

namespace chronon3d::presets::motion {

void register_idle_presets(MotionPresetRegistry& r) {
    r.register_preset({
        MotionPreset::ParallaxDrift, "ParallaxDrift", [](const FrameContext& ctx, const MotionObject& obj, f32, MotionState& st) {
            st.position.x += std::sin(static_cast<f32>(ctx.frame.frame.frame) * 0.025f) * 20.0f * obj.motion3d.parallax;
            st.position.y += std::cos(static_cast<f32>(ctx.frame.frame.frame) * 0.018f) * 12.0f * obj.motion3d.parallax;
            st.rotation.y += std::sin(static_cast<f32>(ctx.frame.frame.frame) * 0.02f) * 4.0f;
            st.rotation.x += std::cos(static_cast<f32>(ctx.frame.frame.frame) * 0.017f) * 2.5f;
        }
    });

    r.register_preset({
        MotionPreset::ParallaxFloat, "ParallaxFloat", [](const FrameContext& ctx, const MotionObject& obj, f32, MotionState& st) {
            const f32 frame = static_cast<f32>(ctx.frame.frame.frame);
            st.position.x += std::sin(frame * 0.018f) * 18.0f * obj.motion3d.parallax;
            st.position.y += std::cos(frame * 0.014f) * 10.0f * obj.motion3d.parallax;
            st.rotation.y += std::sin(frame * 0.012f) * 3.5f;
        }
    });

    r.register_preset({
        MotionPreset::FloatIdle, "FloatIdle", [](const FrameContext& ctx, const MotionObject& obj, f32, MotionState& st) {
            const f32 frame = static_cast<f32>(ctx.frame.frame.frame);
            st.position.y += std::sin(frame * 0.025f) * 10.0f * obj.motion3d.parallax;
            st.position.x += std::cos(frame * 0.018f) * 6.0f * obj.motion3d.parallax;
            st.rotation.z += std::sin(frame * 0.012f) * 1.5f;
        }
    });
}

} // namespace chronon3d::presets::motion
