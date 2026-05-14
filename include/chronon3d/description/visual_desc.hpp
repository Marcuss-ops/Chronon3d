#pragma once

// VisualDesc -- descriptive visual elements for SceneDescription / LayerDesc.
// Reuses the existing builder_params types so no duplication.
// In v1, visuals are static (not animated individually).

#include <chronon3d/scene/builder_params.hpp>
#include <variant>

namespace chronon3d {

using VisualDesc = std::variant<
    RectParams,
    RoundedRectParams,
    CircleParams,
    LineParams,
    TextParams,
    ImageParams,
    FakeExtrudedTextParams
>;

} // namespace chronon3d
