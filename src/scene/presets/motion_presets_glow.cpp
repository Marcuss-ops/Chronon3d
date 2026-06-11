// =============================================================================
// Motion Presets — Glow / Bloom
//
// Multi-layer luminous reveals: text glow halos, atmospheric bloom washes.
// These presets enable and configure the effect stack on MotionState.
// =============================================================================

#include <chronon3d/presets/motion_preset_registry.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::presets::motion {

void register_glow_presets(MotionPresetRegistry& r) {
    r.register_preset({
        MotionPreset::GlowBloom, "GlowBloom", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.34f, 0.0f, 1.0f, Easing::OutCubic);
            const f32 s = interpolate(t, 0.0f, 0.30f, 1.03f, 1.0f, Easing::OutCubic);
            st.scale = {obj.scale_value.x * s, obj.scale_value.y * s, obj.scale_value.z};
            // Limit the layer-level blur to the first 30 frames so the sharp
            // text remains crisp during the rest of the reveal.  The glow
            // itself still blooms independently (driven by the per-layer
            // strengths below); we only avoid blurring the entire layer,
            // which would also smear the text.
            const f32 anim_dur = static_cast<f32>(obj.time_value.end - obj.time_value.start);
            const f32 blur_t_max = (anim_dur > 0.0f)
                ? std::min(1.0f, 30.0f / anim_dur)
                : 0.5f;
            st.blur = interpolate(t, 0.0f, blur_t_max, 14.0f, 0.0f, Easing::OutCubic);

            st.effects.glow_enabled = true;
            // Premium multi-layer glow — whisper-thin atmosphere, not a
            // coloured halo.  The sharp text renders on TOP, keeping it
            // crisp while the glow layers provide subtle depth.
            //
            // Layer breakdown (at settled radius ≈ 18px):
            //   core: 0.10 × 18 = 1.8 px blur, 12%  → tight character hug
            //   aura: 0.35 × 18 = 6.3 px blur,  5%  → soft between-letters
            //   bloom: 1.00 × 18 = 18  px blur, 1.5% → wide atmospheric wash
            const f32 bloom_mix = std::clamp(1.0f - (st.blur / 14.0f), 0.0f, 1.0f);
            st.effects.glow.radius = interpolate(t, 0.0f, 0.40f, 34.0f, 18.0f, Easing::OutCubic);
            st.effects.glow.intensity = 1.0f + 0.2f * bloom_mix;
            st.effects.glow.core_strength = 0.6f;   // inner: tight character glow (visible)
            st.effects.glow.aura_strength  = 0.3f;   // mid:   soft between-letters
            st.effects.glow.bloom_strength = 0.12f;  // outer: wide atmospheric wash
            // Cool blue-cyan with a faint premium tint on the outer wash
            st.effects.glow.color = Color{
                0.20f + 0.75f * bloom_mix,
                0.40f + 0.55f * bloom_mix,
                1.0f,
                0.40f + 0.50f * bloom_mix
            };
        }
    });

    r.register_preset({
        MotionPreset::GlowReveal3D, "GlowReveal3D", [](const FrameContext&, const MotionObject& obj, f32 t, MotionState& st) {
            st.opacity *= interpolate(t, 0.0f, 0.30f, 0.0f, 1.0f, Easing::OutCubic);
            st.text_reveal = interpolate(t, 0.0f, 0.80f, 0.0f, 1.0f, Easing::OutCubic);
            st.blur = interpolate(t, 0.0f, 0.25f, 16.0f, 0.0f, Easing::OutCubic);
            st.position.z += interpolate(t, 0.0f, 0.40f, 180.0f, 0.0f, Easing::OutCubic);
            st.rotation.x += interpolate(t, 0.0f, 0.40f, 15.0f, 0.0f, Easing::OutCubic);
            st.effects.glow_enabled = true;
            st.effects.glow.intensity = interpolate(t, 0.0f, 0.50f, 1.2f, 0.40f, Easing::OutCubic);
            st.effects.glow.radius = interpolate(t, 0.0f, 0.50f, 50.0f, 15.0f, Easing::OutCubic);
        }
    });
}

} // namespace chronon3d::presets::motion
