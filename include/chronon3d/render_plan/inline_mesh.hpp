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
/// Baked per-frame position tracks: at most 4096 samples, frames in
/// [0, 1000000] strictly increasing, one position per base vertex.
inline constexpr std::size_t kInlineMeshMaxAnimationSamples = 4096;
inline constexpr std::int64_t kInlineMeshMaxAnimationFrame = 1'000'000;

struct InlineMeshMaterialPlan {
    float diffuse{1.0f};
    float specular{0.25f};
    float shininess{32.0f};
    float rim_intensity{1.0f};
    std::optional<std::array<float, 4>> base_color;
    // Exact metallic-roughness values are retained alongside legacy Phong
    // approximations so downstream IBL consumers do not need to infer them.
    std::optional<float> metallic;
    std::optional<float> roughness;
    std::optional<std::array<float, 3>> emissive;
    std::optional<bool> double_sided;
    std::optional<float> alpha_cutoff;
};

struct InlineMeshVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> uv{};
};

/// One baked per-frame position sample. Topology is fixed by the base mesh:
/// `positions` must carry exactly one entry per base vertex.
struct InlineMeshFrameSamplePlan {
    std::int64_t frame{0};
    std::vector<std::array<float, 3>> positions;
    // Optional authored/deformed normals, present consistently for all samples.
    std::vector<std::array<float, 3>> normals;
};

/// One static, indexed TRIANGLES primitive in mesh-local coordinates.
struct InlineMeshPlan {
    std::vector<InlineMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    /// Optional linear metallic-roughness image; green carries roughness and
    /// blue carries metallic according to glTF metallicRoughness.
    std::string metallic_roughness_texture_asset;
    // Presence opts the mesh into lighting; omitted preserves legacy flat color.
    std::optional<InlineMeshMaterialPlan> material;
    // Optional baked geometry track (V3 only). The software renderer samples
    // positions and optional normals while retaining base UVs, indices,
    // material, and texture assets.
    std::vector<InlineMeshFrameSamplePlan> animation;
};

/// Bounded deterministic authoring descriptor. Decoding expands this into an
/// InlineMeshPlan before validation, compilation, and fingerprinting.
struct InlineMeshPrimitivePlan {
    std::string kind{"box"};
    float width{1.0f}, height{1.0f}, depth{1.0f};
    float radius{1.0f}, radius_top{1.0f}, radius_bottom{1.0f}, tube{0.25f};
    std::uint32_t width_segments{1}, height_segments{1};
    std::uint32_t radial_segments{16}, tubular_segments{32};
    std::vector<std::array<float, 2>> profile;
    float bevel{0.0f};
};

/// Expand a supported primitive into triangle positions, normals, UVs, and
/// indices. The same inline-mesh hard ceilings apply before allocations occur.
[[nodiscard]] InlineMeshPlan expand_inline_mesh_primitive(const InlineMeshPrimitivePlan& primitive);

}  // namespace chronon3d::render_plan
