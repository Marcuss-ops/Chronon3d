#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {

// Layer-level post-processing effects applied to the composited layer content.
// Effects are applied in this order: blur → brightness/contrast → tint.
struct LayerEffect {
    f32   blur_radius{0.0f};    // 0 = no blur; positive = gaussian blur radius in pixels
    f32   brightness{0.0f};     // 0 = unchanged; -1 = black; +1 = full white
    f32   contrast{1.0f};       // 1 = unchanged; 0 = flat grey; >1 = more contrast
    Color tint{0, 0, 0, 0};    // a=0 = no tint; a=1 = full tint color

    [[nodiscard]] bool has_any() const {
        return blur_radius > 0.0f || tint.a > 0.0f ||
               brightness != 0.0f || contrast != 1.0f;
    }
};

} // namespace chronon3d
