#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/render/render_runtime.hpp>
#include <vector>
#include <memory>
#include <string>
#include <memory_resource>
#include <variant>

namespace chronon3d {

class FontEngine;  // forward declaration

// Soft drop shadow drawn behind the shape.
struct DropShadow {
    bool  enabled{false};
    Vec2  offset{0.0f, 8.0f};
    Color color{0.0f, 0.0f, 0.0f, 0.35f};
    f32   radius{12.0f};
};

// Soft glow emanating outward from the shape.
// Unified with GlowParams from effect_params.hpp — the single source of truth.
using Glow = GlowParams;

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
    std::pmr::string name;
    Transform world_transform;
    SurfacePolicy surface_policy{SurfacePolicy::IntrinsicSize};
    TransformPolicy transform_policy{TransformPolicy::MatrixOnly};
    Color color{1, 1, 1, 1};
    Fill fill{true, FillType::Solid, {1, 1, 1, 1}, {}};
    Shape shape;
    DropShadow shadow;
    GlowParams glow;
    // Mesh moved to Shape::MeshShape (shape variant index 13).
    // Access via: node.shape.mesh_shape().mesh
    f32 corner_radius{0.0f};

    // Runtime/shape-specific parameters to avoid hardcoding in RenderNode
    // Type-safe variant: stores FakeBox3DRenderState or GridPlaneRenderState
    using RenderNodeParams = std::variant<std::monostate, FakeBox3DRenderState, GridPlaneRenderState>;
    RenderNodeParams params;

    bool visible{true};

    // FontEngine pointer for precise text shaping / glyph metrics.
    // Set by LayerBuilder when the layer has a configured FontEngine.
    FontEngine* font_engine{nullptr};

    // ── TextRun discrimination now lives in the Shape variant ────────
    // Check node.shape.type() == ShapeType::TextRun.
    // The TextRunShape payload is accessed via:
    //   node.shape.text_run_shape_handle().value

    explicit RenderNode(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res) {}
};

} // namespace chronon3d
