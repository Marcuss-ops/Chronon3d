#pragma once

#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d {

/// Value-only static triangles used by the RenderPlan inline_mesh contract.
/// No asset reference or I/O is implicit at this builder boundary.
struct InlineMeshParams {
    std::string name{"inline_mesh"};
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
};

}  // namespace chronon3d
