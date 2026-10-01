#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

/// Hard ceilings for the value-only inline mesh ABI. An individual layer may
/// carry at most 250k vertices and 750k triangle indices; the RenderPlan budget
/// separately limits aggregate decoded payload bytes across all layers.
inline constexpr std::size_t kInlineMeshMaxVertices = 250'000;
inline constexpr std::size_t kInlineMeshMaxIndices = 750'000;
inline constexpr std::uint64_t kInlineMeshMaxAggregateBytes = 64ULL * 1024ULL * 1024ULL;

struct InlineMeshMaterialPlan {
    float diffuse{1.0f};
    float specular{0.25f};
    float shininess{32.0f};
    float rim_intensity{1.0f};
    std::optional<std::array<float, 4>> base_color;
};

struct InlineMeshVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
};

/// One static, indexed TRIANGLES primitive in mesh-local coordinates.
struct InlineMeshPlan {
    std::vector<InlineMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Presence opts the mesh into lighting; omitted preserves legacy flat color.
    std::optional<InlineMeshMaterialPlan> material;
};

/// Bounded deterministic authoring descriptor. Decoding expands this into an
/// InlineMeshPlan before validation, compilation, and fingerprinting.
struct InlineMeshPrimitivePlan {
    std::string kind{"box"};
    float width{1.0f}, height{1.0f}, depth{1.0f};
    float radius{1.0f}, radius_top{1.0f}, radius_bottom{1.0f}, tube{0.25f};
    std::uint32_t width_segments{1}, height_segments{1};
    std::uint32_t radial_segments{16}, tubular_segments{32};
};

/// Expand a supported primitive into triangle positions, normals, UVs, and
/// indices. The same inline-mesh hard ceilings apply before allocations occur.
[[nodiscard]] InlineMeshPlan expand_inline_mesh_primitive(const InlineMeshPrimitivePlan& primitive);

}  // namespace chronon3d::render_plan
