#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>
#include <variant>

namespace chronon3d::registry {

using ShapeParams = std::variant<
    RectParams,
    RoundedRectParams,
    CircleParams,
    LineParams,
    PathParams,
    ImageParams,
    GridBackgroundParams,
    TextParams
>;

} // namespace chronon3d::registry
