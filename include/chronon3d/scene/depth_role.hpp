#pragma once

#include <chronon3d/core/types.hpp>

namespace chronon3d {

// Semantic Z-depth roles for 3D layers.
// Convention: Z negative = closer to camera, Z positive = farther.
// Camera default at z = -1000. Subject (z = 0) renders at scale 1.0.
enum class DepthRole {
    None,           // no role — use explicit position.z
    FarBackground,  // z =  2000  scale ≈ 0.33
    Background,     // z =  1200  scale ≈ 0.45
    Midground,      // z =   600  scale ≈ 0.63
    Subject,        // z =     0  scale = 1.0   (normal plane)
    Atmosphere,     // z =  -300  scale ≈ 1.43  (slight depth-of-field haze)
    Foreground,     // z =  -500  scale = 2.0
    Overlay,        // z =  -800  scale = 5.0   (typically used without enable_3d)
};

// Resolves a DepthRole to a world-space Z value.
// Use depth_offset to fine-tune within a role.
struct DepthRoleResolver {
    [[nodiscard]] static constexpr f32 z_for(DepthRole role) noexcept {
        switch (role) {
            case DepthRole::FarBackground: return  2000.0f;
            case DepthRole::Background:    return  1200.0f;
            case DepthRole::Midground:     return   600.0f;
            case DepthRole::Subject:       return     0.0f;
            case DepthRole::Atmosphere:    return  -300.0f;
            case DepthRole::Foreground:    return  -500.0f;
            case DepthRole::Overlay:       return  -800.0f;
            case DepthRole::None:
            default:                       return     0.0f;
        }
    }
};

} // namespace chronon3d
