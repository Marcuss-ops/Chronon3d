#pragma once

#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <memory>

namespace chronon3d::graph::detail {

[[nodiscard]] std::shared_ptr<Mesh> build_extruded_text_mesh(
    const TextRunShape& shape, float depth);

} // namespace chronon3d::graph::detail
