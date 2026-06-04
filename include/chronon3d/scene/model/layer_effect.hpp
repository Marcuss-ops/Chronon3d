#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {

// Layer-level post-processing effects applied to the composited layer content.
// Effects are applied in this order: blur → brightness/contrast → tint.
struct LayerEffect {
    f32   blur_radius{0.0f};    // 0 = no blur; positive = gaussian blur radius in pixels
    f32   brightness{0.0f};     // 0 = unchanged; -1 = black; +1 = full white
    f32   contrast{1.0f};       // 1 = unchanged; 0 = flat grey; >1 = more contrast
    Color tint{0, 0, 0, 0};    // a=0 = no tint; a=1 = full tint color

    // ── Adjustment-layer color correction (AE-5) ──
    f32   saturation{1.0f};    // 1.0 = unchanged, 0 = greyscale
    f32   hue_rotate{0.0f};    // degrees, 0 = unchanged
    f32   invert_amount{0.0f}; // 0 = unchanged, 1 = full invert
    f32   vignette_radius{0.0f};   // 0 = no vignette; fraction of frame diagonal
    f32   vignette_softness{0.5f}; // edge softness
    f32   vignette_amount{0.0f};   // 0 = no vignette, 1 = full darkness
    Color vignette_color{0, 0, 0, 1};

    [[nodiscard]] bool has_any() const {
        return blur_radius > 0.0f || tint.a > 0.0f ||
               brightness != 0.0f || contrast != 1.0f ||
               saturation != 1.0f || hue_rotate != 0.0f ||
               invert_amount != 0.0f || vignette_amount > 0.0f;
    }
};

} // namespace chronon3d
