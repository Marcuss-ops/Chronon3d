// include/chronon3d/rendering/shadow_settings.hpp
#pragma once
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::rendering {

struct ShadowSettings {
    f32 opacity{0.35f};       // shadow alpha multiplier
    f32 blur_radius{8.0f};    // Gaussian blur radius in pixels
    f32 px_per_unit{1.0f};    // world units → screen pixels (explicit V1, not derived)
    f32 max_offset{300.0f};   // screen-space offset clamp
    f32 contact_opacity{0.28f};
    f32 contact_blur_radius{10.0f};
    f32 ambient_opacity{0.08f};
    f32 ambient_blur_radius{96.0f};

    // ── Depth-aware extension ────────────────────────────────────
    // When enabled, shadow blur and opacity vary with the Z-distance
    // between caster and receiver, communicating depth visually.
    //
    // Formulas (dz = |caster_z - receiver_z|):
    //   ambient_blur  = base_ambient_blur + dz * blur_per_z
    //   opacity_scale = 1.0 / (1.0 + dz * opacity_falloff)
    //   tint          = shadow tint colour (default dark blue)
    bool  depth_aware{true};
    f32   blur_per_z{0.04f};       // extra blur per unit of Z-distance
    f32   opacity_falloff{0.0025f}; // opacity attenuation per unit of Z-distance
    Color tint{0.03f, 0.04f, 0.08f, 1.0f}; // shadow tint colour
};

} // namespace chronon3d::rendering
