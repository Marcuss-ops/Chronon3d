#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d::render_plan {

/// Hard ceilings for the value-only inline mesh ABI. An individual layer may
/// carry at most 250k vertices and 750k triangle indices; the RenderPlan budget
/// separately limits aggregate decoded payload bytes across all layers.
inline constexpr std::size_t kInlineMeshMaxVertices = 250'000;
inline constexpr std::size_t kInlineMeshMaxIndices = 750'000;
inline constexpr std::uint64_t kInlineMeshMaxAggregateBytes = 64ULL * 1024ULL * 1024ULL;

struct InlineMeshVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
};

/// One static, indexed TRIANGLES primitive in mesh-local coordinates.
struct InlineMeshPlan {
    std::vector<InlineMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

}  // namespace chronon3d::render_plan
