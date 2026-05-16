#pragma once

#include <chronon3d/core/types.hpp>

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
};

} // namespace chronon3d
