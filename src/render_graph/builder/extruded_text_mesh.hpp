#pragma once

#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <memory>
#include <optional>

namespace chronon3d::graph::detail {

[[nodiscard]] std::shared_ptr<Mesh> build_extruded_text_mesh(
    const TextRunShape& shape,
    float depth,
    std::optional<float> bevel_width_override = std::nullopt);

} // namespace chronon3d::graph::detail
