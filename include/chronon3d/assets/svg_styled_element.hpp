#pragma once

#include <chronon3d/scene/model/shape/path.hpp>

#include <optional>

namespace chronon3d::assets {

struct SvgStyledElement {
    PathShape path;
    std::optional<Fill> fill;
    PathStroke stroke;
    float opacity{1.0f};
};

} // namespace chronon3d::assets
