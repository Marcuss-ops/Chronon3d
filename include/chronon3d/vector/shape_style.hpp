#pragma once

#include <chronon3d/scene/model/shape/path.hpp>

namespace chronon3d {

using StrokeStyle = PathStroke;

struct ShapeStyle {
    Fill fill{Fill::solid_color({1.0f, 1.0f, 1.0f, 1.0f})};
    StrokeStyle stroke{};
    f32 opacity{1.0f};
};

} // namespace chronon3d
