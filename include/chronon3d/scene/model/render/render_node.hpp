#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/render/render_node_params.hpp>
#include <vector>
#include <memory>
#include <string>
#include <memory_resource>
#include <variant>
#include <optional>
#include <cstdint>

namespace chronon3d {

// Soft glow emanating outward from the shape.
// Unified with GlowParams from effect_params.hpp — the single source of truth.

// What kind of surface the node naturally produces.
// IntrinsicSize: content-driven layers such as text, shapes and images.
// ViewportSize: full-canvas layers such as grid backgrounds.
enum class SurfacePolicy {
    IntrinsicSize,
    ViewportSize,
};

// How placement should be applied downstream.
// MatrixOnly means transforms should not force a new raster surface size.
// RasterizeAfter is reserved for future nodes that need a post-transform bake.
enum class TransformPolicy {
    MatrixOnly,
    RasterizeAfter,
};

struct RenderNode {
    struct GpuParticleInstance {
        Vec2 position{0.0f, 0.0f};
        Color color{1.0f, 1.0f, 1.0f, 1.0f};
        f32 size{1.0f};
        f32 rotation{0.0f};
        f32 sprite_scale{1.0f};
        f32 z{0.0f};
    };
    std::pmr::string name;
    Transform world_transform;
    SurfacePolicy surface_policy{SurfacePolicy::IntrinsicSize};
    TransformPolicy transform_policy{TransformPolicy::MatrixOnly};
    Color color{1, 1, 1, 1};
    Fill fill{true, FillType::Solid, {1, 1, 1, 1}, {}};
    Shape shape;
    // Inline mesh shading is opt-in; absent keeps the historical flat path.
    struct InlineMeshMaterial {
        f32 diffuse{1.0f};
        f32 specular{0.25f};
        f32 shininess{32.0f};
        f32 rim_intensity{1.0f};
        std::optional<Color> base_color;
        std::optional<f32> metallic;
        std::optional<f32> roughness;
        std::optional<Color> emissive;
        bool double_sided{false};
        std::optional<f32> alpha_cutoff;
    };
    std::optional<InlineMeshMaterial> inline_mesh_material;
    // RenderPlan inline-mesh frame samples are frame-varying source geometry.
    // The asset field names an optional RGBA image sampled through vertex UVs.
    std::string inline_mesh_texture_asset;
    std::string inline_mesh_metallic_roughness_texture_asset;
    bool inline_mesh_animated{false};
    // Optional per-emitter payload. CPU shape nodes remain the software
    // authority; strict Vulkan consumes this array in one instanced dispatch.
    std::vector<GpuParticleInstance> gpu_particle_instances;
    bool gpu_particle_bokeh{false};
    std::string gpu_particle_sprite_path;
    struct SvgGroupIsolation {
        struct OpacityAnimation {
            std::int64_t start_frame{0};
            std::int64_t end_frame{0};
            f32 from{1.0f};
            f32 to{1.0f};
            bool discrete{false};
        };
        std::uint32_t id{0};
        f32 opacity{1.0f};
        std::optional<OpacityAnimation> opacity_animation;
    };
    std::optional<SvgGroupIsolation> svg_group;
    std::vector<SvgGroupIsolation> svg_groups;
    /// Per-element SVG clip/mask geometries in the same local coordinates as
    /// the associated path node, outermost scope first.
    std::vector<PathShape> svg_local_masks;
    // Mesh moved to Shape::MeshShape (shape variant index 13).
    // Access via: node.shape.mesh_shape().prepared / .reference
    f32 corner_radius{0.0f};

    // Runtime/shape-specific parameters to avoid hardcoding in RenderNode
    // Type-safe variant: stores FakeBox3DRenderState or GridPlaneRenderState
    using RenderNodeParams = std::variant<std::monostate, FakeBox3DRenderState, GridPlaneRenderState>;
    RenderNodeParams params;

    bool visible{true};

    // ── TextRun discrimination now lives in the Shape variant ────────
    // Check node.shape.type() == ShapeType::TextRun.
    // The TextRunShape payload is accessed via:
    //   node.shape.text_run_shape_handle().value

    explicit RenderNode(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res) {}
};

} // namespace chronon3d
