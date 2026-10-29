#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <optional>

namespace chronon3d {

// Per-layer material properties for 2.5D lighting and shadow interaction.
struct Material2_5D {
    bool  accepts_lights{true};    // false → layer is unlit (full color)
    bool  casts_shadows{false};    // true → layer projects shadow onto receivers
    bool  accepts_shadows{true};   // false → layer is never darkened by shadows
    f32   diffuse{1.0f};           // diffuse coefficient [0..1]
    f32   specular{0.0f};          // specular coefficient [0..1]
    f32   shininess{16.0f};        // Phong shininess exponent
    f32   ambient_multiplier{1.0f};// scales ambient contribution
    Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
    bool emissive_enabled{false};
    bool double_sided{false};
    std::optional<f32> alpha_cutoff{};
    std::optional<f32> metallic{};
    std::optional<f32> roughness{};
};

} // namespace chronon3d
