#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace raster {

struct BBox {
    i32 x0, y0, x1, y1;
    
    void clip_to(i32 w, i32 h) {
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(w, x1);
        y1 = std::min(h, y1);
    }
    
    bool is_empty() const { return x0 >= x1 || y0 >= y1; }
};

} // namespace raster
} // namespace chronon3d
