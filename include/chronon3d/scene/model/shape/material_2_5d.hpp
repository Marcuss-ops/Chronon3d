#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <optional>
#include <string>

namespace chronon3d {

// Per-layer material properties for 2.5D lighting and shadow interaction.
struct Material2_5D {
    bool  accepts_lights{true};    // false → layer is unlit (full color)
    bool  casts_shadows{false};    // true → layer projects shadow onto receivers
    bool  accepts_shadows{true};   // false → layer is never darkened by shadows
    bool  shadow_catcher{false};   // true → emit only received shadow alpha
    f32   diffuse{1.0f};           // diffuse coefficient [0..1]
    f32   specular{0.0f};          // specular coefficient [0..1]
    f32   shininess{16.0f};        // Phong shininess exponent
    f32   ambient_multiplier{1.0f};// scales ambient contribution
    // ── 2.5D image relighting (opt-in; both default to the flat legacy path) ──
    f32   image_bump_strength{0.0f};   // >0 derives a normal from image luminance
    f32   image_rim_strength{0.0f};    // >0 adds an alpha-edge rim light
    f32   image_rim_width_px{4.0f};    // alpha blur radius for the rim edge
    // Optional projected gobo and background plate. Empty asset strings keep
    // the historical flat path; the paths are resolved through ImageCache at
    // execution, so no pixel data is copied into the material.
    std::string image_gobo_asset{};
    f32   image_gobo_scale{0.0008f};   // canvas pixels -> gobo UV
    Vec2  image_gobo_offset{0.0f, 0.0f};
    std::string image_light_wrap_asset{};
    f32   image_light_wrap_strength{0.0f}; // background bleed on the alpha edge
    Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
    bool emissive_enabled{false};
    bool double_sided{false};
    std::optional<f32> alpha_cutoff{};
    std::optional<f32> metallic{};
    std::optional<f32> roughness{};
};

} // namespace chronon3d
